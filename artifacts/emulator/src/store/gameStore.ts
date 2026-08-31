import { create } from 'zustand';

export type Maschine = 'A' | 'B' | 'C' | 'D' | 'E' | 'F' | 'G' | 'H';
export type Modus = 'NORMAL' | 'HARAKIRI' | 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4';
export type Screen = 'dashboard' | 'start' | 'spiel' | 'einstellungen' | 'resultate' | 'geschichte' | 'kredite' | 'spillerverwaltung';
export type SyncStatus = 'idle' | 'syncing' | 'success' | 'error';
export const AUTO_SYNC_DEFAULT_SECONDS = 300;
export const BILLING_SYNC_MIN_SECONDS = 20;
export const BILLING_SYNC_MAX_SECONDS = 30;
export const BILLING_SYNC_DEFAULT_SECONDS = 30;

export interface Spieler {
  id: number;
  name: string;
  punkte: number;
  startPosten: number; // 1–5 starting post position
}
/** Durable setup assignment. It intentionally has no score or game cursor. */
export interface LineupEintrag {
  spielerId: number;
  startPosten: number;
}

export interface Ergebnis {
  spielerId: number;
  lauf: number;
  taube: number;       // 1-based within lauf
  maschine: Maschine;
  posten: number;
  schuss1: boolean;
  schuss2: boolean;
  punkte: number;
  wiederholt: boolean;
}

export interface SequenzEintrag {
  maschine: Maschine;
  doubletteNr?: 1 | 2;
  pairKind?: 'h' | 'custom';
  partner?: Maschine;
  delayMs?: number;
}

export interface PortalSpieler {
  id: number;
  name: string;
  mitgliedNr: string | null;
  email?: string | null;
  portalAktiv?: boolean;
  /** true for players created locally on the terminal, not yet pushed to the portal */
  lokal?: boolean;
}

/** Status of a queued player change: local → pushed → email delivered/failed */
export type SpielerUpdateStatus = 'pending' | 'synced' | 'email_sent' | 'email_failed';

/** A player edit or password reset queued for portal sync (idempotent via externalId) */
export interface SpielerUpdate {
  externalId: string;
  spielerId: number;
  spielerName: string;   // snapshot for display
  typ: 'UPDATE' | 'PASSWORT_RESET';
  name?: string;
  email?: string | null;
  portalAktiv?: boolean;
  status: SpielerUpdateStatus;
  fehler?: string;
  queuedAt: string;
}

/** A locally created player waiting to be pushed to the portal (localId is negative) */
export interface PendingSpieler {
  localId: number;
  name: string;
}

// Custom modes store launch units, not expanded score entries. H is always a
// local H1/H2 doublette. A-G custom pairs carry their ordered partner and delay.
export interface CustomSequenzEintrag {
  maschine: Maschine;
  partner?: Maschine;
  isDoublette?: boolean;
  delaySeconds?: number;
}
export type CustomSequenz = CustomSequenzEintrag[];

/** Per-player credit tally for the current day */
export interface KreditStand {
  gewaehrt: number;
  verbraucht: number;
}

/** A credit grant/consumption event queued for portal sync (idempotent via externalId) */
export interface KreditEvent {
  externalId: string;
  spielerId: number;
  datum: string; // YYYY-MM-DD
  typ: 'GRANT' | 'USE';
  anzahl: number;
  /** Immutable billing snapshot required for a billable USE (absent on GRANTs). */
  occurredAt?: string;
  priceRevisionId?: number;
  unitPriceCents?: number;
}

export interface Produkt {
  id: number;
  code: string | null;
  category: string;
  name: string;
  active: boolean;
  currentPrice: { id: number; productId: number; unitPriceCents: number; effectiveFrom: string } | null;
}

/** Immutable ledger entry. Negative quantities are corrections, never edits. */
export interface VerkaufEvent {
  externalId: string;
  spielerId: number;
  datum: string;
  productId: number;
  priceRevisionId: number;
  quantity: number;
}

export interface PaymentEvent {
  externalId: string;
  spielerId: number;
  datum: string;
  /** Local snapshot used to detect activity entered after an offline payment. */
  coveredActivityExternalIds?: string[];
  /** Exact bill closed by this payment; retained after its outbox rows reconcile. */
  coveredBillSnapshot?: DaySummary['players'][number];
}

export interface DaySummary {
  datum: string;
  players: Array<{
    spielerId: number;
    spielerName: string;
    mitgliedNr: string | null;
    lines: Array<{
      productId: number;
      productName: string;
      category: string;
      priceRevisionId: number;
      unitPriceCents: number;
      quantity: number;
      totalCents: number;
      /** Locally queued activity which is not yet part of the authoritative bill. */
      pending?: boolean;
    }>;
    categorySubtotals: Record<string, number>;
    totalCents: number;
    credit: { granted: number; used: number; remaining: number };
    games: number;
    completedGames: number;
    state: 'OPEN' | 'PENDING_NEUTRAL' | 'PAID';
    paymentExternalId: string | null;
    paidAt: string | null;
  }>;
  categorySubtotals: Record<string, number>;
  productTotals: Record<string, number>;
  generalTotalCents: number;
  uniquePlayers: number;
  paidPlayers: number;
  games: number;
  completedGames: number;
  confirmedClays: number;
}

/** Authoritative, per-player/day sale row returned by GET /api/sync/sales. */
export interface VerkaufReportRow {
  spielerId: number;
  spielerName?: string;
  productId: number;
  productName?: string;
  quantity: number;
  totalCents: number;
}

export const PRODUKT_IDS = {
  kredit: 'GAME_CREDIT',
  cal12: 'AMMO_CAL12',
  cal20: 'AMMO_CAL20',
} as const;

const DEFAULT_PRODUKTE: Produkt[] = [
  { id: -1, code: 'GAME_CREDIT', category: 'GAME_CREDIT', name: 'Game credit', active: true, currentPrice: null },
  { id: -2, code: 'AMMO_CAL12', category: 'AMMO_CAL12', name: 'Cal. 12', active: true, currentPrice: null },
  { id: -3, code: 'AMMO_CAL20', category: 'AMMO_CAL20', name: 'Cal. 20', active: true, currentPrice: null },
];

const MAX_PENDING_KREDIT_EVENTS = 50;
const kreditEventsInFlight = new Set<string>();

/** A finished game kept in local history (last 50) */
export interface FinishedGame extends PendingGame {
  finishedAt: string; // ISO timestamp of completion
  spielerNamen: Record<number, string>; // name snapshot so history works after renames
}

/** A completed game queued for later sync */
export interface PendingGame {
  externalId: string;
  datum: string;
  modus: Modus;
  lauf: number;
  taubenProLauf: number;
  abgeschlossen: boolean;
  teilnahmen: Array<{
    spielerId: number;
    startPosten: number;
    punkte: number;
    lauf: number;
  }>;
  ergebnisse: Ergebnis[];
  confirmedLaunches: number;
}

/** Compute tauben per Lauf from a sequence (H = 2 tauben) */
export function taubenFromSequenz(seq: SequenzEintrag[]): number {
  return seq.length; // H already expanded to 2 entries in generateSequenz
}

interface Settings {
  modus: Modus;
  maschinenAktiv: Record<Maschine, boolean>;
  apiUrl: string;
  apiKey: string;
  customSequenzen: Record<'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4', CustomSequenz>;
  customLaeufe: Record<'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4', 1 | 2>;
  produkte: Produkt[];
  kioskMode: 'GAME' | 'CATERING';
  kioskPinHash: string | null;
  kioskPinSalt: string | null;
  kioskFailedAttempts: number;
  kioskLockoutUntil: number | null;
  autoSyncEnabled: boolean;
  autoSyncSeconds: number;
  billingSyncSeconds: number;
}

interface GameState extends Settings {
  screen: Screen;
  spieler: Spieler[];
  lineup: LineupEintrag[];
  /** Recoverable setup problem; never represents game results. */
  lineupWarning: string | null;

  // In-game tracking
  lauf: number;
  taubeIndex: number;  // which machine in sequenz
  spielerIndex: number; // which player is shooting within this taube (0..N-1)
  taubeGeworfen: boolean;
  sequenz: SequenzEintrag[];
  ergebnisse: Ergebnis[];
  confirmedLaunches: number;
  spielId: string | null;
  /** The exact USE events charged when the current game started. */
  activeGameCreditUses: KreditEvent[];

  // Portal player cache
  portalSpieler: PortalSpieler[];
  portalLaden: boolean;
  portalFehler: string | null;
  spielerAusCache: boolean;  // true when player list was loaded from offline cache

  // Offline queue
  pendingGames: PendingGame[];
  pendingSpieler: PendingSpieler[];
  spielerUpdates: SpielerUpdate[];

  syncStatus: SyncStatus;
  lastSync: string | null;
  lastFullSync: string | null;
  lastBillingSync: string | null;

  // Local game history (last 50, persisted)
  gameHistory: FinishedGame[];
  lastFinishedGame: FinishedGame | null;

  // Day credits (valid only for kreditDatum = today; no carryover)
  kreditDatum: string;
  kredite: Record<number, KreditStand>;
  pendingKredite: KreditEvent[];
  krediteLaden: boolean;

  // Product ledger (day-scoped totals are calculated from these immutable events)
  produkte: Produkt[];
  verkaufDatum: string;
  verkaeufe: VerkaufEvent[];
  pendingVerkaeufe: VerkaufEvent[];
  verkaeufeLaden: boolean;
  /** Cached authoritative report rows; pending events are layered at read time. */
  serverVerkaeufe: VerkaufReportRow[];
  daySummary: DaySummary | null;
  daySummaryLaden: boolean;
  pendingPayments: PaymentEvent[];
  paidBillCache: Record<number, DaySummary['players'][number]>;

