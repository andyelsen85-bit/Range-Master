import { Router } from "express";
import bcrypt from "bcryptjs";
import { db, spielerTable, spieleTable, spielTeilnahmenTable, ergebnisseTable, kreditEventsTable, spielerUpdatesTable, productsTable, productPriceRevisionsTable, saleEventsTable, billPaymentsTable, terminalConfigBackupsTable, terminalRestoreAuthorizationsTable } from "@workspace/db";
import { and, eq, inArray, sql, desc, isNotNull, gt, isNull } from "drizzle-orm";
import { requireApiKey } from "./auth";
import { z } from "zod";
import { getSmtpSettings, sendMail, generatePassword, invitationEmail, resetEmail } from "../lib/mailer";
import { catalogue, unsettledDaySalesReport } from "../lib/products";
import { billSettlementStatus, dayBillSummary, isSettlementRedundant, lockBillSettlement } from "../lib/bills";
import { sameImmutableChildren } from "../lib/immutable-events";
import { revisionToken, SYNC_MANIFEST_SCHEMA_VERSION } from "../lib/sync-manifest";
import {
  decryptTerminalConfiguration,
  encryptTerminalConfiguration,
  hashRestoreCode,
  terminalConfigurationSchema,
  TERMINAL_CONFIG_SCHEMA_VERSION,
} from "../lib/terminal-config";

const router = Router();

const terminalIdSchema = z.string().trim().min(1).max(120).regex(/^[A-Za-z0-9._:-]+$/);

// POST /api/sync/config-backup — upload an encrypted, versioned configuration snapshot
router.post("/config-backup", requireApiKey, async (req, res) => {
  if ((req as any).apiKeyType !== "TERMINAL") return res.status(403).json({ error: "Terminal API key required" });
  const body = z.object({
    terminalId: terminalIdSchema,
    schemaVersion: z.number().int().positive(),
    firmwareVersion: z.string().trim().max(80).optional(),
    configuration: terminalConfigurationSchema,
  }).safeParse(req.body);
  if (!body.success) return res.status(400).json({ error: "Ungültige Terminal-Konfiguration", details: body.error.issues });
  if (body.data.schemaVersion !== TERMINAL_CONFIG_SCHEMA_VERSION) {
    return res.status(409).json({ error: "Nicht unterstützte Konfigurationsversion", supportedSchemaVersion: TERMINAL_CONFIG_SCHEMA_VERSION });
  }

  const [latest] = await db.select().from(terminalConfigBackupsTable)
    .where(and(
      eq(terminalConfigBackupsTable.terminalId, body.data.terminalId),
      isNull(terminalConfigBackupsTable.revokedAt),
    ))
    .orderBy(desc(terminalConfigBackupsTable.updatedAt))
    .limit(1);
  if (latest) {
    try {
      const previous = decryptTerminalConfiguration(latest);
      if (JSON.stringify(previous) === JSON.stringify(body.data.configuration)) {
        const [backup] = await db.update(terminalConfigBackupsTable)
          .set({ updatedAt: new Date(), firmwareVersion: body.data.firmwareVersion ?? latest.firmwareVersion })
          .where(eq(terminalConfigBackupsTable.id, latest.id))
          .returning({
            id: terminalConfigBackupsTable.id,
            terminalId: terminalConfigBackupsTable.terminalId,
            schemaVersion: terminalConfigBackupsTable.schemaVersion,
            updatedAt: terminalConfigBackupsTable.updatedAt,
          });
        return res.json({ success: true, unchanged: true, backup });
      }
    } catch {
      // Preserve an unreadable snapshot for audit/revocation and create a fresh
      // validated version instead of overwriting potentially recoverable data.
    }
  }

  const encrypted = encryptTerminalConfiguration(body.data.configuration);
  const [backup] = await db.insert(terminalConfigBackupsTable).values({
    terminalId: body.data.terminalId,
    apiKeyId: (req as any).apiKeyId ?? null,
    schemaVersion: body.data.schemaVersion,
    firmwareVersion: body.data.firmwareVersion ?? null,
    ...encrypted,
  }).returning({
    id: terminalConfigBackupsTable.id,
    terminalId: terminalConfigBackupsTable.terminalId,
    schemaVersion: terminalConfigBackupsTable.schemaVersion,
    updatedAt: terminalConfigBackupsTable.updatedAt,
  });

  return res.status(201).json({ success: true, backup });
});

