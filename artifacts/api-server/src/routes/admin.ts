import { Router } from "express";
import bcrypt from "bcryptjs";
import { randomBytes } from "crypto";
import { db, spielerTable, spieleTable, spielTeilnahmenTable, ergebnisseTable, apiKeysTable, kreditEventsTable, spielerUpdatesTable, smtpSettingsTable, productsTable, productPriceRevisionsTable, saleEventsTable, billPaymentsTable, terminalConfigBackupsTable, terminalRestoreAuthorizationsTable } from "@workspace/db";
import { buildTransport } from "../lib/mailer";
import { and, desc, eq, lte, isNull, sql } from "drizzle-orm";
import { authenticate, requireAdmin } from "./auth";
import { z } from "zod";
import { catalogue, daySalesReport, ensureSystemProducts } from "../lib/products";
import { createRestoreCode, hashRestoreCode } from "../lib/terminal-config";
import { activityDays, billSettlementStatus, dayBillSummary, isSettlementRedundant, lockBillSettlement, periodBillSummary } from "../lib/bills";

const router = Router();

// All admin routes require auth + admin role
router.use(authenticate, requireAdmin);

// ─── Terminal configuration backups ───────────────────────────────────────────

router.get("/terminal-config/backups", async (_req, res) => {
  const rows = await db.select({
    id: terminalConfigBackupsTable.id,
    terminalId: terminalConfigBackupsTable.terminalId,
    schemaVersion: terminalConfigBackupsTable.schemaVersion,
    firmwareVersion: terminalConfigBackupsTable.firmwareVersion,
    checksum: terminalConfigBackupsTable.checksum,
    createdAt: terminalConfigBackupsTable.createdAt,
    updatedAt: terminalConfigBackupsTable.updatedAt,
    lastRestoredAt: terminalConfigBackupsTable.lastRestoredAt,
    revokedAt: terminalConfigBackupsTable.revokedAt,
  }).from(terminalConfigBackupsTable).orderBy(desc(terminalConfigBackupsTable.updatedAt));
  const terminalKeys = await db.select({ id: apiKeysTable.id, name: apiKeysTable.name })
    .from(apiKeysTable)
    .where(and(eq(apiKeysTable.type, "TERMINAL"), eq(apiKeysTable.active, true)))
    .orderBy(apiKeysTable.name);
  return res.json({ backups: rows, terminalKeys });
});

router.post("/terminal-config/backups/:id/authorize-restore", async (req, res) => {
  const id = z.coerce.number().int().positive().safeParse(req.params.id);
  if (!id.success) return res.status(400).json({ error: "Ungültige Backup-ID" });
  const target = z.object({
    targetTerminalId: z.string().trim().min(1).max(120).regex(/^[A-Za-z0-9._:-]+$/),
    targetApiKeyId: z.number().int().positive(),
  }).safeParse(req.body);
  if (!target.success) return res.status(400).json({ error: "Ersatzterminal an API-Schlëssel sinn obligatoresch" });
  const [targetKey] = await db.select({ id: apiKeysTable.id }).from(apiKeysTable).where(and(
    eq(apiKeysTable.id, target.data.targetApiKeyId),
    eq(apiKeysTable.type, "TERMINAL"),
    eq(apiKeysTable.active, true),
  )).limit(1);
  if (!targetKey) return res.status(400).json({ error: "Terminal API-Schlëssel net fonnt oder inaktiv" });

  const code = createRestoreCode();
  const expiresAt = new Date(Date.now() + 15 * 60 * 1000);
  const authorized = await db.transaction(async (tx) => {
    const [backup] = await tx.update(terminalConfigBackupsTable)
      .set({ updatedAt: terminalConfigBackupsTable.updatedAt })
      .where(and(eq(terminalConfigBackupsTable.id, id.data), isNull(terminalConfigBackupsTable.revokedAt)))
      .returning({ id: terminalConfigBackupsTable.id, terminalId: terminalConfigBackupsTable.terminalId });
    if (!backup || backup.terminalId === target.data.targetTerminalId) return false;
    await tx.update(terminalRestoreAuthorizationsTable)
      .set({ revokedAt: new Date() })
      .where(and(
        eq(terminalRestoreAuthorizationsTable.backupId, id.data),
        isNull(terminalRestoreAuthorizationsTable.usedAt),
        isNull(terminalRestoreAuthorizationsTable.revokedAt),
      ));
    await tx.insert(terminalRestoreAuthorizationsTable).values({
      backupId: id.data,
      targetTerminalId: target.data.targetTerminalId,
      targetApiKeyId: target.data.targetApiKeyId,
      codeHash: hashRestoreCode(code),
      expiresAt,
      createdBy: (req as any).user.id,
    });
    return true;
  });
  if (!authorized) return res.status(400).json({ error: "Backup net disponibel oder Ersatzterminal ass mam Quellterminal identesch" });
  return res.json({ code, expiresAt, warning: "De Code gëtt nëmmen eemol gewisen." });
});

