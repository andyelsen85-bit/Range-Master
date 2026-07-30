import { Router } from "express";
import bcrypt from "bcryptjs";
import { db, spielerTable, spieleTable, spielTeilnahmenTable, ergebnisseTable, kreditEventsTable, spielerUpdatesTable } from "@workspace/db";
import { eq, inArray, sql } from "drizzle-orm";
import { requireApiKey } from "./auth";
import { z } from "zod";
import { getSmtpSettings, sendMail, generatePassword, invitationEmail, resetEmail } from "../lib/mailer";

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
    .select({
      id: spielerTable.id,
      name: spielerTable.name,
      mitgliedNr: spielerTable.mitgliedNr,
      email: spielerTable.email,
      portalAktiv: spielerTable.portalAktiv,
    })
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

// ─── Day credits ──────────────────────────────────────────────────────────────

const KreditEventSchema = z.object({
  externalId: z.string().min(8),
  spielerId: z.number().int().positive(),
  datum: z.string().regex(/^\d{4}-\d{2}-\d{2}$/),
  typ: z.enum(["GRANT", "USE"]),
  anzahl: z.number().int().min(-100).max(100).refine(n => n !== 0, "anzahl darf net 0 sinn"),
});

// GET /api/sync/kredite?datum=YYYY-MM-DD — aggregated per-player credits for one day
router.get("/kredite", requireApiKey, async (req, res) => {
  const datum = z.string().regex(/^\d{4}-\d{2}-\d{2}$/).catch(new Date().toISOString().slice(0, 10)).parse(req.query.datum);
  const rows = await db
    .select({
      spielerId: kreditEventsTable.spielerId,
      gewaehrt: sql<number>`COALESCE(SUM(CASE WHEN ${kreditEventsTable.typ} = 'GRANT' THEN ${kreditEventsTable.anzahl} ELSE 0 END), 0)::int`,
      verbraucht: sql<number>`COALESCE(SUM(CASE WHEN ${kreditEventsTable.typ} = 'USE' THEN ${kreditEventsTable.anzahl} ELSE 0 END), 0)::int`,
    })
    .from(kreditEventsTable)
    .where(eq(kreditEventsTable.datum, datum))
    .groupBy(kreditEventsTable.spielerId);
  return res.json({ datum, kredite: rows });
});

// POST /api/sync/kredite — idempotent push of grant/use events from the terminal
router.post("/kredite", requireApiKey, async (req, res) => {
  const body = z.object({ events: z.array(KreditEventSchema) }).parse(req.body);

  let synced = 0;
  if (body.events.length) {
    // Reject events for unknown players explicitly (avoid FK 500s)
    const ids = [...new Set(body.events.map((e) => e.spielerId))];
    const known = await db.select({ id: spielerTable.id }).from(spielerTable).where(inArray(spielerTable.id, ids));
    const knownIds = new Set(known.map((k) => k.id));
    const unknown = ids.filter((id) => !knownIds.has(id));
    if (unknown.length) {
      return res.status(400).json({ error: `Onbekannte Spiller-IDs: ${unknown.join(", ")}` });
    }

    const inserted = await db
      .insert(kreditEventsTable)
      .values(body.events)
      .onConflictDoNothing({ target: kreditEventsTable.externalId })
      .returning({ id: kreditEventsTable.id });
    synced = inserted.length;
  }

  return res.json({ synced, skipped: body.events.length - synced });
});

// ─── Player updates from the terminal ────────────────────────────────────────

const SpielerUpdateSchema = z.object({
  externalId: z.string().min(8),
  spielerId: z.number().int().positive(),
  typ: z.enum(["UPDATE", "PASSWORT_RESET"]),
  // Only for typ=UPDATE:
  name: z.string().min(1).optional(),
  email: z.string().email().nullable().optional(),
  portalAktiv: z.boolean().optional(),
});

type EmailJob = { to: string; subject: string; text: string };

/** Try to send a queued email for one spieler_update row; updates emailStatus */
async function trySendUpdateEmail(updateId: number, job: EmailJob): Promise<"SENT" | "FAILED"> {
  try {
    await sendMail(job.to, job.subject, job.text);
    await db.update(spielerUpdatesTable)
      .set({ emailStatus: "SENT", emailError: null })
      .where(eq(spielerUpdatesTable.id, updateId));
    return "SENT";
  } catch (err) {
    await db.update(spielerUpdatesTable)
      .set({ emailStatus: "FAILED", emailError: err instanceof Error ? err.message : String(err) })
      .where(eq(spielerUpdatesTable.id, updateId));
    return "FAILED";
  }
}