// GET /api/sync/config-restore?code=... — consume a one-time admin approval
router.get("/config-restore", requireApiKey, async (req, res) => {
  if ((req as any).apiKeyType !== "TERMINAL") return res.status(403).json({ error: "Terminal API key required" });
  const parsed = z.object({
    code: z.string().trim().regex(/^[A-F0-9]{12}$/i),
    terminalId: terminalIdSchema,
  }).safeParse(req.query);
  if (!parsed.success) return res.status(400).json({ error: "Ungültiger Wiederherstellungscode" });

  const now = new Date();
  const [candidate] = await db.select({
    authorizationId: terminalRestoreAuthorizationsTable.id,
    backup: terminalConfigBackupsTable,
  }).from(terminalRestoreAuthorizationsTable)
    .innerJoin(terminalConfigBackupsTable, eq(terminalRestoreAuthorizationsTable.backupId, terminalConfigBackupsTable.id))
    .where(and(
      eq(terminalRestoreAuthorizationsTable.codeHash, hashRestoreCode(parsed.data.code)),
      eq(terminalRestoreAuthorizationsTable.targetTerminalId, parsed.data.terminalId),
      eq(terminalRestoreAuthorizationsTable.targetApiKeyId, (req as any).apiKeyId),
      gt(terminalRestoreAuthorizationsTable.expiresAt, now),
      isNull(terminalRestoreAuthorizationsTable.usedAt),
      isNull(terminalRestoreAuthorizationsTable.revokedAt),
      isNull(terminalConfigBackupsTable.revokedAt),
    )).limit(1);
  if (!candidate || candidate.backup.terminalId === parsed.data.terminalId) {
    return res.status(404).json({ error: "Code ungültig, abgelaf oder Backup net méi verfügbar" });
  }

  let configuration;
  try {
    configuration = decryptTerminalConfiguration(candidate.backup);
  } catch {
    return res.status(409).json({ error: "Backup ass korrupt oder net kompatibel" });
  }

  class RestoreRaceError extends Error {}
  try {
    await db.transaction(async (tx) => {
      const [activeBackup] = await tx.update(terminalConfigBackupsTable)
        .set({ lastRestoredAt: terminalConfigBackupsTable.lastRestoredAt })
        .where(and(eq(terminalConfigBackupsTable.id, candidate.backup.id), isNull(terminalConfigBackupsTable.revokedAt)))
        .returning({ id: terminalConfigBackupsTable.id });
      if (!activeBackup) throw new RestoreRaceError();
      const [authorization] = await tx.update(terminalRestoreAuthorizationsTable)
        .set({ usedAt: now })
        .where(and(
          eq(terminalRestoreAuthorizationsTable.id, candidate.authorizationId),
          eq(terminalRestoreAuthorizationsTable.targetTerminalId, parsed.data.terminalId),
          eq(terminalRestoreAuthorizationsTable.targetApiKeyId, (req as any).apiKeyId),
          gt(terminalRestoreAuthorizationsTable.expiresAt, now),
          isNull(terminalRestoreAuthorizationsTable.usedAt),
          isNull(terminalRestoreAuthorizationsTable.revokedAt),
        ))
        .returning({ id: terminalRestoreAuthorizationsTable.id });
      if (!authorization) throw new RestoreRaceError();
      await tx.update(terminalConfigBackupsTable).set({ lastRestoredAt: now })
        .where(eq(terminalConfigBackupsTable.id, candidate.backup.id));
    });
  } catch (error) {
    if (error instanceof RestoreRaceError) {
      return res.status(404).json({ error: "Code ungültig, abgelaf oder Backup net méi verfügbar" });
    }
    throw error;
  }
  return res.json({
    schemaVersion: candidate.backup.schemaVersion,
    terminalId: candidate.backup.terminalId,
    firmwareVersion: candidate.backup.firmwareVersion,
    checksum: candidate.backup.checksum,
    configuration,
  });
});

/** Auto-generate next WLZ number — same logic used by the admin player-create route */
async function nextMitgliedNr(): Promise<string> {
  const [maxRow] = await db
    .select({
      maxNr: sql<number | null>`MAX(CAST(SUBSTRING(${spielerTable.mitgliedNr} FROM 4) AS INTEGER))`,
    })
    .from(spielerTable)
    .where(sql`${spielerTable.mitgliedNr} ~ '^WLZ[0-9]+$'`);
  const next = (maxRow?.maxNr ?? 0) + 1;
  return `WLZ${String(next).padStart(3, "0")}`;
}

const maschineValues = ["A", "B", "C", "D", "E", "F", "G", "H"] as const;
const modusValues = ["NORMAL", "HARAKIRI", "CUSTOM_1", "CUSTOM_2", "CUSTOM_3", "CUSTOM_4"] as const;

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
  confirmedLaunches: z.number().int().min(0).optional().default(0),
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

async function rosterPayload() {
  const rows = await db
    .select({
      id: spielerTable.id,
      name: spielerTable.name,
      mitgliedNr: spielerTable.mitgliedNr,
      email: spielerTable.email,
      portalAktiv: spielerTable.portalAktiv,
    })
    .from(spielerTable)
    .orderBy(spielerTable.name, spielerTable.id);
  return { spieler: rows };
}

