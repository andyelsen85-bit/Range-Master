import { Router } from "express";
import bcrypt from "bcryptjs";
import { randomBytes } from "crypto";
import { db, spielerTable, spielTeilnahmenTable, ergebnisseTable, apiKeysTable, kreditEventsTable, smtpSettingsTable } from "@workspace/db";
import { buildTransport } from "../lib/mailer";
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

// POST /api/admin/spieler — create player (mitgliedNr auto-generated)
router.post("/spieler", async (req, res) => {
  const schema = z.object({
    name: z.string().min(1),
    email: z.string().email().optional().or(z.literal("")).transform((v) => v || null),
    portalAktiv: z.boolean().default(false),
    isAdmin: z.boolean().default(false),
    passwort: z.string().min(6).optional(),
  });
  const body = schema.parse(req.body);

  // Auto-generate next WLZ number using typed select
  const [maxRow] = await db
    .select({
      maxNr: sql<number | null>`MAX(CAST(SUBSTRING(${spielerTable.mitgliedNr} FROM 4) AS INTEGER))`,
    })
    .from(spielerTable)
    .where(sql`${spielerTable.mitgliedNr} ~ '^WLZ[0-9]+$'`);
  const next = (maxRow?.maxNr ?? 0) + 1;
  const mitgliedNr = `WLZ${String(next).padStart(3, "0")}`;

  let passwortHash: string | null = null;
  if (body.passwort) {
    passwortHash = await bcrypt.hash(body.passwort, 10);
  }

  const [created] = await db
    .insert(spielerTable)
    .values({
      name: body.name,
      email: body.email ?? null,
      mitgliedNr,
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

// GET /api/admin/kredite?datum=YYYY-MM-DD — read-only day-credit overview
router.get("/kredite", async (req, res) => {
  const datum = z.string().regex(/^\d{4}-\d{2}-\d{2}$/).catch(new Date().toISOString().slice(0, 10)).parse(req.query.datum);
  const rows = await db
    .select({
      spielerId: kreditEventsTable.spielerId,
      name: spielerTable.name,
      mitgliedNr: spielerTable.mitgliedNr,
      gewaehrt: sql<number>`COALESCE(SUM(CASE WHEN ${kreditEventsTable.typ} = 'GRANT' THEN ${kreditEventsTable.anzahl} ELSE 0 END), 0)::int`,
      verbraucht: sql<number>`COALESCE(SUM(CASE WHEN ${kreditEventsTable.typ} = 'USE' THEN ${kreditEventsTable.anzahl} ELSE 0 END), 0)::int`,
    })
    .from(kreditEventsTable)
    .innerJoin(spielerTable, eq(kreditEventsTable.spielerId, spielerTable.id))
    .where(eq(kreditEventsTable.datum, datum))
    .groupBy(kreditEventsTable.spielerId, spielerTable.name, spielerTable.mitgliedNr)
    .orderBy(spielerTable.name);
  return res.json({ datum, kredite: rows });
});

// ─── API Key routes ───────────────────────────────────────────────────────────

// GET /api/admin/api-keys — list keys (last 8 chars only)
router.get("/api-keys", async (_req, res) => {
  const rows = await db
    .select()
    .from(apiKeysTable)
    .orderBy(apiKeysTable.id);
  return res.json({
    keys: rows.map((k) => ({
      id: k.id,
      name: k.name,
      key: k.key.slice(-8),
      type: k.type,
      active: k.active,
      createdAt: k.createdAt,
    })),
  });
});

// POST /api/admin/api-keys/:id/regenerate — generate new key, return full value ONCE
router.post("/api-keys/:id/regenerate", async (req, res) => {
  const id = Number(req.params.id);
  const newKey = randomBytes(32).toString("hex");
  const [updated] = await db
    .update(apiKeysTable)
    .set({ key: newKey })
    .where(eq(apiKeysTable.id, id))
    .returning({ id: apiKeysTable.id });
  if (!updated) return res.status(404).json({ error: "Nicht gefunden" });
  return res.json({ id, key: newKey });
});

// PATCH /api/admin/api-keys/:id — toggle active status
router.patch("/api-keys/:id", async (req, res) => {
  const id = Number(req.params.id);
  const schema = z.object({ active: z.boolean() });
  const { active } = schema.parse(req.body);
  const [updated] = await db
    .update(apiKeysTable)
    .set({ active })
    .where(eq(apiKeysTable.id, id))
    .returning({ id: apiKeysTable.id });
  if (!updated) return res.status(404).json({ error: "Nicht gefunden" });
  return res.json({ success: true });
});

// ─── SMTP settings ────────────────────────────────────────────────────────────

const SmtpSchema = z.object({
  host: z.string(),
  port: z.number().int().min(1).max(65535),
  username: z.string(),
  passwort: z.string().optional(), // omitted/empty = keep existing
  fromAddress: z.string().email().or(z.literal("")),
  verschluesselung: z.enum(["NONE", "STARTTLS", "SSL"]),
  ignoreTlsErrors: z.boolean(),
  portalUrl: z.string(),
});

async function loadSmtpRow() {
  const rows = await db.select().from(smtpSettingsTable).limit(1);
  return rows[0] ?? null;
}

// GET /api/admin/smtp — settings without password
router.get("/smtp", async (_req, res) => {
  const s = await loadSmtpRow();
  return res.json({
    host: s?.host ?? "",
    port: s?.port ?? 587,
    username: s?.username ?? "",
    fromAddress: s?.fromAddress ?? "",
    verschluesselung: s?.verschluesselung ?? "STARTTLS",
    ignoreTlsErrors: s?.ignoreTlsErrors ?? false,
    portalUrl: s?.portalUrl ?? "",
    passwortGesat: !!s?.passwort,
    konfiguréiert: !!(s?.host && s?.fromAddress),
  });
});

// PUT /api/admin/smtp — save settings (password write-only)
router.put("/smtp", async (req, res) => {
  const body = SmtpSchema.parse(req.body);
  const existing = await loadSmtpRow();
  const values = {
    host: body.host,
    port: body.port,
    username: body.username,
    fromAddress: body.fromAddress,
    verschluesselung: body.verschluesselung,
    ignoreTlsErrors: body.ignoreTlsErrors,
    portalUrl: body.portalUrl,
    ...(body.passwort ? { passwort: body.passwort } : {}),
  };
  if (existing) {
    await db.update(smtpSettingsTable).set(values).where(eq(smtpSettingsTable.id, existing.id));
  } else {
    await db.insert(smtpSettingsTable).values({ passwort: "", ...values });
  }
  return res.json({ success: true });
});

// POST /api/admin/smtp/test — send a test email with the stored settings
router.post("/smtp/test", async (req, res) => {
  const { empfaenger } = z.object({ empfaenger: z.string().email() }).parse(req.body);
  const s = await loadSmtpRow();
  if (!s || !s.host || !s.fromAddress) {
    return res.status(400).json({ error: "SMTP net konfiguréiert (Host a Vun-Adress néideg)" });
  }
  try {
    const transport = buildTransport(s);
    await transport.sendMail({
      from: s.fromAddress,
      to: empfaenger,
      subject: "TrapMaster SMTP Test",
      text: "Dës Test-Email confirméiert datt d'SMTP-Astellunge fonctionéieren.",
    });
    return res.json({ success: true });
  } catch (err) {
    return res.status(502).json({ error: err instanceof Error ? err.message : String(err) });
  }
});

export default router;
