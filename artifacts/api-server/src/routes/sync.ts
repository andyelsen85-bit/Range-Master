import { Router } from "express";
import { db, spielerTable, spieleTable, spielTeilnahmenTable, ergebnisseTable } from "@workspace/db";
import { eq, inArray, sql } from "drizzle-orm";
import { requireApiKey } from "./auth";
import { z } from "zod";

const router = Router();

const maschineValues = ["A", "B", "C", "D", "E", "F", "G", "H"] as const;
const modusValues = ["NORMAL", "HARAKIRI", "HARAKIRI_DELAYED", "HARAKIRI_FULL", "CUSTOM_1", "CUSTOM_2", "CUSTOM_3"] as const;

const ErgebnisSchema = z.object({
  spielerId: z.number().int(),
  lauf: z.number().int().min(1).max(2),
  taube: z.number().int().min(1),          // no upper limit — custom games can have many tauben
  maschine: z.enum(maschineValues),
  posten: z.number().int().min(1).max(6),
  schuss1: z.boolean(),
  schuss2: z.boolean(),
  punkte: z.number().int().min(0).max(4),  // per-taube max is always 2 (or 4 for H? no — still 2 per clay)
  wiederholt: z.boolean().default(false),
});

const SpielSchema = z.object({
  externalId: z.string().uuid(),
  datum: z.string().datetime(),
  modus: z.enum(modusValues),
  lauf: z.number().int().min(1).max(2),
  taubenProLauf: z.number().int().min(1).default(9),
  abgeschlossen: z.boolean(),
  teilnahmen: z.array(
    z.object({
      spielerId: z.number().int(),
      startPosten: z.number().int().min(1).max(6),
      punkte: z.number().int().min(0),     // no upper cap — depends on custom sequence length
      lauf: z.number().int().min(1).max(2),
    })
  ),
  ergebnisse: z.array(ErgebnisSchema),
});

// GET /api/sync/status
router.get("/status", requireApiKey, async (_req, res) => {
  const rows = await db.select({ id: spielerTable.id }).from(spielerTable);
  return res.json({
    status: "ok",
    timestamp: new Date().toISOString(),
    spielerCount: rows.length,
  });
});

// GET /api/sync/spieler
router.get("/spieler", requireApiKey, async (_req, res) => {
  const rows = await db
    .select({ id: spielerTable.id, name: spielerTable.name, mitgliedNr: spielerTable.mitgliedNr })
    .from(spielerTable)
    .orderBy(spielerTable.name);
  return res.json({ spieler: rows });
});

// POST /api/sync/spieler
// Accepts players created locally on the terminal. Local IDs are negative;
// the response includes a mapping localId → server id so the terminal can
// de-duplicate. Matching is done case-insensitively by name.
router.post("/spieler", requireApiKey, async (req, res) => {
  const body = z.object({
    spieler: z.array(z.object({ id: z.number(), name: z.string().min(1), mitgliedNr: z.string().nullable().optional() })),
  }).parse(req.body);

  let synced = 0;
  const mappings: Array<{ localId: number; id: number; name: string; status: "created" | "matched" | "existing" }> = [];

  for (const s of body.spieler) {
    const name = s.name.trim();

    // Server-assigned (positive) IDs: verify existence, nothing to create
    if (s.id > 0) {
      const existing = await db.select({ id: spielerTable.id }).from(spielerTable).where(eq(spielerTable.id, s.id)).limit(1);
      if (existing[0]) {
        mappings.push({ localId: s.id, id: existing[0].id, name, status: "existing" });
        continue;
      }
    }

    // De-duplicate by name (case-insensitive) before inserting
    const byName = await db
      .select({ id: spielerTable.id, name: spielerTable.name })
      .from(spielerTable)
      .where(sql`lower(${spielerTable.name}) = lower(${name})`)
      .limit(1);

    if (byName[0]) {
      mappings.push({ localId: s.id, id: byName[0].id, name: byName[0].name, status: "matched" });
      continue;
    }

    const [inserted] = await db
      .insert(spielerTable)
      .values({ name, mitgliedNr: s.mitgliedNr ?? null })
      .returning({ id: spielerTable.id });
    synced++;
    mappings.push({ localId: s.id, id: inserted.id, name, status: "created" });
  }

  return res.json({ synced, mappings });
});

// POST /api/sync/spiele
router.post("/spiele", requireApiKey, async (req, res) => {
  const body = z.object({ spiele: z.array(SpielSchema) }).parse(req.body);
  const results = [];

  for (const s of body.spiele) {
    const existing = await db
      .select({ id: spieleTable.id })
      .from(spieleTable)
      .where(eq(spieleTable.externalId, s.externalId))
      .limit(1);

    if (existing[0]) {
      results.push({ externalId: s.externalId, status: "skipped" as const });
      continue;
    }

    const [spiel] = await db
      .insert(spieleTable)
      .values({
        externalId: s.externalId,
        datum: new Date(s.datum),
        modus: s.modus,
        lauf: s.lauf,
        taubenProLauf: s.taubenProLauf,
        abgeschlossen: s.abgeschlossen,
        syncedAt: new Date(),
      })
      .returning({ id: spieleTable.id });

    if (s.teilnahmen.length) {
      await db.insert(spielTeilnahmenTable).values(
        s.teilnahmen.map((t) => ({ spielId: spiel.id, ...t }))
      );
    }
    if (s.ergebnisse.length) {
      await db.insert(ergebnisseTable).values(
        s.ergebnisse.map((e) => ({ spielId: spiel.id, ...e }))
      );
    }

    results.push({ externalId: s.externalId, status: "created" as const });
  }

  return res.json({ results });
});

export default router;