// GET /api/sync/spieler
router.get("/spieler", requireApiKey, async (_req, res) => {
  return res.json(await rosterPayload());
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

    const mitgliedNr = s.mitgliedNr ?? await nextMitgliedNr();
    const [inserted] = await db
      .insert(spielerTable)
      .values({ name, mitgliedNr })
      .returning({ id: spielerTable.id });
    synced++;
    mappings.push({ localId: s.id, id: inserted.id, name, status: "created" });
  }

  return res.json({ synced, mappings });
});

async function recentGamesPayload(limit: number) {
  const recentSpiele = await db
    .select({
      id: spieleTable.id,
      externalId: spieleTable.externalId,
      datum: spieleTable.datum,
      modus: spieleTable.modus,
      lauf: spieleTable.lauf,
      taubenProLauf: spieleTable.taubenProLauf,
      abgeschlossen: spieleTable.abgeschlossen, confirmedLaunches: spieleTable.confirmedLaunches,
    })
    .from(spieleTable)
    .where(isNotNull(spieleTable.externalId))
    .orderBy(desc(spieleTable.datum), desc(spieleTable.id))
    .limit(limit);

  if (recentSpiele.length === 0) return { spiele: [] };

  const spielIds = recentSpiele.map(s => s.id);

  const teilnahmenRows = await db
    .select()
    .from(spielTeilnahmenTable)
    .where(inArray(spielTeilnahmenTable.spielId, spielIds))
    .orderBy(spielTeilnahmenTable.spielId, spielTeilnahmenTable.spielerId, spielTeilnahmenTable.lauf);

  const spielerIds = [...new Set(teilnahmenRows.map(t => t.spielerId))];
  const spielerRows = spielerIds.length > 0
    ? await db.select({ id: spielerTable.id, name: spielerTable.name }).from(spielerTable).where(inArray(spielerTable.id, spielerIds))
    : [];
  const spielerMap = new Map(spielerRows.map(s => [s.id, s.name]));

  const teilnahmenBySpiel = new Map<number, typeof teilnahmenRows>();
  for (const t of teilnahmenRows) {
    if (!teilnahmenBySpiel.has(t.spielId)) teilnahmenBySpiel.set(t.spielId, []);
    teilnahmenBySpiel.get(t.spielId)!.push(t);
  }

  const spiele = recentSpiele.map(s => {
    const teilnahmen = teilnahmenBySpiel.get(s.id) ?? [];
    const spielerNamen: Record<number, string> = {};
    for (const t of teilnahmen) spielerNamen[t.spielerId] = spielerMap.get(t.spielerId) ?? `Spiller ${t.spielerId}`;
    return {
      externalId: s.externalId!,
      datum: s.datum.toISOString(),
      modus: s.modus,
      lauf: s.lauf,
      taubenProLauf: s.taubenProLauf,
      abgeschlossen: s.abgeschlossen,
      confirmedLaunches: s.confirmedLaunches,
      teilnahmen: teilnahmen.map(t => ({ spielerId: t.spielerId, startPosten: t.startPosten, punkte: t.punkte, lauf: t.lauf })),
      spielerNamen,
    };
  });

  return { spiele };
}

// GET /api/sync/spiele?limit=N — pull recent games to terminal (newest first)
router.get("/spiele", requireApiKey, async (req, res) => {
  const limit = Math.min(500, Math.max(1, Number(req.query.limit) || 100));
  return res.json(await recentGamesPayload(limit));
});