  // Actions
  setScreen: (screen: Screen) => void;
  dismissResultate: () => void;
  setSpieler: (spieler: Spieler[]) => void;
  setSpielerAufPosten: (post: number, data: { id: number; name: string } | null) => void;
  reconcileLineupCredits: () => void;
  clearLineup: () => void;
  mixLineup: (random?: () => number) => void;
  moveLineup: (post: number, direction: -1 | 1) => void;
  updateSpielerName: (id: number, name: string) => void;
  setModus: (modus: Modus) => void;
  toggleMaschineAktiv: (m: Maschine) => void;
  setCustomSequenz: (modus: 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4', seq: CustomSequenz) => void;
  setCustomLaeufe: (modus: 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4', laeufe: 1 | 2) => void;
  startSpiel: () => void;
  eintragenErgebnis: (schuss1: boolean, schuss2: boolean) => void;
  wiederholenTaube: () => void;
  ueberspringenTaube: () => void;
  ofbriechenSpiel: () => boolean;
  werfenTaube: () => void;
  setApiSettings: (url: string, key: string) => void;
  setSyncSettings: (enabled: boolean, fullSeconds: number, billingSeconds: number) => boolean;
  /** Create a new local player (negative id) and queue them for portal upload on next sync */
  addLocalSpieler: (name: string) => PortalSpieler;
  syncAllPending: () => Promise<void>;
  syncBillingPending: () => Promise<void>;
  ladeSpielerVomPortal: () => Promise<void>;
  getAktivenSpieler: () => Spieler | undefined;
  saveSettings: () => void;

  /** Add a player to today's list with 0 credits (no event queued until credits are actually granted) */
  registerSpielerFuerTag: (spielerId: number) => void;
  /** Grant N day credits to a player (queued for portal sync) */
  addKredite: (spielerId: number, anzahl: number) => void;
  /** Manually refund/deduct N credits (player paid out early) */
  removeKredit: (spielerId: number, anzahl: number) => void;
  /** Remove a player's entire entry from today's credit list (mistake correction) */
  deleteKreditEntry: (spielerId: number) => void;
  /** Remaining credits for a player today (0 if none) */
  getKreditRest: (spielerId: number) => number;
  /** Pull today's credit state from the portal and merge with unsynced local events */
  ladeKredite: () => Promise<void>;
  addVerkauf: (spielerId: number, productId: number, quantity: number) => void;
  getProduktAnzahl: (spielerId: number, productId: number) => number;
  getProjectedDaySummary: (spielerId: number) => DaySummary['players'][number] | null;
  ladeProdukte: () => Promise<void>;
  ladeVerkaeufe: () => Promise<void>;
  ladeDaySummary: () => Promise<void>;
  markBillPaid: (spielerId: number) => void;

  /** Queue a player edit (name/email/portal activation) for the next sync */
  queueSpielerUpdate: (spielerId: number, changes: { name: string; email: string | null; portalAktiv: boolean }) => void;
  /** Queue a password reset for the next sync (server emails a new password) */
  queuePasswortReset: (spielerId: number) => void;
  /** Poll the portal for email status of already-synced changes */
  refreshSpielerUpdateStatus: () => Promise<void>;
  /** Remove finished (email_sent) entries from the change list */
  clearErledegtSpielerUpdates: () => void;

  setKioskMode: (mode: 'GAME' | 'CATERING') => void;
  setKioskPin: (oldPin: string | null, newPin: string | null) => Promise<{ success: boolean; error?: string }>;
  verifyKioskPin: (pin: string) => Promise<boolean>;
  queueCateringBasket: (spielerId: number, cart: Record<number, number>) => { success: boolean; error?: string };

  kioskFailedAttempts: number;
  kioskLockoutUntil: number | null;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

async function hashPin(pin: string, salt: string): Promise<string> {
  const encoder = new TextEncoder();
  const data = encoder.encode(pin + salt);
  const hashBuffer = await crypto.subtle.digest('SHA-256', data);
  const hashArray = Array.from(new Uint8Array(hashBuffer));
  return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
}

function generateSalt(): string {
  const array = new Uint8Array(16);
  crypto.getRandomValues(array);
  return Array.from(array).map(b => b.toString(16).padStart(2, '0')).join('');
}

const MASCHINEN: Maschine[] = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'];
export { MASCHINEN as MASCHINEN_LIST };

const DEFAULT_MASCHINEN_AKTIV: Record<Maschine, boolean> = {
  A: true, B: true, C: true, D: true, E: true, F: true, G: true, H: true,
};

const normalEntry = (maschine: Maschine): CustomSequenzEintrag =>
  maschine === 'H' ? { maschine, isDoublette: true } : { maschine };

const DEFAULT_CUSTOM: Record<'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4', CustomSequenz> = {
  CUSTOM_1: ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'].map(normalEntry),
  CUSTOM_2: ['A', 'C', 'E', 'G', 'B', 'D', 'F', 'H'].map(normalEntry),
  CUSTOM_3: ['H', 'G', 'F', 'E', 'D', 'C', 'B', 'A'].map(normalEntry),
  CUSTOM_4: ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'].map(normalEntry),
};

const DEFAULT_CUSTOM_LAEUFE: Record<'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4', 1 | 2> = {
  CUSTOM_1: 2,
  CUSTOM_2: 2,
  CUSTOM_3: 2,
  CUSTOM_4: 2,
};

function shuffleArray<T>(arr: T[]): T[] {
  const a = [...arr];
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}

function generateSequenz(
  modus: Modus,
  maschinenAktiv: Record<Maschine, boolean>,
  customSequenzen: Record<'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4', CustomSequenz>,
): SequenzEintrag[] {
  const single = (['A', 'B', 'C', 'D', 'E', 'F', 'G'] as Maschine[]).filter(m => maschinenAktiv[m]);
  const hAktiv = maschinenAktiv['H'];

  let order: CustomSequenz;

  if (modus === 'NORMAL') {
    order = [...single.map(normalEntry), ...(hAktiv ? [normalEntry('H')] : [])];
  } else if (modus === 'HARAKIRI') {
    // H is shuffled as one logical H1/H2 unit, not appended at the end.
    const units = [...single, ...(hAktiv ? (['H'] as Maschine[]) : [])];
    order = shuffleArray(units).map(normalEntry);
  } else {
    // CUSTOM modes: use the sequence exactly as defined — ignore maschinenAktiv
    const key = modus as 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4';
    order = customSequenzen[key];
  }

  // Expand each launch unit into scoring entries. The terminal/gateway fire
  // path sends only the pair's first entry; H itself remains one relay command.
  const result: SequenzEintrag[] = [];
  for (const entry of order) {
    if (entry.maschine === 'H') {
      result.push({ maschine: 'H', doubletteNr: 1, pairKind: 'h', partner: 'H' });
      result.push({ maschine: 'H', doubletteNr: 2, pairKind: 'h', partner: 'H' });
    } else if (entry.isDoublette && entry.partner && entry.partner !== 'H' &&
               entry.partner !== entry.maschine) {
      const delayMs = Math.round(Math.min(10, Math.max(0, entry.delaySeconds ?? 1)) * 1000);
      result.push({
        maschine: entry.maschine, doubletteNr: 1, pairKind: 'custom',
        partner: entry.partner, delayMs,
      });
      result.push({
        maschine: entry.partner, doubletteNr: 2, pairKind: 'custom',
        partner: entry.maschine, delayMs,
      });
    } else {
      result.push({ maschine: entry.maschine });
    }
  }
  return result;
}

function generateSpielId(): string {
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => {
    const r = Math.random() * 16 | 0;
    return (c === 'x' ? r : (r & 0x3 | 0x8)).toString(16);
  });
}

/** Current post for a player at a given taubeIndex (0-based) within the current lauf */
export function getCurrentPosten(spieler: Spieler, taubeIndex: number, totalSpieler: number): number {
  return ((spieler.startPosten - 1 + taubeIndex) % 5) + 1;
}

/**
 * Count H2 (doubletteNr === 2) entries at indices strictly before `before`.
 * H1 + H2 occupy two taubeIndex slots but represent ONE physical position.
 * Subtracting this count converts a raw taubeIndex into a logical position index,
 * so entries after an H doublette advance by +1 post, not +2.
 */
export function countH2Before(sequenz: SequenzEintrag[], before: number): number {
  let n = 0;
  for (let i = 0; i < before; i++) {
    if (sequenz[i].doubletteNr === 2) n++;
  }
  return n;
}

/** True for all three Harakiri variants — in those modes each post faces a different machine */
export function isHarakiriModus(modus: Modus): boolean {
  return modus === 'HARAKIRI';
}

/**
 * Returns the sequenz entry that a player at `posten` should shoot at step `taubeIndex`.
 * Normal: every player shoots the same entry (sequenz[taubeIndex]).
 * Harakiri: each post is offset, so player at post P shoots sequenz[(T + P - 1) % len].
 */
export function getEintragForPlayer(
  sequenz: SequenzEintrag[],
  taubeIndex: number,
  posten: number,
  modus: Modus,
): SequenzEintrag | undefined {
  if (!sequenz.length) return undefined;
  if (isHarakiriModus(modus)) {
    return sequenz[(taubeIndex + posten - 1) % sequenz.length];
  }
  return sequenz[taubeIndex];
}

/** Assign startPosten to players. Max 5 posts; player 6 gets startPosten=1 */
function assignPostenToSpieler(spieler: Spieler[]): Spieler[] {
  return spieler.map((s, i) => ({
    ...s,
    startPosten: (i % 5) + 1,
  }));
}

// ─── localStorage persistence ─────────────────────────────────────────────────

const SETTINGS_KEY = 'rangemaster-emulator-settings';
const SYNC_SETTINGS_KEY = 'rangemaster-emulator-sync-settings';
const PENDING_KEY = 'rangemaster-pending-games';
const CACHED_SPIELER_KEY = 'rangemaster-cached-spieler';
const HISTORY_KEY = 'rangemaster-game-history';
const PENDING_SPIELER_KEY = 'rangemaster-pending-spieler';
const KREDITE_KEY = 'rangemaster-kredite';
const PENDING_KREDITE_KEY = 'rangemaster-pending-kredite';
const SPIELER_UPDATES_KEY = 'rangemaster-spieler-updates';
const VERKAEUFE_KEY = 'rangemaster-verkaeufe';
const PENDING_VERKAEUFE_KEY = 'rangemaster-pending-verkaeufe';
const SERVER_VERKAEUFE_KEY = 'rangemaster-server-verkaeufe';
const PENDING_PAYMENTS_KEY = 'rangemaster-pending-payments';
const PAID_BILL_CACHE_KEY = 'rangemaster-paid-bill-cache';
const DAY_SUMMARY_KEY = 'rangemaster-day-summary';
const LINEUP_KEY = 'rangemaster-lineup';

function loadLineup(): LineupEintrag[] {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(LINEUP_KEY) : null;
    const entries = raw ? JSON.parse(raw) : [];
    if (!Array.isArray(entries)) return [];
    const used = new Set<number>(), posts = new Set<number>();
    return entries.filter((e): e is LineupEintrag =>
      !!e && Number.isInteger(e.spielerId) && e.spielerId !== 0 &&
      Number.isInteger(e.startPosten) && e.startPosten >= 1 && e.startPosten <= 6 &&
      !used.has(e.spielerId) && !posts.has(e.startPosten) &&
      !!used.add(e.spielerId) && !!posts.add(e.startPosten)
    ).sort((a, b) => a.startPosten - b.startPosten);
  } catch { return []; }
}
function saveLineup(lineup: LineupEintrag[]) {
  try { localStorage.setItem(LINEUP_KEY, JSON.stringify(lineup)); } catch {}
}

export function normalizeProdukte(raw: unknown): Produkt[] {
  if (!Array.isArray(raw)) return DEFAULT_PRODUKTE;
  const valid = raw.filter((p): p is Produkt => !!p && typeof p === 'object' &&
    typeof (p as Produkt).id === 'number' && typeof (p as Produkt).category === 'string');

  // The terminal always exposes these three fixed products, even when a stale
  // local settings file predates the product catalogue.
  const defaults = DEFAULT_PRODUKTE.map(defaultProduct =>
    valid.find(p => p.code === defaultProduct.code || p.category === defaultProduct.category) ?? defaultProduct
  );

  // Keep other valid products (like FOOD/DRINK)
  const defaultCodes = new Set(DEFAULT_PRODUKTE.map(p => p.code));
  const defaultCategories = new Set(DEFAULT_PRODUKTE.map(p => p.category));
  const others = valid.filter(p => p.code && !defaultCodes.has(p.code) && !defaultCategories.has(p.category));

  return [...defaults, ...others];
}

function loadVerkaeufe(key: string): VerkaufEvent[] {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(key) : null;
    return raw ? JSON.parse(raw) as VerkaufEvent[] : [];
  } catch { return []; }
}

function saveVerkaeufe(key: string, events: VerkaufEvent[]) {
  try { localStorage.setItem(key, JSON.stringify(events)); } catch {}
}

function loadPaidBillCache(datum: string): Record<number, DaySummary['players'][number]> {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(PAID_BILL_CACHE_KEY) : null;
    const parsed = raw ? JSON.parse(raw) as { datum?: unknown; players?: unknown } : null;
    if (!parsed || parsed.datum !== datum || !parsed.players || typeof parsed.players !== 'object') return {};
    return parsed.players as Record<number, DaySummary['players'][number]>;
  } catch { return {}; }
}

function savePaidBillCache(datum: string, players: Record<number, DaySummary['players'][number]>) {
  try { localStorage.setItem(PAID_BILL_CACHE_KEY, JSON.stringify({ datum, players })); } catch {}
}

function loadVerkaufsReport(): { datum: string; rows: VerkaufReportRow[] } {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(SERVER_VERKAEUFE_KEY) : null;
    return raw ? JSON.parse(raw) as { datum: string; rows: VerkaufReportRow[] } : { datum: '', rows: [] };
  } catch { return { datum: '', rows: [] }; }
}

function saveVerkaufsReport(datum: string, rows: VerkaufReportRow[]) {
  try { localStorage.setItem(SERVER_VERKAEUFE_KEY, JSON.stringify({ datum, rows })); } catch {}
}

function loadPendingPayments(): PaymentEvent[] {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(PENDING_PAYMENTS_KEY) : null;
    return raw ? JSON.parse(raw) as PaymentEvent[] : [];
  } catch { return []; }
}

function savePendingPayments(events: PaymentEvent[]) {
  try { localStorage.setItem(PENDING_PAYMENTS_KEY, JSON.stringify(events)); } catch {}
}

function loadDaySummary(): DaySummary | null {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(DAY_SUMMARY_KEY) : null;
    return raw ? JSON.parse(raw) as DaySummary : null;
  } catch { return null; }
}

function saveDaySummary(summary: DaySummary) {
  try { localStorage.setItem(DAY_SUMMARY_KEY, JSON.stringify(summary)); } catch {}
}

function loadSpielerUpdates(): SpielerUpdate[] {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(SPIELER_UPDATES_KEY) : null;
    if (!raw) return [];
    return JSON.parse(raw) as SpielerUpdate[];
  } catch {
    return [];
  }
}

function saveSpielerUpdates(updates: SpielerUpdate[]) {
  try {
    localStorage.setItem(SPIELER_UPDATES_KEY, JSON.stringify(updates));
  } catch {}
}

/** Local date (not UTC) — day credits expire on the local day boundary */
export function todayStr(): string {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
}

function loadKredite(): { kreditDatum: string; kredite: Record<number, KreditStand> } {
  const today = todayStr();
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(KREDITE_KEY) : null;
    if (raw) {
      const parsed = JSON.parse(raw) as { datum: string; kredite: Record<number, KreditStand> };
      // Credits are day-scoped: discard anything not from today (no carryover)
      if (parsed.datum === today) return { kreditDatum: today, kredite: parsed.kredite ?? {} };
    }
  } catch {}
  return { kreditDatum: today, kredite: {} };
}

function saveKredite(datum: string, kredite: Record<number, KreditStand>) {
  try {
    localStorage.setItem(KREDITE_KEY, JSON.stringify({ datum, kredite }));
  } catch {}
}

function loadPendingKredite(): KreditEvent[] {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(PENDING_KREDITE_KEY) : null;
    if (!raw) return [];
    return JSON.parse(raw) as KreditEvent[];
  } catch {
    return [];
  }
}

function savePendingKredite(events: KreditEvent[]) {
  try {
    localStorage.setItem(PENDING_KREDITE_KEY, JSON.stringify(events));
  } catch {}
}

/** If the stored credit day is stale, roll over to an empty tally for today */
function rolledKredite(state: { kreditDatum: string; kredite: Record<number, KreditStand> }) {
  const today = todayStr();
  if (state.kreditDatum === today) return { kreditDatum: state.kreditDatum, kredite: state.kredite };
  return { kreditDatum: today, kredite: {} as Record<number, KreditStand> };
}

function loadPendingSpieler(): PendingSpieler[] {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(PENDING_SPIELER_KEY) : null;
    if (!raw) return [];
    return JSON.parse(raw) as PendingSpieler[];
  } catch {
    return [];
  }
}

function savePendingSpieler(spieler: PendingSpieler[]) {
  try {
    localStorage.setItem(PENDING_SPIELER_KEY, JSON.stringify(spieler));
  } catch {}
}

