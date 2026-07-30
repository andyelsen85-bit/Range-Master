import { Router } from "express";
import bcrypt from "bcryptjs";
import { db, spielerTable, spielTeilnahmenTable, ergebnisseTable } from "@workspace/db";
import { eq } from "drizzle-orm";
import { sql } from "drizzle-orm";
import { authenticate, requireAdmin } from "./auth";
import { z } from "zod";

const router = Router();

// All admin routes require auth + admin role
router.use(authenticate, requireAdmin);

// GET /api/admin/spieler — all players with game stats
router.get("/spieler", async (_req, res) => {
  const spieler = await db
    .select({
      id: spielerTable.id,
      name: spielerTable.name,
      email: spielerTable.email,
      mitgliedNr: spielerTable.mitgliedNr,
      portalAktiv: spielerTable.portalAktiv,
      isAdmin: spielerTable.isAdmin,
      createdAt: spielerTable.createdAt,
    })
    .from(spielerTable)
    .orderBy(spielerTable.name);

  // Per-game stats for all players in one query
  const statsResult = await db.execute(sql`
    SELECT
      spieler_id,
      COUNT(DISTINCT spiel_id)::int        AS anzahl_spiele,
      COALESCE(ROUND(AVG(game_total)::numeric, 1), 0) AS durchschnitt,
      COALESCE(MAX(game_total), 0)         AS best_punkte
    FROM (
      SELECT spieler_id, spiel_id, SUM(punkte) AS game_total
      FROM spiel_teilnahmen
      GROUP BY spieler_id, spiel_id
    ) t
    GROUP BY spieler_id
  `);

  const statsMap = new Map(
    (statsResult.rows as any[]).map((r) => [Number(r.spieler_id), r]),
  );

  return res.json({
    spieler: spieler.map((s) => {
      const st = statsMap.get(s.id);
      return {
        ...s,
        anzahlSpiele: st ? Number(st.anzahl_spiele) : 0,
        durchschnitt: st ? Number(st.durchschnitt) : 0,
        bestPunkte: st ? Number(st.best_punkte) : 0,
      };
    }),
  });
});

// POST /api/admin/spieler — create player
router.post("/spieler", async (req, res) => {
  const schema = z.object({
    name: z.string().min(1),
    email: z.string().email().optional().or(z.literal("")).transform((v) => v || null),
    mitgliedNr: z.string().optional().transform((v) => v || null),
    portalAktiv: z.boolean().default(false),
    isAdmin: z.boolean().default(false),
    passwort: z.string().min(6).optional(),
  });
  const body = schema.parse(req.body);

  let passwortHash: string | null = null;
  if (body.passwort) {
    passwortHash = await bcrypt.hash(body.passwort, 10);
  }

  const [created] = await db
    .insert(spielerTable)
    .values({
      name: body.name,
      email: body.email ?? null,
      mitgliedNr: body.mitgliedNr ?? null,
      portalAktiv: body.passwort ? true : body.portalAktiv,
      isAdmin: body.isAdmin,
      passwortHash,
    })
    .returning();

  return res.status(201).json(created);
});

// PUT /api/admin/spieler/:id — update player
router.put("/spieler/:id", async (req, res) => {
  const id = Number(req.params.id);
  const schema = z.object({
    name: z.string().min(1),
    email: z.string().email().nullable().optional(),
    mitgliedNr: z.string().nullable().optional(),
    portalAktiv: z.boolean(),
    isAdmin: z.boolean(),
  });
  const body = schema.parse(req.body);

  const [updated] = await db
    .update(spielerTable)
    .set({
      name: body.name,
      email: body.email ?? null,
      mitgliedNr: body.mitgliedNr ?? null,
      portalAktiv: body.portalAktiv,
      isAdmin: body.isAdmin,
    })
    .where(eq(spielerTable.id, id))
    .returning();

  if (!updated) return res.status(404).json({ error: "Nicht gefunden" });
  return res.json(updated);
});

// DELETE /api/admin/spieler/:id — delete player + all their data
router.delete("/spieler/:id", async (req, res) => {
  const id = Number(req.params.id);
  const requestingUserId = (req as any).user.id;

  if (id === requestingUserId) {
    return res.status(400).json({ error: "Kann den eegene Benotzer net läschen" });
  }

  // Delete in dependency order (no cascade on spieler_id FK)
  await db.delete(ergebnisseTable).where(eq(ergebnisseTable.spielerId, id));
  await db.delete(spielTeilnahmenTable).where(eq(spielTeilnahmenTable.spielerId, id));
  await db.delete(spielerTable).where(eq(spielerTable.id, id));

  return res.json({ deleted: true });
});

// PUT /api/admin/spieler/:id/passwort — reset any player's password
router.put("/spieler/:id/passwort", async (req, res) => {
  const id = Number(req.params.id);
  const schema = z.object({ neuesPasswort: z.string().min(6) });
  const { neuesPasswort } = schema.parse(req.body);

  const passwortHash = await bcrypt.hash(neuesPasswort, 10);
  const [updated] = await db
    .update(spielerTable)
    .set({ passwortHash, portalAktiv: true })
    .where(eq(spielerTable.id, id))
    .returning({ id: spielerTable.id });

  if (!updated) return res.status(404).json({ error: "Nicht gefunden" });
  return res.json({ success: true });
});

export default router;