// POST /api/sync/spiele
router.post("/spiele", requireApiKey, async (req, res) => {
  const body = z.object({ spiele: z.array(SpielSchema) }).parse(req.body);
  const results = [];

  for (const s of body.spiele) {
    if (s.confirmedLaunches > 0 && (!s.abgeschlossen || s.confirmedLaunches > s.ergebnisse.length)) {
      results.push({ externalId: s.externalId, status: "conflict" as const, error: "confirmedLaunches requires completed game and cannot exceed result count" });
      continue;
    }
    const status = await db.transaction(async tx => {
      // The unique external ID is the concurrency gate.  A competing writer
      // either creates all children in this transaction or leaves nothing.
      const inserted = await tx.insert(spieleTable).values({
        externalId: s.externalId, datum: new Date(s.datum), modus: s.modus, lauf: s.lauf,
        taubenProLauf: s.taubenProLauf, abgeschlossen: s.abgeschlossen,
        confirmedLaunches: s.confirmedLaunches, syncedAt: new Date(),
      }).onConflictDoNothing({ target: spieleTable.externalId }).returning({ id: spieleTable.id });
      if (inserted[0]) {
        if (s.teilnahmen.length) await tx.insert(spielTeilnahmenTable).values(s.teilnahmen.map(t => ({ spielId: inserted[0].id, ...t })));
        if (s.ergebnisse.length) await tx.insert(ergebnisseTable).values(s.ergebnisse.map(e => ({ spielId: inserted[0].id, ...e })));
        return "created" as const;
      }
      const [stored] = await tx.select().from(spieleTable).where(eq(spieleTable.externalId, s.externalId)).limit(1);
      if (!stored) return "conflict" as const; // defensive: a concurrently deleted row
      const [storedTeilnahmen, storedErgebnisse] = await Promise.all([
        tx.select({ spielerId: spielTeilnahmenTable.spielerId, startPosten: spielTeilnahmenTable.startPosten, punkte: spielTeilnahmenTable.punkte, lauf: spielTeilnahmenTable.lauf }).from(spielTeilnahmenTable).where(eq(spielTeilnahmenTable.spielId, stored.id)),
        tx.select({ spielerId: ergebnisseTable.spielerId, lauf: ergebnisseTable.lauf, taube: ergebnisseTable.taube, maschine: ergebnisseTable.maschine, posten: ergebnisseTable.posten, schuss1: ergebnisseTable.schuss1, schuss2: ergebnisseTable.schuss2, punkte: ergebnisseTable.punkte, wiederholt: ergebnisseTable.wiederholt }).from(ergebnisseTable).where(eq(ergebnisseTable.spielId, stored.id)),
      ]);
      const sameParent = stored.datum.toISOString() === new Date(s.datum).toISOString() && stored.modus === s.modus &&
        stored.lauf === s.lauf && stored.taubenProLauf === s.taubenProLauf && stored.abgeschlossen === s.abgeschlossen &&
        stored.confirmedLaunches === s.confirmedLaunches;
      return sameParent && sameImmutableChildren(storedTeilnahmen, s.teilnahmen) && sameImmutableChildren(storedErgebnisse, s.ergebnisse)
        ? "skipped" as const : "conflict" as const;
    });
    results.push({ externalId: s.externalId, status });
  }

  return res.json({ results });
});

const PaymentEventSchema = z.object({
  externalId: z.string().trim().min(1).max(200),
  spielerId: z.number().int().positive(),
  datum: z.string().regex(/^\d{4}-\d{2}-\d{2}$/),
  terminalId: terminalIdSchema.optional(),
});

// GET /api/sync/bills/day-summary?datum=YYYY-MM-DD — shared authoritative bills.
router.get("/bills/day-summary", requireApiKey, async (req, res) => {
  const datum = z.string().regex(/^\d{4}-\d{2}-\d{2}$/).safeParse(req.query.datum);
  if (!datum.success) return res.status(400).json({ error: "datum must be YYYY-MM-DD" });
  return res.json(await dayBillSummary(datum.data));
});

// POST /api/sync/payments — atomic validation with explicit idempotency outcomes.
router.post("/payments", requireApiKey, async (req, res) => {
  const parsed = z.object({ events: z.array(PaymentEventSchema).min(1).max(500) }).safeParse(req.body);
  if (!parsed.success) return res.status(400).json({ error: parsed.error.message });
  const duplicateIds = new Set<string>();
  const seen = new Set<string>();
  for (const event of parsed.data.events) { if (seen.has(event.externalId)) duplicateIds.add(event.externalId); seen.add(event.externalId); }
  const playerIds = [...new Set(parsed.data.events.map(e => e.spielerId))];
  const players = await db.select({ id: spielerTable.id }).from(spielerTable).where(inArray(spielerTable.id, playerIds));
  const known = new Set(players.map(p => p.id));
  if (playerIds.some(id => !known.has(id))) return res.status(400).json({ error: "Unknown player in payment batch" });
  const results = await db.transaction(async tx => {
    const outcome: Array<{ externalId: string; status: "accepted" | "skipped" | "conflict"; error?: string }> = [];
    const scopes = [...new Set(parsed.data.events.map(event => `${event.spielerId}:${event.datum}`))].sort();
    for (const scope of scopes) {
      const [spielerId, datum] = scope.split(":");
      await lockBillSettlement(tx, Number(spielerId), datum);
    }
    for (const event of parsed.data.events) {
      if (duplicateIds.has(event.externalId)) { outcome.push({ externalId: event.externalId, status: "conflict", error: "duplicate externalId in batch" }); continue; }
      const [byExternal] = await tx.select().from(billPaymentsTable).where(eq(billPaymentsTable.externalId, event.externalId)).limit(1);
      if (byExternal) {
        outcome.push({ externalId: event.externalId, status: byExternal.spielerId === event.spielerId && byExternal.datum === event.datum ? "skipped" : "conflict", ...(byExternal.spielerId === event.spielerId && byExternal.datum === event.datum ? {} : { error: "externalId belongs to a different payment" }) }); continue;
      }
      const settlement = await billSettlementStatus(tx, event.spielerId, event.datum);
      if (isSettlementRedundant(settlement)) {
        outcome.push({ externalId: event.externalId, status: "skipped" }); continue;
      }
      const inserted = await tx.insert(billPaymentsTable).values({
        ...event, source: "TERMINAL", markedByApiKeyId: (req as any).apiKeyId ?? null,
      }).onConflictDoNothing().returning({ id: billPaymentsTable.id });
      outcome.push(inserted.length
        ? { externalId: event.externalId, status: "accepted" }
        : { externalId: event.externalId, status: "conflict", error: "payment conflicts with an existing payment" });
    }
    return outcome;
  });
  return res.json({ results });
});