function loadGameHistory(): FinishedGame[] {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(HISTORY_KEY) : null;
    if (!raw) return [];
    return JSON.parse(raw) as FinishedGame[];
  } catch {
    return [];
  }
}

function saveGameHistory(history: FinishedGame[]) {
  try {
    localStorage.setItem(HISTORY_KEY, JSON.stringify(history));
  } catch {}
}

function loadSettings(): Partial<Settings> {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(SETTINGS_KEY) : null;
    const fromStorage: Partial<Settings> = raw ? (JSON.parse(raw) as Partial<Settings>) : {};

    // URL params override localStorage — allows bookmarking a pre-configured emulator:
    //   /emulator?apiKey=abc123&apiUrl=https://rangemaster.hostzone.lu/api
    const params = typeof window !== 'undefined' ? new URLSearchParams(window.location.search) : null;
    const urlApiKey = params?.get('apiKey');
    const urlApiUrl = params?.get('apiUrl');

    if (urlApiKey || urlApiUrl) {
      if (urlApiKey) fromStorage.apiKey = urlApiKey;
      if (urlApiUrl) fromStorage.apiUrl = urlApiUrl;
      // Persist so the next plain reload (without params) still works
      try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(fromStorage)); } catch {}
    }

    return fromStorage;
  } catch {
    return {};
  }
}

function saveToStorage(settings: Partial<Settings>) {
  try {
    // Legacy settings actions only update game/network preferences. Retain the
    // independently edited catalogue instead of accidentally dropping it.
    const existing = loadSettings();
    localStorage.setItem(SETTINGS_KEY, JSON.stringify({
      ...existing,
      ...settings,
      produkte: settings.produkte ?? normalizeProdukte(loadSettings().produkte),
    }));
  } catch {}
}

function loadSyncSettings(): Partial<Pick<Settings, 'autoSyncEnabled' | 'autoSyncSeconds' | 'billingSyncSeconds'>> {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(SYNC_SETTINGS_KEY) : null;
    return raw ? JSON.parse(raw) : {};
  } catch {
    return {};
  }
}

function saveSyncSettings(enabled: boolean, fullSeconds: number, billingSeconds: number) {
  try {
    localStorage.setItem(SYNC_SETTINGS_KEY, JSON.stringify({
      autoSyncEnabled: enabled,
      autoSyncSeconds: fullSeconds,
      billingSyncSeconds: billingSeconds,
    }));
  } catch {}
}

type SyncKind = 'full' | 'billing';
let activeSync: Promise<void> | null = null;
let queuedFullSync = false;
let queuedBillingSync = false;

async function runSerializedSync(kind: SyncKind, execute: (kind: SyncKind) => Promise<void>) {
  if (activeSync) {
    if (kind === 'full') queuedFullSync = true;
    else queuedBillingSync = true;
    return activeSync;
  }

  activeSync = execute(kind).finally(() => {
    activeSync = null;
    if (queuedFullSync) {
      queuedFullSync = false;
      queuedBillingSync = false;
      window.setTimeout(() => void useGameStore.getState().syncAllPending(), 0);
    } else if (queuedBillingSync) {
      queuedBillingSync = false;
      window.setTimeout(() => void useGameStore.getState().syncBillingPending(), 0);
    }
  });
  return activeSync;
}

function loadPendingGames(): PendingGame[] {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(PENDING_KEY) : null;
    if (!raw) return [];
    return JSON.parse(raw) as PendingGame[];
  } catch {
    return [];
  }
}

function savePendingGames(games: PendingGame[]) {
  try {
    localStorage.setItem(PENDING_KEY, JSON.stringify(games));
  } catch {}
}

function loadCachedSpieler(): PortalSpieler[] {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(CACHED_SPIELER_KEY) : null;
    if (!raw) return [];
    return JSON.parse(raw) as PortalSpieler[];
  } catch {
    return [];
  }
}

function saveCachedSpieler(spieler: PortalSpieler[]) {
  try {
    localStorage.setItem(CACHED_SPIELER_KEY, JSON.stringify(spieler));
  } catch {}
}

function normalizeCustomEntry(value: unknown): CustomSequenzEintrag | null {
  if (typeof value === 'string' && MASCHINEN.includes(value as Maschine)) {
    return normalEntry(value as Maschine);
  }
  if (!value || typeof value !== 'object') return null;
  const raw = value as Partial<CustomSequenzEintrag>;
  if (!raw.maschine || !MASCHINEN.includes(raw.maschine)) return null;
  if (raw.maschine === 'H') return { maschine: 'H', isDoublette: true };
  const validPartner = raw.partner && raw.partner !== 'H' &&
    raw.partner !== raw.maschine && MASCHINEN.includes(raw.partner);
  if (!raw.isDoublette || !validPartner) return { maschine: raw.maschine };
  return {
    maschine: raw.maschine,
    partner: raw.partner,
    isDoublette: true,
    delaySeconds: Math.min(10, Math.max(0, Number(raw.delaySeconds ?? 1) || 1)),
  };
}

function normalizeCustomSequenzen(
  raw: Partial<Settings>['customSequenzen'],
): Record<'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4', CustomSequenz> {
  const result = { ...DEFAULT_CUSTOM };
  for (const key of Object.keys(DEFAULT_CUSTOM) as Array<keyof typeof DEFAULT_CUSTOM>) {
    const source = raw?.[key];
    if (!Array.isArray(source)) continue;
    result[key] = source
      .map(normalizeCustomEntry)
      .filter((entry): entry is CustomSequenzEintrag => entry !== null)
      .slice(0, 16);
  }
  return result;
}

// ─── Store ────────────────────────────────────────────────────────────────────

const saved = loadSettings();
const savedSyncSettings = loadSyncSettings();
const savedVerkaufsReport = loadVerkaufsReport();

const INIT_SPIELER: Spieler[] = [];
const INITIAL_LINEUP = loadLineup();
const INITIAL_PENDING_SPIELER = loadPendingSpieler();
const INITIAL_CACHED_SPIELER = loadCachedSpieler();
const INITIAL_PORTAL_SPIELER: PortalSpieler[] = [
  ...INITIAL_CACHED_SPIELER,
  ...INITIAL_PENDING_SPIELER
    .filter(p => !INITIAL_CACHED_SPIELER.some(c => c.id === p.localId))
    .map(p => ({ id: p.localId, name: p.name, mitgliedNr: null, lokal: true })),
];