router.post("/terminal-config/backups/:id/revoke", async (req, res) => {
  const id = z.coerce.number().int().positive().safeParse(req.params.id);
  if (!id.success) return res.status(400).json({ error: "Ungültige Backup-ID" });
  const backup = await db.transaction(async (tx) => {
    const [updated] = await tx.update(terminalConfigBackupsTable)
      .set({ revokedAt: new Date() })
      .where(and(eq(terminalConfigBackupsTable.id, id.data), isNull(terminalConfigBackupsTable.revokedAt)))
      .returning({ id: terminalConfigBackupsTable.id });
    if (!updated) return null;
    await tx.update(terminalRestoreAuthorizationsTable)
      .set({ revokedAt: new Date() })
      .where(and(eq(terminalRestoreAuthorizationsTable.backupId, id.data), isNull(terminalRestoreAuthorizationsTable.usedAt), isNull(terminalRestoreAuthorizationsTable.revokedAt)));
    return updated;
  });
  if (!backup) return res.status(404).json({ error: "Backup net fonnt oder schonn revokéiert" });
  return res.json({ success: true });
});

// GET /api/admin/spieler — all players with game stats
router.get("/spieler", async (_req, res) => {
  const spieler = await db
    .select({
      id: spielerTable.id,
      name: spielerTable.name,
      email: spielerTable.email,
      mitgliedNr: spielerTable.mitgliedNr,
      aktiv: spielerTable.aktiv,
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

// DELETE /api/admin/spieler/:id — deactivate a player while retaining history
router.delete("/spieler/:id", async (req, res): Promise<void> => {
  const parsedId = z.coerce.number().int().positive().safeParse(req.params.id);
  if (!parsedId.success) {
    res.status(400).json({ error: "Ungültige Spieler-ID" });
    return;
  }
  const id = parsedId.data;
  const requestingUserId = (req as any).user.id;

  if (id === requestingUserId) {
    res.status(400).json({ error: "Kann den eegene Benotzer net läschen" });
    return;
  }

  const deactivated = await db.transaction(async (tx) => {
    const [player] = await tx
      .select({ id: spielerTable.id, isAdmin: spielerTable.isAdmin, aktiv: spielerTable.aktiv })
      .from(spielerTable)
      .where(eq(spielerTable.id, id))
      .for("update")
      .limit(1);
    if (!player) return "not_found" as const;
    if (player.isAdmin) return "admin" as const;
    if (!player.aktiv) return "already_inactive" as const;

    await tx.update(spielerTable)
      .set({ aktiv: false })
      .where(eq(spielerTable.id, id));
    return "deactivated" as const;
  });
  if (deactivated === "not_found") {
    res.status(404).json({ error: "Spieler nicht gefunden" });
    return;
  }
  if (deactivated === "admin") {
    res.status(400).json({ error: "Administratorkonten können nicht deaktiviert werden" });
    return;
  }
  res.json({ deactivated: deactivated === "deactivated", aktiv: false });
});

// POST /api/admin/spieler/:id/reactivate — make a retained player selectable again
router.post("/spieler/:id/reactivate", async (req, res): Promise<void> => {
  const parsedId = z.coerce.number().int().positive().safeParse(req.params.id);
  if (!parsedId.success) {
    res.status(400).json({ error: "Ungültige Spieler-ID" });
    return;
  }
  const [updated] = await db.update(spielerTable)
    .set({ aktiv: true })
    .where(eq(spielerTable.id, parsedId.data))
    .returning({ id: spielerTable.id, aktiv: spielerTable.aktiv });
  if (!updated) {
    res.status(404).json({ error: "Spieler nicht gefunden" });
    return;
  }
  res.json(updated);
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

type PurgeCounts = {
  players: number;
  games: number;
  participations: number;
  results: number;
  credits: number;
  sales: number;
  billPayments: number;
  playerUpdates: number;
};

async function countRows(tx: any, query: any): Promise<number> {
  const result = await tx.execute(query);
  return Number(result.rows[0]?.count ?? 0);
}

// POST /api/admin/purge — explicitly confirmed day or global operational reset
router.post("/purge", async (req, res): Promise<void> => {
  const parsed = z.discriminatedUnion("mode", [
    z.object({
      mode: z.literal("day"),
      datum: z.string().regex(/^\d{4}-\d{2}-\d{2}$/),
      confirmation: z.literal("PURGE_DAY"),
    }),
    z.object({
      mode: z.literal("all"),
      confirmation: z.literal("PURGE_ALL"),
    }),
  ]).safeParse(req.body);
  if (!parsed.success) {
    res.status(400).json({ error: "Ungültige Löschanfrage oder Bestätigung" });
    return;
  }

  const counts = await db.transaction(async (tx) => {
    await tx.execute(sql`
      LOCK TABLE
        ${billPaymentsTable}, ${saleEventsTable}, ${kreditEventsTable},
        ${spielerUpdatesTable}, ${ergebnisseTable}, ${spielTeilnahmenTable},
        ${spieleTable}, ${spielerTable}
      IN ACCESS EXCLUSIVE MODE
    `);
    const result: PurgeCounts = {
      players: 0,
      games: 0,
      participations: 0,
      results: 0,
      credits: 0,
      sales: 0,
      billPayments: 0,
      playerUpdates: 0,
    };

    if (parsed.data.mode === "day") {
      const datum = parsed.data.datum;
      const gameDay = sql`(${spieleTable.datum} AT TIME ZONE 'UTC')::date = ${datum}::date`;
      result.games = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${spieleTable} WHERE ${gameDay}`);
      result.participations = await countRows(tx, sql`
        SELECT COUNT(*)::int AS count
        FROM ${spielTeilnahmenTable} t
        INNER JOIN ${spieleTable} s ON s.id = t.spiel_id
        WHERE (s.datum AT TIME ZONE 'UTC')::date = ${datum}::date
      `);
      result.results = await countRows(tx, sql`
        SELECT COUNT(*)::int AS count
        FROM ${ergebnisseTable} e
        INNER JOIN ${spieleTable} s ON s.id = e.spiel_id
        WHERE (s.datum AT TIME ZONE 'UTC')::date = ${datum}::date
      `);
      result.credits = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${kreditEventsTable} WHERE ${kreditEventsTable.datum} = ${datum}::date`);
      result.sales = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${saleEventsTable} WHERE ${saleEventsTable.datum} = ${datum}::date`);
      result.billPayments = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${billPaymentsTable} WHERE ${billPaymentsTable.datum} = ${datum}::date`);

      await tx.delete(billPaymentsTable).where(eq(billPaymentsTable.datum, datum));
      await tx.delete(saleEventsTable).where(eq(saleEventsTable.datum, datum));
      await tx.delete(kreditEventsTable).where(eq(kreditEventsTable.datum, datum));
      await tx.delete(spieleTable).where(gameDay);
      return result;
    }

    result.players = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${spielerTable} WHERE ${spielerTable.isAdmin} = false`);
    result.games = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${spieleTable}`);
    result.participations = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${spielTeilnahmenTable}`);
    result.results = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${ergebnisseTable}`);
    result.credits = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${kreditEventsTable}`);
    result.sales = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${saleEventsTable}`);
    result.billPayments = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${billPaymentsTable}`);
    result.playerUpdates = await countRows(tx, sql`SELECT COUNT(*)::int AS count FROM ${spielerUpdatesTable}`);

    await tx.delete(billPaymentsTable);
    await tx.delete(saleEventsTable);
    await tx.delete(kreditEventsTable);
    await tx.delete(spielerUpdatesTable);
    await tx.delete(spieleTable);
    await tx.delete(spielerTable).where(eq(spielerTable.isAdmin, false));
    return result;
  });

  res.json({
    mode: parsed.data.mode,
    datum: parsed.data.mode === "day" ? parsed.data.datum : undefined,
    counts,
  });
});

// GET /api/admin/kredite/joer?year=YYYY — annual credit summary
router.get("/kredite/joer", async (req, res) => {
  const year = z.coerce.number().int().min(2020).max(2100).catch(new Date().getFullYear()).parse(req.query.year);
  const [totals] = (await db.execute(sql`
    SELECT
      COALESCE(SUM(CASE WHEN typ = 'GRANT' THEN anzahl ELSE 0 END), 0)::int  AS total_gewaehrt,
      COALESCE(SUM(CASE WHEN typ = 'USE'   THEN anzahl ELSE 0 END), 0)::int  AS total_verbraucht,
      COUNT(DISTINCT datum)::int                                               AS anzahl_dagen,
      COUNT(DISTINCT spieler_id)::int                                          AS anzahl_spiller
    FROM kredit_events
    WHERE EXTRACT(YEAR FROM datum::timestamp) = ${year}
  `)).rows as any[];
  const byMonth = (await db.execute(sql`
    SELECT
      EXTRACT(MONTH FROM datum::timestamp)::int                                AS monat,
      COALESCE(SUM(CASE WHEN typ = 'GRANT' THEN anzahl ELSE 0 END), 0)::int  AS gewaehrt,
      COALESCE(SUM(CASE WHEN typ = 'USE'   THEN anzahl ELSE 0 END), 0)::int  AS verbraucht,
      COUNT(DISTINCT datum)::int                                               AS dagen
    FROM kredit_events
    WHERE EXTRACT(YEAR FROM datum::timestamp) = ${year}
    GROUP BY monat ORDER BY monat
  `)).rows as any[];
  return res.json({
    year,
    totalGewaehrt: Number(totals?.total_gewaehrt ?? 0),
    totalVerbraucht: Number(totals?.total_verbraucht ?? 0),
    anzahlDagen: Number(totals?.anzahl_dagen ?? 0),
    anzahlSpiller: Number(totals?.anzahl_spiller ?? 0),
    byMonth: byMonth.map(r => ({
      monat: Number(r.monat),
      gewaehrt: Number(r.gewaehrt),
      verbraucht: Number(r.verbraucht),
      dagen: Number(r.dagen),
    })),
  });
});

// GET /api/admin/kredite/tauben?year=YYYY — clay counts per machine based on ergebnisse
router.get("/kredite/tauben", async (req, res) => {
  const year = z.coerce.number().int().min(2020).max(2100).catch(new Date().getFullYear()).parse(req.query.year);
  const rows = (await db.execute(sql`
    SELECT e.maschine, COUNT(*)::int AS anzahl
    FROM ergebnisse e
    JOIN spiele s ON e.spiel_id = s.id
    WHERE EXTRACT(YEAR FROM s.datum) = ${year}
    GROUP BY e.maschine
    ORDER BY e.maschine
  `)).rows as any[];
  const byMaschine: Record<string, number> = {};
  let total = 0;
  for (const r of rows) {
    byMaschine[r.maschine] = Number(r.anzahl);
    total += Number(r.anzahl);
  }
  return res.json({ year, byMaschine, total });
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

// ─── Product catalogue and sales ─────────────────────────────────────────────

const productId = (raw: string | string[]) => z.coerce.number().int().positive().safeParse(Array.isArray(raw) ? raw[0] : raw);
const day = z.string().regex(/^\d{4}-\d{2}-\d{2}$/).refine((value) => {
  const parsed = new Date(`${value}T00:00:00Z`);
  return Number.isFinite(parsed.getTime()) && parsed.toISOString().slice(0, 10) === value;
}, "Date must be a valid calendar day");
const dateRange = z.object({ from: day, to: day }).superRefine((value, ctx) => {
  const fromTime = Date.parse(`${value.from}T00:00:00Z`);
  const toTime = Date.parse(`${value.to}T00:00:00Z`);
  if (!Number.isFinite(fromTime) || !Number.isFinite(toTime) || fromTime > toTime) {
    ctx.addIssue({ code: z.ZodIssueCode.custom, message: "from must be before or equal to to" });
    return;
  }
  if ((toTime - fromTime) / 86_400_000 > 366) {
    ctx.addIssue({ code: z.ZodIssueCode.custom, message: "The selected period cannot exceed 367 days" });
  }
});
const adminAdjustment = z.object({
  spielerId: z.number().int().positive(),
  datum: day,
  delta: z.union([z.literal(1), z.literal(-1)]),
  externalId: z.string().trim().min(1).max(200),
});

/** Shared lock names are also used by terminal uploads to serialize ledger totals. */
async function lockLedger(tx: any, scope: string): Promise<void> {
  await tx.execute(sql`SELECT pg_advisory_xact_lock(hashtextextended(${scope}, 0))`);
}

async function creditAggregate(tx: Parameters<Parameters<typeof db.transaction>[0]>[0], spielerId: number, datum: string) {
  const [totals] = await tx.select({
    granted: sql<number>`COALESCE(SUM(CASE WHEN ${kreditEventsTable.typ} = 'GRANT' THEN ${kreditEventsTable.anzahl} ELSE 0 END), 0)::int`,
    used: sql<number>`COALESCE(SUM(CASE WHEN ${kreditEventsTable.typ} = 'USE' THEN ${kreditEventsTable.anzahl} ELSE 0 END), 0)::int`,
  }).from(kreditEventsTable).where(and(
    eq(kreditEventsTable.spielerId, spielerId),
    eq(kreditEventsTable.datum, datum),
  ));
  const gewaehrt = Number(totals?.granted ?? 0);
  const verbraucht = Number(totals?.used ?? 0);
  return { spielerId, datum, gewaehrt, verbraucht, available: gewaehrt - verbraucht };
}

async function ammoAggregate(tx: Parameters<Parameters<typeof db.transaction>[0]>[0], spielerId: number, datum: string, productId: number) {
  const [totals] = await tx.select({
    quantity: sql<number>`COALESCE(SUM(${saleEventsTable.quantity}), 0)::int`,
  }).from(saleEventsTable).where(and(
    eq(saleEventsTable.spielerId, spielerId),
    eq(saleEventsTable.datum, datum),
    eq(saleEventsTable.productId, productId),
  ));
  return { spielerId, datum, productId, quantity: Number(totals?.quantity ?? 0) };
}

// POST /api/admin/kredite/adjust — append a one-credit grant or correction.
router.post("/kredite/adjust", async (req, res): Promise<void> => {
  const parsed = adminAdjustment.safeParse(req.body);
  if (!parsed.success) { res.status(400).json({ error: "spielerId, datum, delta (+1 or -1), and externalId are required" }); return; }
  const event = parsed.data;
  const externalId = event.externalId;
  const result = await db.transaction(async (tx) => {
    await lockLedger(tx, `credit:${event.spielerId}:${event.datum}`);
    const [prior] = await tx.select().from(kreditEventsTable).where(eq(kreditEventsTable.externalId, externalId)).limit(1);
    if (prior) {
      const matches = prior.spielerId === event.spielerId && prior.datum === event.datum && prior.typ === "GRANT" && prior.anzahl === event.delta;
      return matches
        ? { status: "skipped" as const, aggregate: await creditAggregate(tx, event.spielerId, event.datum) }
        : { status: "conflict" as const };
    }
    const [player] = await tx.select({ id: spielerTable.id }).from(spielerTable).where(eq(spielerTable.id, event.spielerId)).limit(1);
    if (!player) return { status: "not_found" as const };
    const before = await creditAggregate(tx, event.spielerId, event.datum);
    if (before.available + event.delta < 0) return { status: "insufficient" as const, aggregate: before };
    const inserted = await tx.insert(kreditEventsTable).values({
      externalId, spielerId: event.spielerId, datum: event.datum,
      // A negative GRANT is an append-only correction; USE remains reserved for game starts.
      typ: "GRANT", anzahl: event.delta,
    }).onConflictDoNothing({ target: kreditEventsTable.externalId }).returning({ id: kreditEventsTable.id });
    if (!inserted.length) return { status: "conflict" as const };
    return { status: "accepted" as const, aggregate: await creditAggregate(tx, event.spielerId, event.datum) };
  });
  if (result.status === "not_found") { res.status(404).json({ error: "Player not found" }); return; }
  if (result.status === "insufficient") { res.status(409).json({ error: "Credit balance cannot become negative", credit: result.aggregate }); return; }
  if (result.status === "conflict") { res.status(409).json({ error: "externalId belongs to a different credit adjustment" }); return; }
  res.json({ externalId, status: result.status, credit: result.aggregate });
});

// POST /api/admin/ammo/adjust — append a one-box sale or reversal for ammunition.
router.post("/ammo/adjust", async (req, res): Promise<void> => {
  const parsed = adminAdjustment.extend({ productId: z.number().int().positive() }).safeParse(req.body);
  if (!parsed.success) { res.status(400).json({ error: "spielerId, datum, productId, delta (+1 or -1), and externalId are required" }); return; }
  const event = parsed.data;
  const externalId = event.externalId;
  const result = await db.transaction(async (tx) => {
    await lockLedger(tx, `ammo:${event.spielerId}:${event.datum}:${event.productId}`);
    const [prior] = await tx.select().from(saleEventsTable).where(eq(saleEventsTable.externalId, externalId)).limit(1);
    if (prior) {
      const matches = prior.spielerId === event.spielerId && prior.datum === event.datum &&
        prior.productId === event.productId && prior.quantity === event.delta;
      return matches
        ? { status: "skipped" as const, aggregate: await ammoAggregate(tx, event.spielerId, event.datum, event.productId) }
        : { status: "conflict" as const };
    }
    const [player] = await tx.select({ id: spielerTable.id }).from(spielerTable).where(eq(spielerTable.id, event.spielerId)).limit(1);
    if (!player) return { status: "not_found" as const };
    const [product] = await tx.select().from(productsTable).where(and(
      eq(productsTable.id, event.productId),
      eq(productsTable.isSystem, true),
      eq(productsTable.active, true),
      sql`${productsTable.code} IN ('AMMO_CAL12', 'AMMO_CAL20')`,
    )).limit(1);
    if (!product) return { status: "invalid_product" as const };
    const before = await ammoAggregate(tx, event.spielerId, event.datum, product.id);
    if (before.quantity + event.delta < 0) return { status: "insufficient" as const, aggregate: before };
    const saleSnapshot = event.delta === 1
      ? (() => tx.select({
        id: productPriceRevisionsTable.id,
        unitPriceCents: productPriceRevisionsTable.unitPriceCents,
      }).from(productPriceRevisionsTable).where(and(
        eq(productPriceRevisionsTable.productId, product.id),
        lte(productPriceRevisionsTable.effectiveFrom, new Date()),
      )).orderBy(desc(productPriceRevisionsTable.effectiveFrom), desc(productPriceRevisionsTable.id)).limit(1)
        .then(([price]) => price ? { ...price, productName: product.name, productCategory: product.category } : null))()
      : tx.select({
        id: saleEventsTable.priceRevisionId,
        productName: saleEventsTable.productName,
        productCategory: saleEventsTable.productCategory,
        unitPriceCents: saleEventsTable.unitPriceCents,
      }).from(saleEventsTable).where(and(
        eq(saleEventsTable.spielerId, event.spielerId),
        eq(saleEventsTable.datum, event.datum),
        eq(saleEventsTable.productId, product.id),
      )).groupBy(
        saleEventsTable.priceRevisionId, saleEventsTable.productName,
        saleEventsTable.productCategory, saleEventsTable.unitPriceCents,
      ).having(sql`SUM(${saleEventsTable.quantity}) > 0`)
        // LIFO: choose the lot whose original positive sale is newest; a later
        // correction must not make an older lot appear newer.
        .orderBy(desc(sql`MAX(CASE WHEN ${saleEventsTable.quantity} > 0 THEN ${saleEventsTable.id} ELSE 0 END)`)).limit(1)
        .then(([price]) => price ?? null);
    const price = await saleSnapshot;
    if (!price) return { status: "missing_price" as const };
    const inserted = await tx.insert(saleEventsTable).values({
      externalId, spielerId: event.spielerId, datum: event.datum, productId: product.id,
      priceRevisionId: price.id, productName: price.productName, productCategory: price.productCategory,
      unitPriceCents: price.unitPriceCents, quantity: event.delta,
    }).onConflictDoNothing({ target: saleEventsTable.externalId }).returning({ id: saleEventsTable.id });
    if (!inserted.length) return { status: "conflict" as const };
    return { status: "accepted" as const, aggregate: await ammoAggregate(tx, event.spielerId, event.datum, product.id) };
  });
  if (result.status === "not_found") { res.status(404).json({ error: "Player not found" }); return; }
  if (result.status === "invalid_product") { res.status(400).json({ error: "productId must be an active AMMO_CAL12 or AMMO_CAL20 system product" }); return; }
  if (result.status === "missing_price") { res.status(409).json({ error: "A current price revision is required" }); return; }
  if (result.status === "insufficient") { res.status(409).json({ error: "Ammunition quantity cannot become negative", ammo: result.aggregate }); return; }
  if (result.status === "conflict") { res.status(409).json({ error: "externalId belongs to a different ammunition adjustment" }); return; }
  res.json({ externalId, status: result.status, ammo: result.aggregate });
});

router.get("/products", async (_req, res): Promise<void> => {
  res.json({ products: await catalogue() });
});

router.post("/products", async (req, res): Promise<void> => {
  const parsed = z.object({
    name: z.string().trim().min(1).max(200),
    category: z.enum(["FOOD", "DRINK"]),
    active: z.boolean().default(true),
    unitPriceCents: z.number().int().min(0),
  }).safeParse(req.body);
  if (!parsed.success) { res.status(400).json({ error: parsed.error.message }); return; }
  const { unitPriceCents, ...values } = parsed.data;
  const [product] = await db.insert(productsTable).values(values).returning();
  await db.insert(productPriceRevisionsTable).values({ productId: product.id, unitPriceCents });
  res.status(201).json(product);
});

router.patch("/products/:id", async (req, res): Promise<void> => {
  const id = productId(req.params.id);
  const parsed = z.object({
    name: z.string().trim().min(1).max(200).optional(),
    category: z.enum(["FOOD", "DRINK"]).optional(),
    active: z.boolean().optional(),
  }).refine(v => v.name !== undefined || v.category !== undefined || v.active !== undefined).safeParse(req.body);
  if (!id.success) { res.status(400).json({ error: id.error.message }); return; }
  if (!parsed.success) { res.status(400).json({ error: parsed.error.message }); return; }
  const [existing] = await db.select().from(productsTable).where(eq(productsTable.id, id.data)).limit(1);
  if (!existing) { res.status(404).json({ error: "Product not found" }); return; }
  // System identity is protected; only presentation and availability can change.
  if (existing.isSystem && parsed.data.category !== undefined) {
    res.status(400).json({ error: "System product category cannot be changed" });
    return;
  }
  const [product] = await db.update(productsTable).set(parsed.data).where(eq(productsTable.id, id.data)).returning();
  res.json(product);
});

router.delete("/products/:id", async (req, res): Promise<void> => {
  const id = productId(req.params.id);
  if (!id.success) { res.status(400).json({ error: id.error.message }); return; }
  const [product] = await db.select().from(productsTable).where(eq(productsTable.id, id.data)).limit(1);
  if (!product) { res.status(404).json({ error: "Product not found" }); return; }
  if (product.isSystem) { res.status(400).json({ error: "System products cannot be deleted" }); return; }
  const [sale] = await db.select({ id: saleEventsTable.id }).from(saleEventsTable).where(eq(saleEventsTable.productId, id.data)).limit(1);
  if (sale) { res.status(400).json({ error: "Products with sales history cannot be deleted" }); return; }
  await db.transaction(async (tx) => {
    await tx.delete(productPriceRevisionsTable).where(eq(productPriceRevisionsTable.productId, id.data));
    await tx.delete(productsTable).where(eq(productsTable.id, id.data));
  });
  res.sendStatus(204);
});

router.post("/products/:id/prices", async (req, res): Promise<void> => {
  const id = productId(req.params.id);
  const parsed = z.object({ unitPriceCents: z.number().int().min(0), effectiveFrom: z.string().datetime().optional() }).safeParse(req.body);
  if (!id.success) { res.status(400).json({ error: id.error.message }); return; }
  if (!parsed.success) { res.status(400).json({ error: parsed.error.message }); return; }
  const [product] = await db.select({ id: productsTable.id }).from(productsTable).where(eq(productsTable.id, id.data)).limit(1);
  if (!product) { res.status(404).json({ error: "Product not found" }); return; }
  const [revision] = await db.insert(productPriceRevisionsTable).values({
    productId: product.id, unitPriceCents: parsed.data.unitPriceCents,
    ...(parsed.data.effectiveFrom ? { effectiveFrom: new Date(parsed.data.effectiveFrom) } : {}),
  }).returning();
  res.status(201).json(revision);
});

router.get("/products/:id/current-price", async (req, res): Promise<void> => {
  const id = productId(req.params.id);
  if (!id.success) { res.status(400).json({ error: id.error.message }); return; }
  const [product] = await db.select({ id: productsTable.id }).from(productsTable).where(eq(productsTable.id, id.data)).limit(1);
  const [price] = product ? await db.select().from(productPriceRevisionsTable)
    .where(and(eq(productPriceRevisionsTable.productId, product.id), lte(productPriceRevisionsTable.effectiveFrom, new Date())))
    .orderBy(desc(productPriceRevisionsTable.effectiveFrom), desc(productPriceRevisionsTable.id)).limit(1) : [];
  if (!price) { res.status(404).json({ error: "Product or current price not found" }); return; }
  res.json(price);
});

router.get("/sales", async (req, res): Promise<void> => {
  const datum = day.catch(new Date().toISOString().slice(0, 10)).parse(req.query.datum);
  res.json(await daySalesReport(datum));
});

router.get("/bills/day-summary", async (req, res) => {
  const datum = day.safeParse(req.query.datum);
  if (!datum.success) return res.status(400).json({ error: "datum must be YYYY-MM-DD" });
  return res.json(await dayBillSummary(datum.data));
});

router.get("/bills/activity-days", async (req, res) => {
  const range = dateRange.safeParse(req.query);
  if (!range.success) return res.status(400).json({ error: range.error.issues[0]?.message ?? "Invalid date range" });
  return res.json(await activityDays(range.data.from, range.data.to));
});

router.get("/bills/period-summary", async (req, res) => {
  const range = dateRange.safeParse(req.query);
  if (!range.success) return res.status(400).json({ error: range.error.issues[0]?.message ?? "Invalid date range" });
  return res.json(await periodBillSummary(range.data.from, range.data.to));
});

/** Admin settlement uses the same incremental immutable payment events as terminals. */
router.post("/bills/:spielerId/paid", async (req, res) => {
  const spielerId = z.coerce.number().int().positive().safeParse(req.params.spielerId);
  const body = z.object({ datum: day, externalId: z.string().trim().min(1).max(200).optional() }).safeParse(req.body);
  if (!spielerId.success || !body.success) return res.status(400).json({ error: "spielerId and datum are required" });
  const externalId = body.data.externalId ?? `admin:${randomBytes(16).toString("hex")}`;
  const outcome = await db.transaction(async tx => {
    await lockBillSettlement(tx, spielerId.data, body.data.datum);
    const [prior] = await tx.select().from(billPaymentsTable).where(eq(billPaymentsTable.externalId, externalId)).limit(1);
    if (prior) return prior.spielerId === spielerId.data && prior.datum === body.data.datum ? "skipped" : "conflict";
    const settlement = await billSettlementStatus(tx, spielerId.data, body.data.datum);
    if (isSettlementRedundant(settlement)) return "skipped";
    const inserted = await tx.insert(billPaymentsTable).values({
      externalId, spielerId: spielerId.data, datum: body.data.datum, source: "ADMIN", markedByAdminId: (req as any).user.id,
    }).onConflictDoNothing().returning({ id: billPaymentsTable.id });
    return inserted.length ? "accepted" : "conflict";
  });
  return res.status(outcome === "conflict" ? 409 : 200).json({ externalId, status: outcome });
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
      subject: "Range-Master SMTP Test",
      text: "Dës Test-Email confirméiert datt d'SMTP-Astellunge fonctionéieren.",
    });
    return res.json({ success: true });
  } catch (err) {
    return res.status(502).json({ error: err instanceof Error ? err.message : String(err) });
  }
});

export default router;