// ─── Day credits ──────────────────────────────────────────────────────────────

const KreditEventSchema = z.object({
  externalId: z.string().min(8),
  spielerId: z.number().int().positive(),
  datum: z.string().regex(/^\d{4}-\d{2}-\d{2}$/),
  typ: z.enum(["GRANT", "USE"]),
  anzahl: z.number().int().min(-100).max(100).refine(n => n !== 0, "anzahl darf net 0 sinn"),
  occurredAt: z.string().datetime().optional(),
  priceRevisionId: z.number().int().positive().optional(),
  unitPriceCents: z.number().int().min(0).optional(),
}).superRefine((event, ctx) => {
  if (event.typ === "USE") {
    for (const field of ["occurredAt", "priceRevisionId", "unitPriceCents"] as const) {
      if (event[field] == null) ctx.addIssue({ code: z.ZodIssueCode.custom, path: [field], message: `${field} is required for credit use events` });
    }
  }
});
class LedgerBalanceError extends Error {}

/** Must stay byte-for-byte compatible with the admin ledger lock names. */
async function lockLedger(tx: any, scope: string): Promise<void> {
  await tx.execute(sql`SELECT pg_advisory_xact_lock(hashtextextended(${scope}, 0))`);
}

async function unsettledDayCreditsPayload(datum: string) {
  const afterLatestPayment = sql<boolean>`${kreditEventsTable.createdAt} > COALESCE((
    SELECT MAX(${billPaymentsTable.paidAt})
    FROM ${billPaymentsTable}
    WHERE ${billPaymentsTable.spielerId} = ${kreditEventsTable.spielerId}
      AND ${billPaymentsTable.datum} = ${kreditEventsTable.datum}
  ), '-infinity'::timestamptz)`;
  const rows = await db
    .select({
      spielerId: kreditEventsTable.spielerId,
      gewaehrt: sql<number>`COALESCE(SUM(CASE WHEN ${kreditEventsTable.typ} = 'GRANT' THEN ${kreditEventsTable.anzahl} ELSE 0 END), 0)::int`,
      verbraucht: sql<number>`COALESCE(SUM(CASE WHEN ${kreditEventsTable.typ} = 'USE' THEN ${kreditEventsTable.anzahl} ELSE 0 END), 0)::int`,
    })
    .from(kreditEventsTable)
    .where(and(eq(kreditEventsTable.datum, datum), afterLatestPayment))
    .groupBy(kreditEventsTable.spielerId)
    .orderBy(kreditEventsTable.spielerId);
  return { datum, kredite: rows };
}

// GET /api/sync/kredite?datum=YYYY-MM-DD — aggregated per-player credits for one day
router.get("/kredite", requireApiKey, async (req, res) => {
  const datum = z.string().regex(/^\d{4}-\d{2}-\d{2}$/).catch(new Date().toISOString().slice(0, 10)).parse(req.query.datum);
  return res.json(await unsettledDayCreditsPayload(datum));
});