/** Build the email job for a pending update row (regenerates the password) */
async function buildEmailJobForUpdate(
  u: { id: number; spielerId: number; typ: "UPDATE" | "PASSWORT_RESET" },
): Promise<EmailJob | null> {
  const [s] = await db.select().from(spielerTable).where(eq(spielerTable.id, u.spielerId)).limit(1);
  if (!s?.email) return null;
  const smtp = await getSmtpSettings();
  const portalUrl = smtp?.portalUrl ?? "";
  const passwort = generatePassword();
  const passwortHash = await bcrypt.hash(passwort, 10);
  await db.update(spielerTable)
    .set(u.typ === "UPDATE" ? { passwortHash, eingeladenAt: new Date() } : { passwortHash })
    .where(eq(spielerTable.id, u.spielerId));
  const mail = u.typ === "UPDATE"
    ? invitationEmail(s.name, s.email, passwort, portalUrl)
    : resetEmail(s.name, passwort, portalUrl);
  return { to: s.email, ...mail };
}

// POST /api/sync/spieler-updates — idempotent push of player edits & password resets
router.post("/spieler-updates", requireApiKey, async (req, res) => {
  const body = z.object({ updates: z.array(SpielerUpdateSchema) }).parse(req.body);
  const results: Array<{ externalId: string; status: "applied" | "skipped" | "error"; emailStatus: string; error?: string }> = [];

  for (const u of body.updates) {
    // Idempotency: skip already-processed change events
    const [existing] = await db
      .select({ id: spielerUpdatesTable.id, emailStatus: spielerUpdatesTable.emailStatus })
      .from(spielerUpdatesTable)
      .where(eq(spielerUpdatesTable.externalId, u.externalId))
      .limit(1);
    if (existing) {
      results.push({ externalId: u.externalId, status: "skipped", emailStatus: existing.emailStatus });
      continue;
    }

    const [spieler] = await db.select().from(spielerTable).where(eq(spielerTable.id, u.spielerId)).limit(1);
    if (!spieler) {
      results.push({ externalId: u.externalId, status: "error", emailStatus: "NONE", error: `Spiller ${u.spielerId} net fonnt` });
      continue;
    }

    let needsEmail = false;

    if (u.typ === "UPDATE") {
      // Reject email collisions with other players explicitly
      if (u.email) {
        const [clash] = await db.select({ id: spielerTable.id }).from(spielerTable)
          .where(sql`${spielerTable.email} = ${u.email} AND ${spielerTable.id} != ${u.spielerId}`).limit(1);
        if (clash) {
          results.push({ externalId: u.externalId, status: "error", emailStatus: "NONE", error: `Email ${u.email} gëtt schonn benotzt` });
          continue;
        }
      }
      const patch: Partial<typeof spielerTable.$inferInsert> = {};
      if (u.name !== undefined) patch.name = u.name;
      if (u.email !== undefined) patch.email = u.email;
      if (u.portalAktiv !== undefined) patch.portalAktiv = u.portalAktiv;
      const nowActivating = u.portalAktiv === true && !spieler.portalAktiv;
      const effectiveEmail = u.email !== undefined ? u.email : spieler.email;
      await db.update(spielerTable).set(patch).where(eq(spielerTable.id, u.spielerId));
      // Invitation only on fresh activation with a known email address
      needsEmail = nowActivating && !!effectiveEmail;
    } else {
      // PASSWORT_RESET always triggers a mail (if the player has an email)
      needsEmail = !!spieler.email;
      if (u.typ === "PASSWORT_RESET" && !spieler.email) {
        // Reset without email: still set a new password is pointless — flag as error
        results.push({ externalId: u.externalId, status: "error", emailStatus: "NONE", error: "Keng Email-Adress beim Spiller" });
        continue;
      }
    }

    const [row] = await db.insert(spielerUpdatesTable).values({
      externalId: u.externalId,
      spielerId: u.spielerId,
      typ: u.typ,
      emailStatus: needsEmail ? "PENDING" : "NONE",
    }).returning({ id: spielerUpdatesTable.id });

    let emailStatus: string = needsEmail ? "PENDING" : "NONE";
    if (needsEmail) {
      const job = await buildEmailJobForUpdate({ id: row.id, spielerId: u.spielerId, typ: u.typ });
      if (job) emailStatus = await trySendUpdateEmail(row.id, job);
    }

    results.push({ externalId: u.externalId, status: "applied", emailStatus });
  }

  return res.json({ results });
});

// GET /api/sync/spieler-updates/status?ids=a,b,c — per-change status; retries failed emails
router.get("/spieler-updates/status", requireApiKey, async (req, res) => {
  const ids = String(req.query.ids ?? "").split(",").map((s) => s.trim()).filter(Boolean).slice(0, 100);
  if (!ids.length) return res.json({ updates: [] });

  const rows = await db.select().from(spielerUpdatesTable).where(inArray(spielerUpdatesTable.externalId, ids));

  // Retry emails that are still pending or previously failed
  for (const r of rows) {
    if (r.emailStatus === "PENDING" || r.emailStatus === "FAILED") {
      const job = await buildEmailJobForUpdate({ id: r.id, spielerId: r.spielerId, typ: r.typ });
      if (job) r.emailStatus = await trySendUpdateEmail(r.id, job);
    }
  }

  return res.json({
    updates: rows.map((r) => ({
      externalId: r.externalId,
      typ: r.typ,
      emailStatus: r.emailStatus,
      emailError: r.emailError,
    })),
  });
});

export default router;