export const useGameStore = create<GameState>((set, get) => {
  const modus: Modus = saved.modus ?? 'NORMAL';
  const maschinenAktiv = saved.maschinenAktiv ?? { ...DEFAULT_MASCHINEN_AKTIV };
  const apiUrl: string = saved.apiUrl ?? '';
  const apiKey: string = saved.apiKey ?? '';
  const customSequenzen = normalizeCustomSequenzen(saved.customSequenzen);
  const customLaeufe = saved.customLaeufe ?? { ...DEFAULT_CUSTOM_LAEUFE };
  const produkte = normalizeProdukte(saved.produkte);
  const kioskMode = saved.kioskMode ?? 'GAME';
  const kioskPinHash = saved.kioskPinHash ?? null;
  const kioskPinSalt = saved.kioskPinSalt ?? null;
  const persistedAutoSyncEnabled = savedSyncSettings.autoSyncEnabled ?? saved.autoSyncEnabled;
  const persistedAutoSyncSeconds = savedSyncSettings.autoSyncSeconds ?? saved.autoSyncSeconds;
  const persistedBillingSyncSeconds = savedSyncSettings.billingSyncSeconds ?? saved.billingSyncSeconds;
  const autoSyncEnabled = persistedAutoSyncEnabled ?? true;
  const autoSyncSeconds = Number.isInteger(persistedAutoSyncSeconds) &&
    (persistedAutoSyncSeconds ?? 0) >= 10 && (persistedAutoSyncSeconds ?? 0) <= 86400
      ? persistedAutoSyncSeconds! : AUTO_SYNC_DEFAULT_SECONDS;
  const billingSyncSeconds = Number.isInteger(persistedBillingSyncSeconds) &&
    (persistedBillingSyncSeconds ?? 0) >= BILLING_SYNC_MIN_SECONDS &&
    (persistedBillingSyncSeconds ?? 0) <= BILLING_SYNC_MAX_SECONDS
      ? persistedBillingSyncSeconds! : BILLING_SYNC_DEFAULT_SECONDS;
  let kioskFailedAttempts = saved.kioskFailedAttempts ?? 0;
  let kioskLockoutUntil = saved.kioskLockoutUntil ?? null;

  // Safely clear expired timestamps
  if (kioskLockoutUntil && Date.now() > kioskLockoutUntil) {
    kioskFailedAttempts = 0;
    kioskLockoutUntil = null;
  }

  return {
    // Settings (persisted)
    modus,
    maschinenAktiv,
    apiUrl,
    apiKey,
    customSequenzen,
    customLaeufe,
    produkte,
    kioskMode,
    kioskPinHash,
    kioskPinSalt,
    kioskFailedAttempts,
    kioskLockoutUntil,
    autoSyncEnabled,
    autoSyncSeconds,
    billingSyncSeconds,

    // Volatile
    screen: 'dashboard',
    spieler: INIT_SPIELER,
    lineup: INITIAL_LINEUP,
    lineupWarning: null,
    lauf: 1,
    taubeIndex: 0,
    spielerIndex: 0,
    taubeGeworfen: false,
    sequenz: generateSequenz(modus, maschinenAktiv, customSequenzen),
    ergebnisse: [],
    confirmedLaunches: 0,
    spielId: null,
    activeGameCreditUses: [],
    // Render the persisted lineup immediately after a reload; authoritative
    // fetches still reconcile this cache later.
    portalSpieler: INITIAL_PORTAL_SPIELER,
    portalLaden: false,
    portalFehler: null,
    spielerAusCache: false,

    // Offline queue — restored from localStorage on startup
    pendingGames: loadPendingGames(),
    pendingSpieler: INITIAL_PENDING_SPIELER,
    spielerUpdates: loadSpielerUpdates(),

    // Local history
    gameHistory: loadGameHistory(),
    lastFinishedGame: null,

    // Day credits — restored from localStorage (auto-expired if from another day)
    ...loadKredite(),
    pendingKredite: loadPendingKredite(),
    krediteLaden: false,
    verkaufDatum: todayStr(),
    verkaeufe: loadVerkaeufe(VERKAEUFE_KEY).filter(e => e.datum === todayStr()),
    pendingVerkaeufe: loadVerkaeufe(PENDING_VERKAEUFE_KEY),
    verkaeufeLaden: false,
    serverVerkaeufe: savedVerkaufsReport.datum === todayStr() ? savedVerkaufsReport.rows : [],
    daySummary: loadDaySummary(),
    daySummaryLaden: false,
    pendingPayments: loadPendingPayments(),
    paidBillCache: loadPaidBillCache(todayStr()),

    syncStatus: 'idle',
    lastSync: null,
    lastFullSync: null,
    lastBillingSync: null,

    saveSettings: () => {
      const s = get();
      saveToStorage({
        modus: s.modus,
        maschinenAktiv: s.maschinenAktiv,
        apiUrl: s.apiUrl,
        apiKey: s.apiKey,
        customSequenzen: s.customSequenzen,
        customLaeufe: s.customLaeufe,
        produkte: s.produkte,
        kioskMode: s.kioskMode,
        kioskPinHash: s.kioskPinHash,
        kioskPinSalt: s.kioskPinSalt,
        kioskFailedAttempts: s.kioskFailedAttempts,
        kioskLockoutUntil: s.kioskLockoutUntil,
        autoSyncEnabled: s.autoSyncEnabled,
        autoSyncSeconds: s.autoSyncSeconds,
        billingSyncSeconds: s.billingSyncSeconds,
      });
      saveSyncSettings(s.autoSyncEnabled, s.autoSyncSeconds, s.billingSyncSeconds);
    },

    setScreen: (screen) => set({ screen }),

    setSpieler: (spieler) => set({ spieler }),

    setSpielerAufPosten: (post, data) => set((state) => {
      if (!Number.isInteger(post) || post < 1 || post > 6) return state;
      // Remove any existing occupant at this post
      let lineup = state.lineup.filter(s => s.startPosten !== post);
      if (data !== null) {
        // Also remove this player from any other post they were assigned to
        lineup = lineup.filter(s => s.spielerId !== data.id);
        lineup = [...lineup, { spielerId: data.id, startPosten: post }];
      }
      lineup.sort((a, b) => a.startPosten - b.startPosten);
      saveLineup(lineup);
      const playersById = new Map(state.portalSpieler.map(p => [p.id, p.name]));
      if (data) playersById.set(data.id, data.name);
      return { lineup, lineupWarning: null, spieler: lineup.map(l => ({
        id: l.spielerId, name: playersById.get(l.spielerId) ?? 'Onbekannt',
        punkte: 0, startPosten: l.startPosten,
      })) };
    }),

    reconcileLineupCredits: () => set((state) => {
      const lineup = state.lineup.filter(entry => state.getKreditRest(entry.spielerId) > 0);
      if (lineup.length === state.lineup.length) return state;
      saveLineup(lineup);
      return { lineup };
    }),

    clearLineup: () => set(() => { saveLineup([]); return { lineup: [], spieler: [], lineupWarning: null }; }),

    mixLineup: (random = Math.random) => set((state) => {
      const lineup = [...state.lineup].sort((a, b) => a.startPosten - b.startPosten);
      // Fisher-Yates: inject a source for deterministic unit tests.
      for (let i = lineup.length - 1; i > 0; i--) {
        const j = Math.floor(random() * (i + 1));
        [lineup[i], lineup[j]] = [lineup[j], lineup[i]];
      }
      const mixed = lineup.map((entry, index) => ({ ...entry, startPosten: index + 1 }));
      saveLineup(mixed);
      const names = new Map(state.spieler.map(p => [p.id, p.name]));
      return { lineup: mixed, spieler: mixed.map(l => ({ id: l.spielerId, name: names.get(l.spielerId) ?? 'Onbekannt', punkte: 0, startPosten: l.startPosten })) };
    }),

    moveLineup: (post, direction) => set((state) => {
      const target = post + direction;
      if (post < 1 || post > 6 || target < 1 || target > 6) return state;
      const lineup = state.lineup.map(entry => {
        if (entry.startPosten === post) return { ...entry, startPosten: target };
        if (entry.startPosten === target) return { ...entry, startPosten: post };
        return entry;
      }).sort((a, b) => a.startPosten - b.startPosten);
      saveLineup(lineup);
      const names = new Map(state.spieler.map(p => [p.id, p.name]));
      return { lineup, spieler: lineup.map(l => ({ id: l.spielerId, name: names.get(l.spielerId) ?? 'Onbekannt', punkte: 0, startPosten: l.startPosten })) };
    }),

    updateSpielerName: (id, name) => set((state) => ({
      spieler: state.spieler.map(s => s.id === id ? { ...s, name } : s),
    })),

    setModus: (modus) => {
      set({ modus });
      const s = get();
      saveToStorage({ modus, maschinenAktiv: s.maschinenAktiv, apiUrl: s.apiUrl, apiKey: s.apiKey, customSequenzen: s.customSequenzen, customLaeufe: s.customLaeufe, kioskMode: s.kioskMode, kioskPinHash: s.kioskPinHash, kioskPinSalt: s.kioskPinSalt, kioskFailedAttempts: s.kioskFailedAttempts, kioskLockoutUntil: s.kioskLockoutUntil });
    },

    toggleMaschineAktiv: (m) => {
      set((state) => {
        const maschinenAktiv = { ...state.maschinenAktiv, [m]: !state.maschinenAktiv[m] };
        saveToStorage({ modus: state.modus, maschinenAktiv, apiUrl: state.apiUrl, apiKey: state.apiKey, customSequenzen: state.customSequenzen, customLaeufe: state.customLaeufe, kioskMode: state.kioskMode, kioskPinHash: state.kioskPinHash, kioskPinSalt: state.kioskPinSalt, kioskFailedAttempts: state.kioskFailedAttempts, kioskLockoutUntil: state.kioskLockoutUntil });
        return { maschinenAktiv };
      });
    },

    setCustomSequenz: (modus, seq) => {
      set((state) => {
        const customSequenzen = { ...state.customSequenzen, [modus]: seq };
        saveToStorage({ modus: state.modus, maschinenAktiv: state.maschinenAktiv, apiUrl: state.apiUrl, apiKey: state.apiKey, customSequenzen, customLaeufe: state.customLaeufe, kioskMode: state.kioskMode, kioskPinHash: state.kioskPinHash, kioskPinSalt: state.kioskPinSalt, kioskFailedAttempts: state.kioskFailedAttempts, kioskLockoutUntil: state.kioskLockoutUntil });
        return { customSequenzen };
      });
    },

    setCustomLaeufe: (modus, laeufe) => {
      set((state) => {
        const customLaeufe = { ...state.customLaeufe, [modus]: laeufe };
        saveToStorage({ modus: state.modus, maschinenAktiv: state.maschinenAktiv, apiUrl: state.apiUrl, apiKey: state.apiKey, customSequenzen: state.customSequenzen, customLaeufe, kioskMode: state.kioskMode, kioskPinHash: state.kioskPinHash, kioskPinSalt: state.kioskPinSalt, kioskFailedAttempts: state.kioskFailedAttempts, kioskLockoutUntil: state.kioskLockoutUntil });
        return { customLaeufe };
      });
    },

    startSpiel: () => set((state) => {
      const portalNames = new Map(state.portalSpieler.map(p => [p.id, p.name]));
      const seenPosts = new Set<number>();
      const seenIds = new Set<number>();
      for (const entry of state.lineup) {
        if (!Number.isInteger(entry.spielerId) || entry.spielerId === 0 ||
            !Number.isInteger(entry.startPosten) || entry.startPosten < 1 || entry.startPosten > 6 ||
            seenPosts.has(entry.startPosten) || seenIds.has(entry.spielerId))
          return { lineupWarning: 'D Opstellung ass ongülteg. Kontrolléiert d Posten.' };
        seenPosts.add(entry.startPosten); seenIds.add(entry.spielerId);
        if (!portalNames.has(entry.spielerId))
          return { lineupWarning: 'E Spiller an der Opstellung ass net méi disponibel. Wielt en nei.' };
        if (state.getKreditRest(entry.spielerId) <= 0)
          return { lineupWarning: `${portalNames.get(entry.spielerId)} huet keng Dageskreditter.` };
      }
      const lineupSpieler = [...state.lineup]
        .sort((a, b) => a.startPosten - b.startPosten)
        .map(entry => ({
          id: entry.spielerId,
          name: portalNames.get(entry.spielerId) ??
            state.spieler.find(s => s.id === entry.spielerId)?.name ?? 'Onbekannt',
          punkte: 0,
          startPosten: entry.startPosten,
        }));
      if (!lineupSpieler.length)
        return { lineupWarning: 'Mindestens ee Spiller auswielen.' };
      const gameCredit = state.produkte.find(product =>
        (product.code === PRODUKT_IDS.kredit || product.category === PRODUKT_IDS.kredit) &&
        product.id > 0 && product.currentPrice
      );
      if (!gameCredit?.currentPrice) {
        return { lineupWarning: 'De Präis fir Game credits ass net konfiguréiert.' };
      }
      // ── Deduct one day credit per participating player ────────────────────
      const { kreditDatum, kredite } = rolledKredite(state);
      if (state.pendingKredite.length + lineupSpieler.length >
          MAX_PENDING_KREDIT_EVENTS) {
        // Match the terminal: do not begin a game unless every USE event can
        // be kept for a safe retry or a future cancellation refund.
        return state;
      }
      const nextKredite = { ...kredite };
      const newEvents: KreditEvent[] = [];
      const occurredAt = new Date().toISOString();
      for (const s of lineupSpieler) {
        const stand = nextKredite[s.id] ?? { gewaehrt: 0, verbraucht: 0 };
        nextKredite[s.id] = { ...stand, verbraucht: stand.verbraucht + 1 };
        newEvents.push({
          externalId: generateSpielId(),
          spielerId: s.id,
          datum: kreditDatum,
          typ: 'USE',
          anzahl: 1,
          occurredAt,
          priceRevisionId: gameCredit.currentPrice.id,
          unitPriceCents: gameCredit.currentPrice.unitPriceCents,
        });
      }
      const pendingKredite = [...state.pendingKredite, ...newEvents];
      saveKredite(kreditDatum, nextKredite);
      savePendingKredite(pendingKredite);

      return {
        screen: 'spiel' as Screen,
        lauf: 1,
        taubeIndex: 0,
        spielerIndex: 0,
        taubeGeworfen: false,
        ergebnisse: [],
        sequenz: generateSequenz(state.modus, state.maschinenAktiv, state.customSequenzen),
        // Ensure consistent game order by startPosten; reset points
        spieler: lineupSpieler,
        spielId: generateSpielId(),
        syncStatus: 'idle' as SyncStatus,
        kreditDatum,
        kredite: nextKredite,
        pendingKredite,
        activeGameCreditUses: newEvents,
        lineupWarning: null,
      };
    }),

    eintragenErgebnis: (schuss1, schuss2) => set((state) => {
      const pts = schuss1 ? 2 : schuss2 ? 1 : 0;
      const currentSpieler = state.spieler[state.spielerIndex];
      if (!currentSpieler) return state;

      // Look up the machine for this player — in Harakiri each post offsets into the sequenz
      const rawPosten = getCurrentPosten(currentSpieler, state.taubeIndex, state.spieler.length);
      const eintrag = getEintragForPlayer(state.sequenz, state.taubeIndex, rawPosten, state.modus);

      // In Normal mode, H2 fires from the same post as H1 (no post advance between H1→H2).
      // In Harakiri, each step advances posts naturally — no adjustment needed.
      const effectiveTaubeIdx =
        !isHarakiriModus(state.modus) && eintrag?.doubletteNr === 2
          ? state.taubeIndex - 1
          : state.taubeIndex;
      // H1+H2 together = ONE physical position step. Subtract H2 entries seen before
      // this slot so entries after an H advance by +1 post, not +2.
      const h2Offset = isHarakiriModus(state.modus) ? 0 : countH2Before(state.sequenz, effectiveTaubeIdx);
      const posten = getCurrentPosten(currentSpieler, effectiveTaubeIdx - h2Offset, state.spieler.length);

      if (!eintrag) return state;

      const neuesErgebnis: Ergebnis = {
        spielerId: currentSpieler.id,
        lauf: state.lauf,
        taube: state.taubeIndex + 1,
        maschine: eintrag.maschine,
        posten,
        schuss1,
        schuss2,
        punkte: pts,
        wiederholt: false,
      };

      const updatedSpieler = state.spieler.map(s =>
        s.id === currentSpieler.id ? { ...s, punkte: s.punkte + pts } : s
      );

      // Advance: next player first, then next taube when all players done
      const nextSpielerIndex = state.spielerIndex + 1;

      if (nextSpielerIndex < state.spieler.length) {
        // Same taube, next player
        return {
          ergebnisse: [...state.ergebnisse, neuesErgebnis],
          taubeGeworfen: false,
          spielerIndex: nextSpielerIndex,
          spieler: updatedSpieler,
        };
      }

      // All players shot this taube → advance taube
      const nextTaubeIndex = state.taubeIndex + 1;

      if (nextTaubeIndex < state.sequenz.length) {
        return {
          ergebnisse: [...state.ergebnisse, neuesErgebnis],
          taubeGeworfen: false,
          spielerIndex: 0,
          taubeIndex: nextTaubeIndex,
          spieler: updatedSpieler,
        };
      }

      // Lauf complete
      const nextLauf = state.lauf + 1;
      const maxLaeufe: number =
        (state.modus === 'CUSTOM_1' || state.modus === 'CUSTOM_2' || state.modus === 'CUSTOM_3' || state.modus === 'CUSTOM_4')
          ? state.customLaeufe[state.modus]
          : 2;
      if (nextLauf > maxLaeufe) {
        // ── Game over: enqueue in offline pending list ──────────────────────
        const allErgebnisse = [...state.ergebnisse, neuesErgebnis];

        // Compute per-lauf points for each player (needed for spiel_teilnahmen rows)
        const laufNummern = Array.from({ length: maxLaeufe }, (_, i) => i + 1);
        const teilnahmen = updatedSpieler.flatMap(s =>
          laufNummern.map(l => ({
            spielerId: s.id,
            startPosten: s.startPosten,
            punkte: allErgebnisse
              .filter(e => e.spielerId === s.id && e.lauf === l)
              .reduce((sum, e) => sum + e.punkte, 0),
            lauf: l,
          }))
        );

        const newPendingGame: PendingGame = {
          externalId: state.spielId!,
          datum: new Date().toISOString(),
          modus: state.modus,
          lauf: maxLaeufe,
          taubenProLauf: state.sequenz.length,
          abgeschlossen: true,
          teilnahmen,
          ergebnisse: allErgebnisse,
          confirmedLaunches: state.confirmedLaunches,
        };

        const newPendingGames = [...state.pendingGames, newPendingGame];
        savePendingGames(newPendingGames);

        // Build and persist local history entry
        const finishedGame: FinishedGame = {
          ...newPendingGame,
          finishedAt: new Date().toISOString(),
          spielerNamen: Object.fromEntries(updatedSpieler.map(s => [s.id, s.name])),
        };
        const newHistory = [finishedGame, ...state.gameHistory].slice(0, 50);
        saveGameHistory(newHistory);
        const nextLineup = state.lineup.filter(entry => state.getKreditRest(entry.spielerId) > 0);
        if (nextLineup.length !== state.lineup.length) saveLineup(nextLineup);

        return {
          ergebnisse: allErgebnisse,
          spielerIndex: 0,
          taubeIndex: 0,
          lauf: nextLauf,
          screen: 'resultate' as Screen,
          spieler: updatedSpieler,
          pendingGames: newPendingGames,
          syncStatus: 'idle',
          gameHistory: newHistory,
          lastFinishedGame: finishedGame,
          activeGameCreditUses: [],
          lineup: nextLineup,
        };
      }

      return {
        ergebnisse: [...state.ergebnisse, neuesErgebnis],
        taubeGeworfen: false,
        spielerIndex: 0,
        taubeIndex: 0,
        lauf: nextLauf,
        sequenz: generateSequenz(state.modus, state.maschinenAktiv, state.customSequenzen),
        spieler: updatedSpieler,
      };
    }),

    wiederholenTaube: () => set((state) => {
      // Step back one player (or one full taube if at start of taube)
      if (state.ergebnisse.length === 0) return state;
      const lastErgebnis = state.ergebnisse[state.ergebnisse.length - 1];
      const prevSpielerIndex = state.spielerIndex > 0
        ? state.spielerIndex - 1
        : state.spieler.length - 1;
      const prevTaubeIndex = state.spielerIndex > 0
        ? state.taubeIndex
        : Math.max(0, state.taubeIndex - 1);
      return {
        ergebnisse: state.ergebnisse.slice(0, -1),
        taubeGeworfen: false,
        spielerIndex: prevSpielerIndex,
        taubeIndex: prevTaubeIndex,
        spieler: state.spieler.map(s =>
          s.id === lastErgebnis.spielerId
            ? { ...s, punkte: Math.max(0, s.punkte - lastErgebnis.punkte) }
            : s
        ),
      };
    }),

    ueberspringenTaube: () => set((state) => {
      // Skip current player's turn
      const nextSpielerIndex = state.spielerIndex + 1;
      if (nextSpielerIndex < state.spieler.length) {
        return { spielerIndex: nextSpielerIndex, taubeGeworfen: false };
      }
      const nextTaubeIndex = state.taubeIndex + 1;
      if (nextTaubeIndex < state.sequenz.length) {
        return { spielerIndex: 0, taubeIndex: nextTaubeIndex, taubeGeworfen: false };
      }
      const nextLauf = state.lauf + 1;
      const maxLaeufe: number =
        (state.modus === 'CUSTOM_1' || state.modus === 'CUSTOM_2' || state.modus === 'CUSTOM_3' || state.modus === 'CUSTOM_4')
          ? state.customLaeufe[state.modus]
          : 2;
      if (nextLauf > maxLaeufe) {
        // ueberspringen path: skip showing resultate, just go to dashboard
        return { screen: 'dashboard' as Screen, spielerIndex: 0, taubeIndex: 0, lauf: nextLauf, taubeGeworfen: false };
      }
      return {
        spielerIndex: 0,
        taubeIndex: 0,
        lauf: nextLauf,
        taubeGeworfen: false,
        sequenz: generateSequenz(state.modus, state.maschinenAktiv, state.customSequenzen),
      };
    }),

    dismissResultate: () => set({ screen: 'dashboard', lastFinishedGame: null }),

    ofbriechenSpiel: () => {
      let canceled = false;
      set((state) => {
        const { kreditDatum, kredite } = rolledKredite(state);
        const activeUses = state.activeGameCreditUses;
        const pendingUseIds = new Set(state.pendingKredite.map(event => event.externalId));
        const removableUseIds = new Set(activeUses
          .filter(event => pendingUseIds.has(event.externalId) &&
            !kreditEventsInFlight.has(event.externalId))
          .map(event => event.externalId));
        const grantsNeeded = activeUses.filter(event =>
          !removableUseIds.has(event.externalId)).length;

        // Mirror the terminal's bounded queue. Do not leave the active game
        // until every required portal correction can be kept durably.
        if (state.pendingKredite.length - removableUseIds.size + grantsNeeded >
            MAX_PENDING_KREDIT_EVENTS) {
          return state;
        }

        const pendingKredite = state.pendingKredite.filter(
          event => !removableUseIds.has(event.externalId),
        );
        const nextKredite = { ...kredite };
        for (const use of activeUses) {
          const stand = nextKredite[use.spielerId] ?? { gewaehrt: 0, verbraucht: 0 };
          nextKredite[use.spielerId] = {
            ...stand,
            verbraucht: Math.max(0, stand.verbraucht - use.anzahl),
          };

          // A USE in an in-flight POST may already have reached the portal.
          // Keep it and queue a GRANT so either POST outcome restores balance.
          if (!removableUseIds.has(use.externalId)) {
            pendingKredite.push({
              externalId: generateSpielId(),
              spielerId: use.spielerId,
              datum: use.datum,
              typ: 'GRANT',
              anzahl: use.anzahl,
            });
          }
        }

        saveKredite(kreditDatum, nextKredite);
        savePendingKredite(pendingKredite);
        canceled = true;
        return {
          screen: 'dashboard' as Screen,
          spieler: [],
          lauf: 1,
          taubeIndex: 0,
          spielerIndex: 0,
          sequenz: [],
          ergebnisse: [],
          confirmedLaunches: 0,
          spielId: null,
          activeGameCreditUses: [],
          kreditDatum,
          kredite: nextKredite,
          pendingKredite,
        };
      });
      return canceled;
    },

    werfenTaube: () => set((state) => {
      // In the emulator we simulate the trap gateway ACK immediately on press.
      if (state.screen !== 'spiel' || state.taubeGeworfen) return state;
      const currentSpieler = state.spieler[state.spielerIndex];
      if (!currentSpieler) return state;
      const rawPosten = getCurrentPosten(currentSpieler, state.taubeIndex, state.spieler.length);
      const eintrag = getEintragForPlayer(state.sequenz, state.taubeIndex, rawPosten, state.modus);

      let clays = 1;
      if (eintrag && eintrag.doubletteNr === 1) clays = 2;

      return {
        taubeGeworfen: true,
        confirmedLaunches: state.confirmedLaunches + clays,
      };
    }),

    setApiSettings: (apiUrl, apiKey) => {
      set({ apiUrl, apiKey });
      const s = get();
      saveToStorage({ modus: s.modus, maschinenAktiv: s.maschinenAktiv, apiUrl, apiKey, customSequenzen: s.customSequenzen, customLaeufe: s.customLaeufe, kioskMode: s.kioskMode, kioskPinHash: s.kioskPinHash, kioskPinSalt: s.kioskPinSalt, kioskFailedAttempts: s.kioskFailedAttempts, kioskLockoutUntil: s.kioskLockoutUntil });
    },

    setSyncSettings: (enabled, fullSeconds, billingSeconds) => {
      if (!Number.isInteger(fullSeconds) || fullSeconds < 10 || fullSeconds > 86400 ||
          !Number.isInteger(billingSeconds) ||
          billingSeconds < BILLING_SYNC_MIN_SECONDS ||
          billingSeconds > BILLING_SYNC_MAX_SECONDS) return false;
      set({
        autoSyncEnabled: enabled,
        autoSyncSeconds: fullSeconds,
        billingSyncSeconds,
      });
      const current = get();
      saveToStorage({
        modus: current.modus,
        maschinenAktiv: current.maschinenAktiv,
        apiUrl: current.apiUrl,
        apiKey: current.apiKey,
        customSequenzen: current.customSequenzen,
        customLaeufe: current.customLaeufe,
        produkte: current.produkte,
        kioskMode: current.kioskMode,
        kioskPinHash: current.kioskPinHash,
        kioskPinSalt: current.kioskPinSalt,
        kioskFailedAttempts: current.kioskFailedAttempts,
        kioskLockoutUntil: current.kioskLockoutUntil,
        autoSyncEnabled: enabled,
        autoSyncSeconds: fullSeconds,
        billingSyncSeconds,
      });
      saveSyncSettings(enabled, fullSeconds, billingSeconds);
      return true;
    },

    addLocalSpieler: (name) => {
      const trimmed = name.trim();
      const state = get();
      // Reuse an existing player with the same name (case-insensitive)
      const existing = state.portalSpieler.find(
        p => p.name.toLowerCase() === trimmed.toLowerCase()
      );
      if (existing) return existing;

      const localId = -Date.now();
      const neu: PortalSpieler = { id: localId, name: trimmed, mitgliedNr: null, lokal: true };
      const pendingSpieler = [...state.pendingSpieler, { localId, name: trimmed }];
      const portalSpieler = [...state.portalSpieler, neu].sort((a, b) => a.name.localeCompare(b.name));
      savePendingSpieler(pendingSpieler);
      saveCachedSpieler(portalSpieler);
      set({ pendingSpieler, portalSpieler });
      return neu;
    },

    registerSpielerFuerTag: (spielerId) => set((state) => {
      const { kreditDatum, kredite } = rolledKredite(state);
      if (kredite[spielerId]) return state; // already listed today
      const nextKredite = { ...kredite, [spielerId]: { gewaehrt: 0, verbraucht: 0 } };
      saveKredite(kreditDatum, nextKredite);
      return { kreditDatum, kredite: nextKredite };
    }),

    addKredite: (spielerId, anzahl) => set((state) => {
      if (anzahl <= 0) return state;
      const { kreditDatum, kredite } = rolledKredite(state);
      const stand = kredite[spielerId] ?? { gewaehrt: 0, verbraucht: 0 };
      const nextKredite = { ...kredite, [spielerId]: { ...stand, gewaehrt: stand.gewaehrt + anzahl } };
      const pendingKredite: KreditEvent[] = [
        ...state.pendingKredite,
        { externalId: generateSpielId(), spielerId, datum: kreditDatum, typ: 'GRANT' as const, anzahl },
      ];
      saveKredite(kreditDatum, nextKredite);
      savePendingKredite(pendingKredite);
      return { kreditDatum, kredite: nextKredite, pendingKredite };
    }),

    removeKredit: (spielerId, anzahl) => set((state) => {
      if (anzahl <= 0) return state;
      const { kreditDatum, kredite } = rolledKredite(state);
      const stand = kredite[spielerId] ?? { gewaehrt: 0, verbraucht: 0 };
      // Refund reduces bezuelt (gewaehrt); cannot go below gespillt (verbraucht)
      const effective = Math.min(anzahl, Math.max(0, stand.gewaehrt - stand.verbraucht));
      if (effective === 0) return state;
      const nextKredite = { ...kredite, [spielerId]: { ...stand, gewaehrt: stand.gewaehrt - effective } };
      const nextLineup = state.lineup.filter(entry =>
        entry.spielerId !== spielerId ||
        nextKredite[spielerId].gewaehrt - nextKredite[spielerId].verbraucht > 0
      );
      const pendingKredite: KreditEvent[] = [
        ...state.pendingKredite,
        { externalId: generateSpielId(), spielerId, datum: kreditDatum, typ: 'GRANT' as const, anzahl: -effective },
      ];
      saveKredite(kreditDatum, nextKredite);
      savePendingKredite(pendingKredite);
      if (nextLineup.length !== state.lineup.length) saveLineup(nextLineup);
      return { kreditDatum, kredite: nextKredite, pendingKredite, lineup: nextLineup };
    }),

    deleteKreditEntry: (spielerId) => set((state) => {
      const { kreditDatum, kredite } = rolledKredite(state);
      const stand = kredite[spielerId];
      if (!stand) return state;
      const rest = Math.max(0, stand.gewaehrt - stand.verbraucht);
      // Build compensating events to zero out the server state
      const newEvents: KreditEvent[] = [];
      if (rest > 0) {
        // Cancel remaining paid credits
        newEvents.push({ externalId: generateSpielId(), spielerId, datum: kreditDatum, typ: 'GRANT', anzahl: -rest });
      }
      const nextKredite = { ...kredite };
      delete nextKredite[spielerId];
      const pendingKredite = [...state.pendingKredite, ...newEvents];
      saveKredite(kreditDatum, nextKredite);
      savePendingKredite(pendingKredite);
      return { kreditDatum, kredite: nextKredite, pendingKredite };
    }),

    getKreditRest: (spielerId) => {
      const state = get();
      if (state.kreditDatum !== todayStr()) return 0; // stale day — everything expired
      const stand = state.kredite[spielerId];
      return stand ? Math.max(0, stand.gewaehrt - stand.verbraucht) : 0;
    },

    ladeKredite: async () => {
      const state = get();
      if (!state.apiUrl || !state.apiKey) return;
      set({ krediteLaden: true });
      try {
        const today = todayStr();
        const res = await fetch(`${state.apiUrl}/api/sync/kredite?datum=${today}`, {
          headers: { 'x-api-key': state.apiKey },
          signal: AbortSignal.timeout(8000),
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json() as { kredite: Array<{ spielerId: number; gewaehrt: number; verbraucht: number }> };

        // Server state + any local events not yet pushed = current local truth
        const merged: Record<number, KreditStand> = {};
        for (const k of data.kredite) merged[k.spielerId] = { gewaehrt: k.gewaehrt, verbraucht: k.verbraucht };
        for (const e of get().pendingKredite) {
          if (e.datum !== today) continue;
          const stand = merged[e.spielerId] ?? { gewaehrt: 0, verbraucht: 0 };
          merged[e.spielerId] = e.typ === 'GRANT'
            ? { ...stand, gewaehrt: stand.gewaehrt + e.anzahl }
            : { ...stand, verbraucht: stand.verbraucht + e.anzahl };
        }
        const current = get();
        const currentLineup = current.lineup;
        const nextLineup = current.screen === 'spiel'
          ? currentLineup
          : currentLineup.filter(entry => {
              const stand = merged[entry.spielerId];
              return stand ? Math.max(0, stand.gewaehrt - stand.verbraucht) > 0 : false;
            });
        if (nextLineup.length !== currentLineup.length) saveLineup(nextLineup);
        saveKredite(today, merged);
        set({ kreditDatum: today, kredite: merged, lineup: nextLineup, krediteLaden: false });
      } catch {
        set({ krediteLaden: false });
      }
    },

    addVerkauf: (spielerId, productId, quantity) => set((state) => {
      if (!Number.isSafeInteger(quantity) || quantity === 0) return state;
      const product = state.produkte.find(p => p.id === productId);
      // Placeholder defaults have no server revision and can never be sold.
      if (!product?.currentPrice || product.id <= 0) return state;
      const event: VerkaufEvent = {
        externalId: generateSpielId(), spielerId, datum: todayStr(), productId,
        priceRevisionId: product.currentPrice.id, quantity,
      };
      const verkaufDatum = todayStr();
      // A terminal can remain open across midnight.  Keep the durable pending
      // queue intact (it contains dated events), but rebuild the day cache.
      const pendingVerkaeufe = [...state.pendingVerkaeufe, event];
      const verkaeufe = [...state.verkaeufe.filter(v => v.datum === verkaufDatum), event];
      saveVerkaeufe(PENDING_VERKAEUFE_KEY, pendingVerkaeufe);
      saveVerkaeufe(VERKAEUFE_KEY, verkaeufe);
      return {
        verkaufDatum, pendingVerkaeufe, verkaeufe,
        serverVerkaeufe: state.verkaufDatum === verkaufDatum ? state.serverVerkaeufe : [],
        paidBillCache: state.verkaufDatum === verkaufDatum ? state.paidBillCache : {},
      };
    }),

    getProduktAnzahl: (spielerId, productId) => {
      const state = get();
      if (state.verkaufDatum !== todayStr()) return 0;
      const serverQuantity = state.serverVerkaeufe
        .filter(row => row.spielerId === spielerId && row.productId === productId)
        .reduce((sum, row) => sum + row.quantity, 0);
      const pendingQuantity = state.pendingVerkaeufe
        .filter(event => event.datum === state.verkaufDatum && event.spielerId === spielerId && event.productId === productId)
        .reduce((sum, event) => sum + event.quantity, 0);
      return serverQuantity + pendingQuantity;
    },

    getProjectedDaySummary: (spielerId) => {
      const state = get();
      const datum = todayStr();
      // Never project yesterday's cached summary after a local date rollover.
      const serverPlayer = state.daySummary?.datum === datum
        ? state.daySummary.players.find(p => p.spielerId === spielerId)
        : undefined;
      const cached = state.paidBillCache[spielerId];
      const baseSource = serverPlayer || cached;
      const hasPendingBillableActivity =
        state.pendingKredite.some(event =>
          event.spielerId === spielerId && event.datum === datum &&
          event.typ === 'USE' && event.anzahl !== 0
        ) ||
        state.pendingVerkaeufe.some(event =>
          event.spielerId === spielerId && event.datum === datum
        );
      const startsAfterSettlement = baseSource?.state === 'PAID' && hasPendingBillableActivity;

      const spieler = state.pendingSpieler.find(s => s.localId === spielerId) || state.spielerUpdates.find(s => s.spielerId === spielerId && s.status === 'pending');
      const baseName = baseSource?.spielerName || (spieler ? spieler.name : `Spiller ${spielerId}`) || `Spiller ${spielerId}`;

      const projected: DaySummary['players'][number] = {
        spielerId,
        spielerName: baseName,
        mitgliedNr: baseSource?.mitgliedNr ?? null,
        lines: baseSource && !startsAfterSettlement ? [...baseSource.lines.map(l => ({ ...l }))] : [],
        categorySubtotals: baseSource && !startsAfterSettlement ? { ...baseSource.categorySubtotals } : {},
        totalCents: baseSource && !startsAfterSettlement ? baseSource.totalCents : 0,
        credit: baseSource ? { ...baseSource.credit } : { granted: 0, used: 0, remaining: 0 },
        games: baseSource?.games ?? 0,
        completedGames: baseSource?.completedGames ?? 0,
        state: baseSource?.state ?? 'OPEN',
        paymentExternalId: baseSource?.paymentExternalId ?? null,
        paidAt: baseSource?.paidAt ?? null,
      };

      // 1. Layer credit counts. GRANTs and unused credits are informational;
      // only individual USE events are billable below.
      const k = state.kredite[spielerId];
      if (k) {
        projected.credit.granted = k.gewaehrt;
        projected.credit.used = k.verbraucht;
        projected.credit.remaining = Math.max(0, k.gewaehrt - k.verbraucht);
      }

      // 2. Layer pending credit consumption at the currently configured game
      // credit price.  Do not use the aggregate credit tally here: it contains
      // grants and may already include authoritative consumption.
      const pendingCreditUses = state.pendingKredite.filter(event =>
        event.spielerId === spielerId && event.datum === datum &&
        event.typ === 'USE' && event.anzahl !== 0
      );
      const creditProduct = state.produkte.find(product =>
        product.code === PRODUKT_IDS.kredit || product.category === PRODUKT_IDS.kredit
      );
      if (creditProduct?.currentPrice) {
        for (const use of pendingCreditUses) {
          // Older persisted events did not have a snapshot.  Keep them
          // billable using today's price, but never replace a real snapshot.
          const unitPriceCents = use.unitPriceCents ?? creditProduct.currentPrice.unitPriceCents;
          const priceRevisionId = use.priceRevisionId ?? creditProduct.currentPrice.id;
          const totalCents = use.anzahl * unitPriceCents;
          projected.lines.push({
            productId: creditProduct.id,
            productName: creditProduct.name,
            category: creditProduct.category,
            priceRevisionId,
            unitPriceCents,
            quantity: use.anzahl,
            totalCents,
            pending: true,
          });
          projected.totalCents += totalCents;
          projected.categorySubtotals[creditProduct.category] =
            (projected.categorySubtotals[creditProduct.category] || 0) + totalCents;
        }
      }

      // 3. Layer sales. Each queued event deliberately gets a separate pending
      // line so it cannot be coalesced into an authoritative row and counted
      // twice on a later refresh.
      const pendingSales = state.pendingVerkaeufe
        .filter(v => v.spielerId === spielerId && v.datum === datum);
      for (const sale of pendingSales) {
        const prod = state.produkte.find(p => p.id === sale.productId);
        if (!prod || !prod.currentPrice) continue;
        const linePriceCents = prod.currentPrice.unitPriceCents;
        const addTotal = sale.quantity * linePriceCents;

        projected.lines.push({
          productId: sale.productId,
          productName: prod.name,
          category: prod.category,
          priceRevisionId: sale.priceRevisionId,
          unitPriceCents: linePriceCents,
          quantity: sale.quantity,
          totalCents: addTotal,
          pending: true,
        });
        projected.totalCents += addTotal;
        projected.categorySubtotals[prod.category] = (projected.categorySubtotals[prod.category] || 0) + addTotal;
      }

      projected.lines = projected.lines.filter(l => l.quantity > 0);

      // Billable activity after a settled baseline reopens the bill.  A queued
      // payment itself is not activity and must not change this state.
      if (projected.state === 'PAID' && hasPendingBillableActivity) {
        projected.state = 'OPEN';
        projected.paymentExternalId = null;
        projected.paidAt = null;
      }

      // 4. Layer games
      const pendingGames = state.pendingGames.filter(g => g.datum.startsWith(datum) && g.teilnahmen.some(t => t.spielerId === spielerId));
      for (const g of pendingGames) {
        projected.games += 1;
        if (g.abgeschlossen) projected.completedGames += 1;
      }

      if (!baseSource && !k && pendingCreditUses.length === 0 && pendingSales.length === 0 && pendingGames.length === 0) {
        return null;
      }

      return projected;
    },

    ladeProdukte: async () => {
      const state = get();
      if (!state.apiUrl || !state.apiKey) return;
      try {
        const res = await fetch(`${state.apiUrl}/api/sync/products`, {
          headers: { 'x-api-key': state.apiKey }, signal: AbortSignal.timeout(8000),
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json() as { products?: Produkt[] };
        const produkte = normalizeProdukte(data.products);
        const s = get();
        saveToStorage({ modus: s.modus, maschinenAktiv: s.maschinenAktiv, apiUrl: s.apiUrl, apiKey: s.apiKey, customSequenzen: s.customSequenzen, customLaeufe: s.customLaeufe, kioskMode: s.kioskMode, kioskPinHash: s.kioskPinHash, kioskPinSalt: s.kioskPinSalt, kioskFailedAttempts: s.kioskFailedAttempts, kioskLockoutUntil: s.kioskLockoutUntil, produkte });
        set({ produkte });
      } catch {}
    },

    ladeVerkaeufe: async () => {
      const state = get();
      if (!state.apiUrl || !state.apiKey) return;
      const datum = todayStr();
      set({ verkaeufeLaden: true });
      try {
        const res = await fetch(`${state.apiUrl}/api/sync/sales?datum=${datum}`, {
          headers: { 'x-api-key': state.apiKey }, signal: AbortSignal.timeout(8000),
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json() as { datum: string; sales: VerkaufReportRow[]; totalCents: number };
        // Rows are authoritative by player and product. Pending events remain
        // separate, so each is added exactly once by getProduktAnzahl.
        const serverVerkaeufe = data.sales;
        saveVerkaufsReport(datum, serverVerkaeufe);
        set({
          verkaufDatum: datum,
          serverVerkaeufe,
          verkaeufe: get().verkaeufe.filter(event => event.datum === datum),
          paidBillCache: get().verkaufDatum === datum ? get().paidBillCache : {},
          verkaeufeLaden: false,
        });
      } catch { set({ verkaeufeLaden: false }); }
    },

    ladeDaySummary: async () => {
      const state = get();
      if (!state.apiUrl || !state.apiKey) return;
      set({ daySummaryLaden: true });
      const datum = todayStr();
      try {
        const res = await fetch(`${state.apiUrl}/api/sync/bills/day-summary?datum=${datum}`, {
          headers: { 'x-api-key': state.apiKey },
          signal: AbortSignal.timeout(15000),
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json() as DaySummary;
        if (data.datum === datum) {
          saveDaySummary(data);
          set({ daySummary: data, daySummaryLaden: false });
        } else {
          set({ daySummaryLaden: false });
        }
      } catch { set({ daySummaryLaden: false }); }
    },

    markBillPaid: (spielerId) => set((state) => {
      // Prevent duplicate pending payments for the same player today
      if (state.pendingPayments.some(p => p.spielerId === spielerId && p.datum === todayStr())) {
        return state;
      }
      const event: PaymentEvent = {
        externalId: generateSpielId(),
        spielerId,
        datum: todayStr(),
        coveredActivityExternalIds: [
          ...state.pendingKredite
            .filter(e => e.spielerId === spielerId && e.datum === todayStr() && e.typ === 'USE')
            .map(e => e.externalId),
          ...state.pendingVerkaeufe
            .filter(e => e.spielerId === spielerId && e.datum === todayStr())
            .map(e => e.externalId),
        ],
        coveredBillSnapshot: state.getProjectedDaySummary(spielerId) ?? undefined,
      };
      const pendingPayments = [...state.pendingPayments, event];
      savePendingPayments(pendingPayments);

      // Offline Paid remains pending and does not remove active player until portal acceptance.
      return { pendingPayments };
    }),

    queueSpielerUpdate: (spielerId, changes) => set((state) => {
      const spieler = state.portalSpieler.find(p => p.id === spielerId);
      const spielerName = changes.name || spieler?.name || `#${spielerId}`;
      const update: SpielerUpdate = {
        externalId: generateSpielId(),
        spielerId,
        spielerName,
        typ: 'UPDATE',
        name: changes.name,
        email: changes.email,
        portalAktiv: changes.portalAktiv,
        status: 'pending',
        queuedAt: new Date().toISOString(),
      };
      // Optimistically update local caches so the UI reflects the edit immediately
      const portalSpieler = state.portalSpieler.map(p =>
        p.id === spielerId ? { ...p, name: changes.name, email: changes.email, portalAktiv: changes.portalAktiv } : p
      ).sort((a, b) => a.name.localeCompare(b.name));
      const spielerUpdates = [update, ...state.spielerUpdates].slice(0, 50);
      saveSpielerUpdates(spielerUpdates);
      saveCachedSpieler(portalSpieler);
      return { spielerUpdates, portalSpieler };
    }),

    queuePasswortReset: (spielerId) => set((state) => {
      const spieler = state.portalSpieler.find(p => p.id === spielerId);
      const update: SpielerUpdate = {
        externalId: generateSpielId(),
        spielerId,
        spielerName: spieler?.name ?? `#${spielerId}`,
        typ: 'PASSWORT_RESET',
        status: 'pending',
        queuedAt: new Date().toISOString(),
      };
      const spielerUpdates = [update, ...state.spielerUpdates].slice(0, 50);
      saveSpielerUpdates(spielerUpdates);
      return { spielerUpdates };
    }),

    refreshSpielerUpdateStatus: async () => {
      const state = get();
      if (!state.apiUrl || !state.apiKey) return;
      // Only entries pushed but without final email verdict need polling
      const offen = state.spielerUpdates.filter(u => u.status === 'synced' || u.status === 'email_failed');
      if (!offen.length) return;
      try {
        const ids = offen.map(u => u.externalId).join(',');
        const res = await fetch(`${state.apiUrl}/api/sync/spieler-updates/status?ids=${encodeURIComponent(ids)}`, {
          headers: { 'x-api-key': state.apiKey },
          signal: AbortSignal.timeout(15000),
        });
        if (!res.ok) return;
        const data = await res.json() as { updates: Array<{ externalId: string; emailStatus: string; emailError: string | null }> };
        const byId = new Map(data.updates.map(u => [u.externalId, u]));
        const spielerUpdates = get().spielerUpdates.map(u => {
          const r = byId.get(u.externalId);
          if (!r) return u;
          if (r.emailStatus === 'SENT') return { ...u, status: 'email_sent' as const, fehler: undefined };
          if (r.emailStatus === 'FAILED') return { ...u, status: 'email_failed' as const, fehler: r.emailError ?? 'Email-Feeler' };
          return u;
        });
        saveSpielerUpdates(spielerUpdates);
        set({ spielerUpdates });
      } catch {}
    },

    clearErledegtSpielerUpdates: () => set((state) => {
      const spielerUpdates = state.spielerUpdates.filter(u => u.status !== 'email_sent' && !(u.status === 'synced' && u.typ === 'UPDATE'));
      saveSpielerUpdates(spielerUpdates);
      return { spielerUpdates };
    }),

    getAktivenSpieler: () => {
      const s = get();
      return s.spieler[s.spielerIndex];
    },

    ladeSpielerVomPortal: async () => {
      const state = get();
      set({ portalLaden: true, portalFehler: null, spielerAusCache: false });

      if (!state.apiUrl || !state.apiKey) {
        // No network config — try cache immediately
        const cached = loadCachedSpieler();
        if (cached.length) {
          set(s => ({ portalSpieler: cached, portalLaden: false, spielerAusCache: true,
            spieler: s.spieler.map(player => ({ ...player,
              name: cached.find(p => p.id === player.id)?.name ?? player.name })) }));
        } else {
          set({
            portalFehler: 'API URL / Key net konfiguriert (Astellungen)',
            portalLaden: false,
          });
        }
        return;
      }

      try {
        const res = await fetch(`${state.apiUrl}/api/sync/spieler`, {
          headers: { 'x-api-key': state.apiKey },
          signal: AbortSignal.timeout(8000),
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json() as { spieler: PortalSpieler[] };
        // Keep locally created (not yet synced) players in the list
        const pending = get().pendingSpieler;
        const merged = [
          ...data.spieler,
          ...pending
            .filter(p => !data.spieler.some(s => s.name.toLowerCase() === p.name.toLowerCase()))
            .map(p => ({ id: p.localId, name: p.name, mitgliedNr: null, lokal: true as const })),
        ].sort((a, b) => a.name.localeCompare(b.name));
        saveCachedSpieler(merged);
        set(s => {
          // This is authoritative for portal IDs, while `pending` above keeps
          // terminal-local IDs alive until their create mapping arrives.
          const available = new Set(merged.map(p => p.id));
          const lineup = s.lineup.filter(entry => available.has(entry.spielerId));
          const removed = lineup.length !== s.lineup.length;
          if (removed) saveLineup(lineup);
          return {
            portalSpieler: merged, portalLaden: false, spielerAusCache: false,
            lineup,
            lineupWarning: removed
              ? 'Net méi existente Spiller goufen aus der Opstellung geläscht. Wielt se nei.'
              : s.lineupWarning,
            spieler: s.spieler.map(player => ({ ...player,
              name: merged.find(p => p.id === player.id)?.name ?? player.name })),
          };
        });
      } catch {
        // Network failed — fall back to cache
        const cached = loadCachedSpieler();
        if (cached.length) {
          set(s => ({
            portalSpieler: cached,
            portalFehler: `Offline – ${cached.length} Spillesch aus Cache gelued`,
            portalLaden: false,
            spielerAusCache: true,
            spieler: s.spieler.map(player => ({ ...player,
              name: cached.find(p => p.id === player.id)?.name ?? player.name })),
          }));
        } else {
          set({
            portalFehler: 'Verbindung fehlgeschloen. Kee Cache disponibel.',
            portalLaden: false,
          });
        }
      }
    },

    syncBillingPending: async () => runSerializedSync('billing', async () => {
      const state = get();
      if (!state.apiUrl || !state.apiKey) {
        set({ syncStatus: 'error' });
        return;
      }
      set({ syncStatus: 'syncing' });
      let hasError = false;
      const headers = { 'Content-Type': 'application/json', 'x-api-key': state.apiKey };

      try {
        const credits = get().pendingKredite.filter(event => event.spielerId > 0);
        if (credits.length) {
          const response = await fetch(`${state.apiUrl}/api/sync/kredite`, {
            method: 'POST',
            headers,
            body: JSON.stringify({ events: credits }),
            signal: AbortSignal.timeout(15000),
          });
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          const submitted = new Set(credits.map(event => event.externalId));
          set(current => {
            const pendingKredite = current.pendingKredite.filter(event => !submitted.has(event.externalId));
            savePendingKredite(pendingKredite);
            return { pendingKredite };
          });
        }
      } catch (error) {
        console.error('Billing credits push failed:', error);
        hasError = true;
      }

      try {
        const sales = get().pendingVerkaeufe.filter(event => event.spielerId > 0);
        if (sales.length) {
          const response = await fetch(`${state.apiUrl}/api/sync/sales`, {
            method: 'POST',
            headers,
            body: JSON.stringify({ events: sales }),
            signal: AbortSignal.timeout(15000),
          });
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          const submitted = new Set(sales.map(event => event.externalId));
          set(current => {
            const pendingVerkaeufe = current.pendingVerkaeufe.filter(event => !submitted.has(event.externalId));
            saveVerkaeufe(PENDING_VERKAEUFE_KEY, pendingVerkaeufe);
            return { pendingVerkaeufe };
          });
        }
      } catch (error) {
        console.error('Billing sales push failed:', error);
        hasError = true;
      }

      try {
        const payments = [...get().pendingPayments];
        if (payments.length) {
          const response = await fetch(`${state.apiUrl}/api/sync/payments`, {
            method: 'POST',
            headers,
            body: JSON.stringify({
              events: payments.map(({ externalId, spielerId, datum }) => ({
                externalId, spielerId, datum,
              })),
            }),
            signal: AbortSignal.timeout(15000),
          });
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          const data = await response.json() as {
            results: Array<{ externalId: string; status: string }>;
          };
          const byId = new Map(data.results.map(result => [result.externalId, result]));
          const accepted: PaymentEvent[] = [];
          const pendingPayments = get().pendingPayments.filter(payment => {
            const status = byId.get(payment.externalId)?.status;
            if (status === 'accepted' || status === 'skipped') {
              accepted.push(payment);
              return false;
            }
            return true;
          });
          savePendingPayments(pendingPayments);
          set({ pendingPayments });

          if (accepted.length) {
            const current = get();
            const kredite = { ...current.kredite };
            const paidBillCache = { ...current.paidBillCache };
            const retired = new Set<number>();
            for (const payment of accepted) {
              const bill = payment.coveredBillSnapshot ??
                current.getProjectedDaySummary(payment.spielerId);
              if (bill && payment.datum === current.verkaufDatum) {
                paidBillCache[payment.spielerId] = {
                  ...bill,
                  lines: bill.lines.map(line => ({ ...line })),
                  categorySubtotals: { ...bill.categorySubtotals },
                  credit: { ...bill.credit },
                  state: 'PAID',
                  paymentExternalId: payment.externalId,
                  paidAt: new Date().toISOString(),
                };
              }
              if (payment.datum === current.kreditDatum && payment.datum === todayStr()) {
                retired.add(payment.spielerId);
                delete kredite[payment.spielerId];
              }
            }
            const lineup = current.lineup.filter(entry => !retired.has(entry.spielerId));
            saveKredite(current.kreditDatum, kredite);
            savePaidBillCache(current.verkaufDatum, paidBillCache);
            saveLineup(lineup);
            set({ kredite, paidBillCache, lineup });
          }
        }
      } catch (error) {
        console.error('Billing payments push failed:', error);
        hasError = true;
      }

      await get().ladeKredite();
      await get().ladeVerkaeufe();
      await get().ladeDaySummary();
      const completedAt = new Date().toLocaleTimeString('de-LU', {
        hour: '2-digit', minute: '2-digit', second: '2-digit',
      });
      set({
        syncStatus: hasError ? 'error' : 'success',
        lastSync: completedAt,
        lastBillingSync: completedAt,
      });
    }),

    syncAllPending: async () => runSerializedSync('full', async () => {
      const state = get();
      if (!state.apiUrl || !state.apiKey) {
        set({ syncStatus: 'error' });
        return;
      }
      set({ syncStatus: 'syncing' });

      let hasError = false;

      // ── Step 1: push locally created players first, get server IDs ──────
      try {
        if (get().pendingSpieler.length > 0) {
          const resSp = await fetch(`${state.apiUrl}/api/sync/spieler`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'x-api-key': state.apiKey },
            body: JSON.stringify({ spieler: get().pendingSpieler.map(p => ({ id: p.localId, name: p.name })) }),
            signal: AbortSignal.timeout(15000),
          });
          if (!resSp.ok) throw new Error(`HTTP ${resSp.status}`);
          const spData = await resSp.json() as { mappings?: Array<{ localId: number; id: number; name: string }> };

          const idMap = new Map<number, number>();
          for (const m of spData.mappings ?? []) idMap.set(m.localId, m.id);

          const remap = (id: number) => idMap.get(id) ?? id;
          const s2 = get();

          const remappedPending = s2.pendingGames.map(g => ({
            ...g,
            teilnahmen: g.teilnahmen.map(t => ({ ...t, spielerId: remap(t.spielerId) })),
            ergebnisse: g.ergebnisse.map(e => ({ ...e, spielerId: remap(e.spielerId) })),
          }));
          const remappedHistory = s2.gameHistory.map(g => ({
            ...g,
            teilnahmen: g.teilnahmen.map(t => ({ ...t, spielerId: remap(t.spielerId) })),
            ergebnisse: g.ergebnisse.map(e => ({ ...e, spielerId: remap(e.spielerId) })),
            spielerNamen: Object.fromEntries(Object.entries(g.spielerNamen).map(([id, name]) => [remap(Number(id)), name]))
          }));
          const remappedSpieler = s2.spieler.map(s => ({ ...s, id: remap(s.id) }));
          const remappedLineup = s2.lineup.map(entry => ({ ...entry, spielerId: remap(entry.spielerId) }));
          const remappedKreditEvents = s2.pendingKredite.map(e => ({ ...e, spielerId: remap(e.spielerId) }));
          const remappedVerkaufEvents = s2.pendingVerkaeufe.map(e => ({ ...e, spielerId: remap(e.spielerId) }));
          const remappedVerkaeufe = s2.verkaeufe.map(e => ({ ...e, spielerId: remap(e.spielerId) }));
          const remappedServerVerkaeufe = s2.serverVerkaeufe.map(row => ({ ...row, spielerId: remap(row.spielerId) }));
          const remappedPayments = s2.pendingPayments.map(p => ({
            ...p,
            spielerId: remap(p.spielerId),
            coveredBillSnapshot: p.coveredBillSnapshot
              ? { ...p.coveredBillSnapshot, spielerId: remap(p.coveredBillSnapshot.spielerId) }
              : undefined,
          }));
          const remappedPaidBillCache = Object.fromEntries(
            Object.entries(s2.paidBillCache).map(([id, bill]) => {
              const spielerId = remap(Number(id));
              return [spielerId, { ...bill, spielerId }];
            })
          ) as Record<number, DaySummary['players'][number]>;
          const remappedActiveCreditUses = s2.activeGameCreditUses.map(e => ({ ...e, spielerId: remap(e.spielerId) }));
          const remappedUpdates = s2.spielerUpdates.map(u => ({ ...u, spielerId: remap(u.spielerId) }));
          const remappedKredite = Object.fromEntries(
            Object.entries(s2.kredite).map(([id, stand]) => [remap(Number(id)), stand])
          ) as Record<number, KreditStand>;
          const remappedPortal = s2.portalSpieler.map(p =>
            idMap.has(p.id) ? { ...p, id: idMap.get(p.id)!, lokal: undefined } : p
          );
          const stillPending = s2.pendingSpieler.filter(p => !idMap.has(p.localId));

          savePendingGames(remappedPending);
          savePendingSpieler(stillPending);
          saveCachedSpieler(remappedPortal);
          saveGameHistory(remappedHistory);
          savePendingKredite(remappedKreditEvents);
          saveVerkaeufe(PENDING_VERKAEUFE_KEY, remappedVerkaufEvents);
          saveVerkaeufe(VERKAEUFE_KEY, remappedVerkaeufe);
          savePendingPayments(remappedPayments);
          savePaidBillCache(s2.verkaufDatum, remappedPaidBillCache);
          saveVerkaufsReport(s2.verkaufDatum, remappedServerVerkaeufe);
          saveKredite(s2.kreditDatum, remappedKredite);
          saveSpielerUpdates(remappedUpdates);
          saveLineup(remappedLineup);

          set({
            pendingGames: remappedPending,
            pendingSpieler: stillPending,
            spieler: remappedSpieler,
            lineup: remappedLineup,
            portalSpieler: remappedPortal,
            gameHistory: remappedHistory,
            pendingKredite: remappedKreditEvents,
            pendingVerkaeufe: remappedVerkaufEvents,
            verkaeufe: remappedVerkaeufe,
            serverVerkaeufe: remappedServerVerkaeufe,
            activeGameCreditUses: remappedActiveCreditUses,
            kredite: remappedKredite,
            pendingPayments: remappedPayments,
            paidBillCache: remappedPaidBillCache,
            spielerUpdates: remappedUpdates,
          });
        }
      } catch (err) {
        console.error('Step 1 (Spieler) failed:', err);
        hasError = true;
      }

      // ── Step 1b: push pending kredite and verkaeufe ─────────────
      try {
        const pk = get().pendingKredite.filter(e => e.spielerId > 0);
        const pv = get().pendingVerkaeufe.filter(e => e.spielerId > 0);

        if (pk.length > 0) {
          const resKr = await fetch(`${state.apiUrl}/api/sync/kredite`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'x-api-key': state.apiKey },
            body: JSON.stringify({ events: pk }),
            signal: AbortSignal.timeout(15000),
          });
          if (!resKr.ok) throw new Error(`HTTP ${resKr.status}`);
          const submittedIds = new Set(pk.map(e => e.externalId));
          set(s => {
            const remaining = s.pendingKredite.filter(e => !submittedIds.has(e.externalId));
            savePendingKredite(remaining);
            return { pendingKredite: remaining };
          });
        }

        if (pv.length > 0) {
          const resVk = await fetch(`${state.apiUrl}/api/sync/sales`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'x-api-key': state.apiKey },
            body: JSON.stringify({ events: pv }),
            signal: AbortSignal.timeout(15000),
          });
          if (!resVk.ok) throw new Error(`HTTP ${resVk.status}`);
          const submittedIdsVk = new Set(pv.map(e => e.externalId));
          set(s => {
            const remaining = s.pendingVerkaeufe.filter(e => !submittedIdsVk.has(e.externalId));
            saveVerkaeufe(PENDING_VERKAEUFE_KEY, remaining);
            return { pendingVerkaeufe: remaining };
          });
        }
      } catch (err) {
        console.error('Step 1b (Credits/Sales) failed:', err);
        hasError = true;
      }

      // ── Step 1c: push pending player edits / password resets ─────────────
      try {
        const pushable = get().spielerUpdates.filter(u => u.status === 'pending' && u.spielerId > 0);
        if (pushable.length > 0) {
          const resU = await fetch(`${state.apiUrl}/api/sync/spieler-updates`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'x-api-key': state.apiKey },
            body: JSON.stringify({
              updates: pushable.map(u => ({
                externalId: u.externalId,
                spielerId: u.spielerId,
                typ: u.typ,
                ...(u.typ === 'UPDATE' ? { name: u.name, email: u.email || null, portalAktiv: u.portalAktiv } : {}),
              })),
            }),
            signal: AbortSignal.timeout(30000),
          });
          if (!resU.ok) throw new Error(`HTTP ${resU.status}`);
          const dataU = await resU.json() as { results: Array<{ externalId: string; status: string; emailStatus: string; error?: string }> };
          const byId = new Map(dataU.results.map(r => [r.externalId, r]));

          const nextUpdates = get().spielerUpdates.map(u => {
            const r = byId.get(u.externalId);
            if (!r) return u;
            if (r.status === 'error') return { ...u, fehler: r.error };
            if (u.typ === 'UPDATE') return { ...u, status: 'synced' as const, fehler: undefined };
            if (u.typ === 'PASSWORT_RESET') {
              if (r.emailStatus === 'SENT') return { ...u, status: 'email_sent' as const, fehler: undefined };
              if (r.emailStatus === 'FAILED') return { ...u, status: 'email_failed' as const, fehler: r.error ?? 'Email Error' };
              return { ...u, status: 'synced' as const, fehler: undefined };
            }
            return u;
          });
          saveSpielerUpdates(nextUpdates);
          set({ spielerUpdates: nextUpdates });
        }
      } catch (err) {
        console.error('Step 1c (Player Updates) failed:', err);
        hasError = true;
      }

      // ── Step 2: push pending games ─────────────
      try {
        const pushableGames = [...get().pendingGames];
        if (pushableGames.length > 0) {
          const res = await fetch(`${state.apiUrl}/api/sync/spiele`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'x-api-key': state.apiKey },
            body: JSON.stringify({ spiele: pushableGames }),
            signal: AbortSignal.timeout(30000),
          });
          if (!res.ok) throw new Error(`HTTP ${res.status}`);

          const resData = await res.json().catch(() => ({})) as { results?: Array<{ externalId?: string, status: string }> };
          const byId = new Map(resData.results?.map((r, i) => [r.externalId || pushableGames[i]?.externalId, r]) || []);

          set(s => {
            const remaining = s.pendingGames.filter(g => {
              const isPushed = pushableGames.some(pg => pg.externalId === g.externalId);
              if (!isPushed) return true; // added in flight

              // Find result in response
              const result = resData.results?.find(r => r.externalId === g.externalId);

              // fallback is 'created' ONLY IF it was pushed and there are no results
              // wait, if we are in conflict, result.status will be 'conflict'
              const status = result?.status || (resData.results ? 'unknown' : 'created');

              return status !== 'created' && status !== 'skipped' && status !== 'accepted';
            });
            savePendingGames(remaining);
            return { pendingGames: remaining };
          });
        }
      } catch (err) {
        console.error('Step 2 (Games) failed:', err);
        hasError = true;
      }

      // ── Step 2.5: push pending payments ─────────────
      try {
        const pushablePayments = get().pendingPayments;
        if (pushablePayments.length > 0) {
          const resP = await fetch(`${state.apiUrl}/api/sync/payments`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'x-api-key': state.apiKey },
            // Snapshot metadata is terminal-only and not part of the portal API.
            body: JSON.stringify({ events: pushablePayments.map(({ externalId, spielerId, datum }) => ({ externalId, spielerId, datum })) }),
            signal: AbortSignal.timeout(15000),
          });
          if (!resP.ok) throw new Error(`HTTP ${resP.status}`);
          const dataP = await resP.json() as { results: Array<{ externalId: string; status: string }> };
          const byId = new Map(dataP.results.map(r => [r.externalId, r]));

          const successfulPayments: PaymentEvent[] = [];
          // Remove accepted and skipped, keep conflict or failed
          const nextPending = get().pendingPayments.filter(p => {
            const status = byId.get(p.externalId)?.status;
            if (status === 'accepted' || status === 'skipped') {
              successfulPayments.push(p);
              return false; // remove from pending
            }
            return true; // keep conflict or unacknowledged
          });

          savePendingPayments(nextPending);
          set({ pendingPayments: nextPending });

          if (successfulPayments.length > 0) {
            // Remove successful payments from today's kredite & lineup (but bill history in day summary stays)
            const s = get();
            const newKredite = { ...s.kredite };
            const newCache = { ...s.paidBillCache };
            const activeDayPlayerIds = new Set<number>();
            for (const payment of successfulPayments) {
              const pId = payment.spielerId;
              const proj = payment?.coveredBillSnapshot ?? s.getProjectedDaySummary(pId);
              if (proj && payment.datum === s.verkaufDatum) {
                newCache[pId] = {
                  ...proj,
                  spielerId: pId,
                  lines: proj.lines.map(line => ({ ...line })),
                  categorySubtotals: { ...proj.categorySubtotals },
                  credit: { ...proj.credit },
                  state: 'PAID',
                  paymentExternalId: payment?.externalId ?? proj.paymentExternalId,
                  paidAt: new Date().toISOString(),
                };
              }
              if (payment.datum === s.kreditDatum && payment.datum === todayStr()) {
                activeDayPlayerIds.add(pId);
                delete newKredite[pId];
              }
            }
            saveKredite(s.kreditDatum, newKredite);
            savePaidBillCache(s.verkaufDatum, newCache);

            const newLineup = s.lineup.filter(l => !activeDayPlayerIds.has(l.spielerId));
            saveLineup(newLineup);

            set({
              kredite: newKredite,
              lineup: newLineup,
              paidBillCache: newCache,
            });
          }
        }
      } catch (err) {
        console.error('Step 2.5 (Payments) failed:', err);
        hasError = true;
      }

      // ── Step 3: pull recent games from server → merge into local history ──
      try {
        const pullRes = await fetch(`${state.apiUrl}/api/sync/spiele?limit=100`, {
          headers: { 'x-api-key': state.apiKey },
          signal: AbortSignal.timeout(15000),
        });
        if (pullRes.ok) {
          const pullData = await pullRes.json() as {
            spiele: Array<{
              externalId: string; datum: string; modus: Modus; lauf: number; taubenProLauf: number;
              abgeschlossen: boolean; teilnahmen: Array<{ spielerId: number; startPosten: number; punkte: number; lauf: number }>;
              spielerNamen: Record<number, string>; confirmedLaunches?: number;
            }>;
          };
          const existingIds = new Set(get().gameHistory.map(g => g.externalId));
          const newGames = (pullData.spiele ?? [])
            .filter(g => !existingIds.has(g.externalId))
            .map(g => ({
              externalId: g.externalId, datum: g.datum, modus: g.modus, lauf: g.lauf,
              taubenProLauf: g.taubenProLauf, abgeschlossen: g.abgeschlossen, teilnahmen: g.teilnahmen,
              ergebnisse: [], confirmedLaunches: g.confirmedLaunches ?? 0, finishedAt: g.datum,
              spielerNamen: Object.fromEntries(Object.entries(g.spielerNamen).map(([k, v]) => [Number(k), v])),
            }));
          if (newGames.length > 0) {
            const merged = [...newGames, ...get().gameHistory]
              .sort((a, b) => new Date(b.finishedAt).getTime() - new Date(a.finishedAt).getTime())
              .slice(0, 50);
            saveGameHistory(merged);
            set({ gameHistory: merged });
          }
        }
      } catch (err) {
        console.error('Step 3 (Games Pull) failed:', err);
        hasError = true;
      }

      try {
        await get().ladeDaySummary();
      } catch (err) {
        console.error('Day Summary Pull failed:', err);
        hasError = true;
      }

      set({
        syncStatus: hasError ? 'error' : 'success',
        lastSync: new Date().toLocaleTimeString('de-LU', {
          hour: '2-digit', minute: '2-digit', second: '2-digit',
        }),
        lastFullSync: new Date().toLocaleTimeString('de-LU', {
          hour: '2-digit', minute: '2-digit', second: '2-digit',
        }),
      });
    }),

    setKioskMode: (mode) => {
      set({ kioskMode: mode });
      get().saveSettings();
    },
    setKioskPin: async (oldPin: string | null, newPin: string | null) => {
      const { kioskPinHash, kioskPinSalt } = get();

      // If a PIN already exists, they must prove the old one first
      if (kioskPinHash && kioskPinSalt) {
        if (!oldPin) {
          return { success: false, error: 'Current PIN required' };
        }
        const oldHash = await hashPin(oldPin, kioskPinSalt);
        if (oldHash !== kioskPinHash) {
          return { success: false, error: 'Incorrect current PIN' };
        }
      }

      if (!newPin) {
        set({ kioskPinHash: null, kioskPinSalt: null, kioskFailedAttempts: 0, kioskLockoutUntil: null });
        get().saveSettings();
        return { success: true };
      }

      const salt = generateSalt();
      const hash = await hashPin(newPin, salt);
      set({ kioskPinHash: hash, kioskPinSalt: salt, kioskFailedAttempts: 0, kioskLockoutUntil: null });
      get().saveSettings();
      return { success: true };
    },

    verifyKioskPin: async (pin) => {
      const { kioskPinHash, kioskPinSalt, kioskFailedAttempts, kioskLockoutUntil } = get();
      if (!kioskPinHash || !kioskPinSalt) return true;

      const now = Date.now();
      if (kioskLockoutUntil && now < kioskLockoutUntil) {
        return false; // Locked out
      }

      const hash = await hashPin(pin, kioskPinSalt);

      // Constant-time comparison
      let isMatch = true;
      if (hash.length !== kioskPinHash.length) {
        isMatch = false;
      }
      const len = Math.min(hash.length, kioskPinHash.length);
      for (let i = 0; i < len; i++) {
        if (hash[i] !== kioskPinHash[i]) {
          isMatch = false;
        }
      }

      if (isMatch) {
        if (kioskFailedAttempts > 0) {
          set({ kioskFailedAttempts: 0, kioskLockoutUntil: null });
          get().saveSettings();
        }
        return true;
      } else {
        const attempts = kioskFailedAttempts + 1;
        if (attempts >= 5) {
          set({ kioskFailedAttempts: attempts, kioskLockoutUntil: now + 30000 });
        } else {
          set({ kioskFailedAttempts: attempts });
        }
        get().saveSettings();
        return false;
      }
    },

    queueCateringBasket: (spielerId, cart) => {
      const state = get();

      if (state.kioskMode !== 'CATERING') {
        return { success: false, error: 'Not in catering mode' };
      }

      if (!state.kredite[spielerId]) {
        return { success: false, error: 'Spiller vum Dag net fonnt (stale player)' };
      }

      const player = state.portalSpieler.find(s => s.id === spielerId) || state.spieler.find(s => s.id === spielerId);
      if (!player) {
        return { success: false, error: 'Onbekannte Spiller' };
      }
      if ('portalAktiv' in player && player.portalAktiv === false) {
        return { success: false, error: 'Spiller ass inaktiv' };
      }

      const MAX_QUEUE = 1000;
      const totalNewItems = Object.keys(cart).length;
      if (state.pendingVerkaeufe.length + totalNewItems > MAX_QUEUE) {
        return { success: false, error: 'Offline queue voll, synchroniséiert w.e.g.' };
      }

      const events: VerkaufEvent[] = [];
      const datum = todayStr();

      for (const [pIdStr, qty] of Object.entries(cart)) {
        const productId = Number(pIdStr);
        if (!Number.isSafeInteger(qty) || qty <= 0) {
          return { success: false, error: `Invalid quantity for product ${productId}` };
        }

        const product = state.produkte.find(p => p.id === productId);
        if (!product || !product.active || (product.category !== 'FOOD' && product.category !== 'DRINK')) {
          return { success: false, error: `Produkt net disponibel: ${product?.name || productId}` };
        }

        if (!product.currentPrice || product.id <= 0) {
          return { success: false, error: `Invalid price for product ${product.name}` };
        }

        events.push({
          externalId: generateSpielId(),
          spielerId,
          datum,
          productId,
          priceRevisionId: product.currentPrice.id,
          quantity: qty,
        });
      }

      if (events.length === 0) {
        return { success: true };
      }

      const pendingVerkaeufe = [...state.pendingVerkaeufe, ...events];
      saveVerkaeufe(PENDING_VERKAEUFE_KEY, pendingVerkaeufe);
      const rolledOver = state.verkaufDatum !== datum;
      if (rolledOver) savePaidBillCache(datum, {});
      set({
        verkaufDatum: datum,
        pendingVerkaeufe,
        verkaeufe: rolledOver ? [] : state.verkaeufe,
        serverVerkaeufe: rolledOver ? [] : state.serverVerkaeufe,
        paidBillCache: rolledOver ? {} : state.paidBillCache,
      });

      return { success: true };
    },
  };
});