// POST /api/sync/kredite — idempotent push of grant/use events from the terminal
router.post("/kredite", requireApiKey, async (req, res) => {
  const body = z.object({ events: z.array(KreditEventSchema) }).parse(req.body);

  let result: {
    synced: number;
    skipped: number;
    results: Array<{ externalId: string; status: "accepted" | "skipped" }>;
  } = { synced: 0, skipped: 0, results: [] };
  if (body.events.length) {
    // Reject events for unknown players explicitly (avoid FK 500s)
    const ids = [...new Set(body.events.map((e) => e.spielerId))];
    const known = await db.select({ id: spielerTable.id }).from(spielerTable).where(inArray(spielerTable.id, ids));
    const knownIds = new Set(known.map((k) => k.id));
    const unknown = ids.filter((id) => !knownIds.has(id));
    if (unknown.length) {
      return res.status(400).json({ error: `Onbekannte Spiller-IDs: ${unknown.join(", ")}` });
    }

    try {
      result = await db.transaction(async (tx) => {
        const scopes = [...new Set(body.events.map(event => `credit:${event.spielerId}:${event.datum}`))].sort();
        const billScopes = [...new Set(body.events.map(event => `${event.spielerId}:${event.datum}`))].sort();
        for (const scope of billScopes) {
          const [spielerId, datum] = scope.split(":");
          await lockBillSettlement(tx, Number(spielerId), datum);
        }
        for (const scope of scopes) await lockLedger(tx, scope);
        const useEvents = body.events.filter(event => event.typ === "USE");
        const [gameCredit] = useEvents.length
          ? await tx.select({ id: productsTable.id }).from(productsTable).where(eq(productsTable.code, "GAME_CREDIT")).limit(1)
          : [];
        if (useEvents.length && !gameCredit) throw new LedgerBalanceError("GAME_CREDIT product is not configured");
        const revisionIds = [...new Set(useEvents.map(event => event.priceRevisionId!))];
        const revisions = revisionIds.length
          ? await tx.select().from(productPriceRevisionsTable).where(inArray(productPriceRevisionsTable.id, revisionIds))
          : [];
        const revisionById = new Map(revisions.map(revision => [revision.id, revision]));
        for (const event of useEvents) {
          const revision = revisionById.get(event.priceRevisionId!);
          if (!revision || revision.productId !== gameCredit!.id || revision.unitPriceCents !== event.unitPriceCents) {
            throw new LedgerBalanceError("GAME_CREDIT price revision or price does not match");
          }
        }

        // Already-stored IDs and duplicate IDs in this upload are no-ops, just
        // like the former bulk ON CONFLICT upload.
        const candidates: typeof body.events = [];
        const seen = new Set<string>();
        for (const event of body.events) {
          if (seen.has(event.externalId)) continue;
          seen.add(event.externalId);
          const [prior] = await tx.select({ id: kreditEventsTable.id }).from(kreditEventsTable)
            .where(eq(kreditEventsTable.externalId, event.externalId)).limit(1);
          if (!prior) candidates.push(event);
        }
        for (const scope of scopes) {
          const [, spielerIdText, datum] = scope.split(":");
          const spielerId = Number(spielerIdText);
          const [balance] = await tx.select({
            available: sql<number>`COALESCE(SUM(CASE WHEN ${kreditEventsTable.typ} = 'GRANT' THEN ${kreditEventsTable.anzahl} ELSE -${kreditEventsTable.anzahl} END), 0)::int`,
          }).from(kreditEventsTable).where(and(eq(kreditEventsTable.spielerId, spielerId), eq(kreditEventsTable.datum, datum)));
          const change = candidates.filter(event => event.spielerId === spielerId && event.datum === datum)
            .reduce((sum, event) => sum + (event.typ === "GRANT" ? event.anzahl : -event.anzahl), 0);
          if (Number(balance?.available ?? 0) + change < 0) throw new LedgerBalanceError("Credit balance cannot become negative");
        }
        const inserted = candidates.length
          ? await tx.insert(kreditEventsTable).values(candidates.map(event => {
              const { occurredAt, priceRevisionId, unitPriceCents, ...creditEvent } = event;
              return event.typ === "USE"
                ? { ...creditEvent, occurredAt: new Date(occurredAt!), priceRevisionId: priceRevisionId!, unitPriceCents: unitPriceCents! }
                : creditEvent;
            }))
              .onConflictDoNothing({ target: kreditEventsTable.externalId })
              .returning({ externalId: kreditEventsTable.externalId })
          : [];
        const acceptedIds = new Set(inserted.map(row => row.externalId));
        const results = body.events.map(event => ({
          externalId: event.externalId,
          status: acceptedIds.has(event.externalId) ? "accepted" as const : "skipped" as const,
        }));
        return {
          synced: inserted.length,
          skipped: body.events.length - inserted.length,
          results,
        };
      });
    } catch (error) {
      if (error instanceof LedgerBalanceError) return res.status(409).json({ error: error.message });
      throw error;
    }
  }

  return res.json(result);
});

// ─── Product catalogue and sale events ───────────────────────────────────────

const SaleEventSchema = z.object({
  externalId: z.string().min(1).max(200),
  spielerId: z.number().int().positive(),
  datum: z.string().regex(/^\d{4}-\d{2}-\d{2}$/),
  productId: z.number().int().positive(),
  // null/omitted (and legacy wire value 0) requests server-side allocation of
  // a negative correction to the original immutable sale lot.
  priceRevisionId: z.number().int().min(0).nullable().optional(),
  quantity: z.number().int().min(-1).refine((value) => value !== 0, "quantity must not be zero"),
}).superRefine((event, ctx) => {
  if (event.quantity > 0 && !(event.priceRevisionId && event.priceRevisionId > 0)) {
    ctx.addIssue({ code: z.ZodIssueCode.custom, path: ["priceRevisionId"], message: "positive sales require a price revision" });
  }
  if (event.quantity < 0 && event.priceRevisionId != null && event.priceRevisionId !== 0) {
    ctx.addIssue({ code: z.ZodIssueCode.custom, path: ["priceRevisionId"], message: "negative corrections must be unallocated" });
  }
});
class SaleBatchValidationError extends Error {}
class SaleIdempotencyConflictError extends Error {}

// GET /api/sync/products — only currently sellable products and their price revision.
router.get("/products", requireApiKey, async (_req, res): Promise<void> => {
  res.json({ products: await catalogue(true) });
});

// GET /api/sync/sales?datum=YYYY-MM-DD — totals for terminal reconciliation.
router.get("/sales", requireApiKey, async (req, res): Promise<void> => {
  const datum = z.string().regex(/^\d{4}-\d{2}-\d{2}$/).safeParse(req.query.datum);
  if (!datum.success) { res.status(400).json({ error: "datum must be YYYY-MM-DD" }); return; }
  res.json(await unsettledDaySalesReport(datum.data));
});

// GET /api/sync/manifest?datum=YYYY-MM-DD — content-addressed revisions let an
// offline terminal avoid downloading unchanged full snapshots. The hashes cover
// exactly the corresponding established GET response, never this response's
// timestamp, so polling does not itself invalidate a revision.
router.get("/manifest", requireApiKey, async (req, res): Promise<void> => {
  const parsed = z.object({ datum: z.string().regex(/^\d{4}-\d{2}-\d{2}$/) }).safeParse(req.query);
  if (!parsed.success) {
    res.status(400).json({ error: "datum must be YYYY-MM-DD" });
    return;
  }

  const datum = parsed.data.datum;
  // Must match the firmware's MAX_HISTORY request exactly so the revision
  // describes the snapshot that is durably cached.
  const gameHistoryLimit = 20;
  const [roster, products, gameHistory, dailyCredits, dailySales, dailyBillSummary] = await Promise.all([
    rosterPayload(),
    catalogue(true).then((items) => ({ products: items })),
    recentGamesPayload(gameHistoryLimit),
    unsettledDayCreditsPayload(datum),
    unsettledDaySalesReport(datum),
    dayBillSummary(datum),
  ]);
  const timestamp = new Date().toISOString();

  res.json({
    schemaVersion: SYNC_MANIFEST_SCHEMA_VERSION,
    timestamp,
    serverDate: timestamp.slice(0, 10),
    datum,
    gameHistoryLimit,
    revisions: {
      roster: revisionToken(roster),
      productCatalog: revisionToken(products),
      gameHistory: revisionToken(gameHistory),
      dailyCredits: revisionToken(dailyCredits),
      dailySales: revisionToken(dailySales),
      dailyBillSummary: revisionToken(dailyBillSummary),
    },
  });
});

// POST /api/sync/sales — offline terminal queue upload. A revision must belong
// to its product; unitPriceCents is copied from that immutable revision.
router.post("/sales", requireApiKey, async (req, res): Promise<void> => {
  const parsed = z.object({ events: z.array(SaleEventSchema).max(500) }).safeParse(req.body);
  if (!parsed.success) { res.status(400).json({ error: parsed.error.message }); return; }
  try {
    const result = await db.transaction(async (tx) => {
      // Validate every event before issuing any insert. This transaction makes
      // a malformed offline batch all-or-nothing rather than partly accepted.
      const playerIds = [...new Set(parsed.data.events.map((event) => event.spielerId))];
      const productIds = [...new Set(parsed.data.events.map((event) => event.productId))];
      const revisionIds = [...new Set(parsed.data.events
        .filter((event) => event.quantity > 0)
        .map((event) => event.priceRevisionId!))];
      const players = playerIds.length
        ? await tx.select({ id: spielerTable.id }).from(spielerTable).where(inArray(spielerTable.id, playerIds))
        : [];
      const products = productIds.length
        ? await tx.select({ id: productsTable.id, name: productsTable.name, category: productsTable.category }).from(productsTable).where(inArray(productsTable.id, productIds))
        : [];
      const revisions = revisionIds.length
        ? await tx.select().from(productPriceRevisionsTable).where(inArray(productPriceRevisionsTable.id, revisionIds))
        : [];
      const knownPlayers = new Set(players.map((player) => player.id));
      const knownProducts = new Set(products.map((product) => product.id));
      const productById = new Map(products.map((product) => [product.id, product]));
      const revisionById = new Map(revisions.map((revision) => [revision.id, revision]));
      for (const event of parsed.data.events) {
        if (!knownPlayers.has(event.spielerId)) throw new SaleBatchValidationError(`Unknown player: ${event.spielerId}`);
        if (!knownProducts.has(event.productId)) throw new SaleBatchValidationError(`Unknown product: ${event.productId}`);
        const revision = event.quantity > 0 ? revisionById.get(event.priceRevisionId!) : undefined;
        if (event.quantity > 0 && (!revision || revision.productId !== event.productId)) {
          throw new SaleBatchValidationError(`Unknown price revision for product: ${event.priceRevisionId}`);
        }
      }
      const billScopes = [...new Set(parsed.data.events.map(event => `${event.spielerId}:${event.datum}`))].sort();
      for (const scope of billScopes) {
        const [spielerId, datum] = scope.split(":");
        await lockBillSettlement(tx, Number(spielerId), datum);
      }
      const scopes = [...new Set(parsed.data.events.map(event => `ammo:${event.spielerId}:${event.datum}:${event.productId}`))].sort();
      for (const scope of scopes) await lockLedger(tx, scope);
      const seen = new Set<string>();
      const results: Array<{ externalId: string; status: "accepted" | "skipped" }> = [];
      let synced = 0;
      for (const event of parsed.data.events) {
        if (seen.has(event.externalId)) throw new SaleIdempotencyConflictError("duplicate externalId in batch");
        seen.add(event.externalId);
        const [prior] = await tx.select().from(saleEventsTable)
          .where(eq(saleEventsTable.externalId, event.externalId)).limit(1);
        if (prior) {
          // Unallocated correction replays deliberately ignore the stored,
          // server-allocated revision but retain all caller-owned fields.
          const matches = prior.spielerId === event.spielerId && prior.datum === event.datum &&
            prior.productId === event.productId && prior.quantity === event.quantity &&
            (event.quantity < 0 || prior.priceRevisionId === event.priceRevisionId);
          if (!matches) throw new SaleIdempotencyConflictError("externalId belongs to a different sale");
          results.push({ externalId: event.externalId, status: "skipped" });
          continue;
        }
        if (event.quantity > 0) {
          const revision = revisionById.get(event.priceRevisionId!)!;
          await tx.insert(saleEventsTable).values({
            externalId: event.externalId, spielerId: event.spielerId, datum: event.datum, productId: event.productId,
            priceRevisionId: revision.id, unitPriceCents: revision.unitPriceCents,
            productName: productById.get(event.productId)!.name, productCategory: productById.get(event.productId)!.category,
            quantity: event.quantity,
          });
        } else {
          const [lot] = await tx.select({
            priceRevisionId: saleEventsTable.priceRevisionId,
            productName: saleEventsTable.productName,
            productCategory: saleEventsTable.productCategory,
            unitPriceCents: saleEventsTable.unitPriceCents,
          }).from(saleEventsTable).where(and(
            eq(saleEventsTable.spielerId, event.spielerId), eq(saleEventsTable.datum, event.datum),
            eq(saleEventsTable.productId, event.productId),
          )).groupBy(saleEventsTable.priceRevisionId, saleEventsTable.productName, saleEventsTable.productCategory, saleEventsTable.unitPriceCents)
            .having(sql`SUM(${saleEventsTable.quantity}) > 0`)
            .orderBy(desc(sql`MAX(CASE WHEN ${saleEventsTable.quantity} > 0 THEN ${saleEventsTable.id} ELSE 0 END)`)).limit(1);
          if (!lot) throw new LedgerBalanceError("Ammunition quantity cannot become negative");
          await tx.insert(saleEventsTable).values({ ...event, ...lot });
        }
        synced++;
        results.push({ externalId: event.externalId, status: "accepted" });
      }
      return { synced, skipped: parsed.data.events.length - synced, results };
    });
    res.json(result);
  } catch (error) {
    if (error instanceof LedgerBalanceError) {
      res.status(409).json({ error: error.message });
      return;
    }
    if (error instanceof SaleIdempotencyConflictError) {
      res.status(409).json({ error: error.message });
      return;
    }
    if (error instanceof SaleBatchValidationError) {
      res.status(400).json({ error: error.message });
      return;
    }
    throw error;
  }
});

// ─── New player created on terminal ──────────────────────────────────────────

// POST /api/sync/spieler-neu — create a brand-new player from the terminal.
// Called when the operator adds a player locally who has no portal account yet.
// Returns the new portal ID so the terminal can replace its local negative ID.
router.post("/spieler-neu", requireApiKey, async (req, res) => {
  const body = z.object({
    externalId: z.string().min(8),
    name: z.string().min(1).max(100),
    email: z.string().email().nullable().optional(),
  }).parse(req.body);

  const [row] = await db
    .insert(spielerTable)
    .values({
      name: body.name,
      email: body.email ?? null,
      portalAktiv: false,
    })
    .returning({ id: spielerTable.id, name: spielerTable.name });

  return res.status(201).json({ id: row.id, name: row.name, externalId: body.externalId });
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
