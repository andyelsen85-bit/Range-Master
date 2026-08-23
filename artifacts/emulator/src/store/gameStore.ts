import { create } from 'zustand';

export type Maschine = 'A' | 'B' | 'C' | 'D' | 'E' | 'F' | 'G' | 'H';
export type Modus = 'NORMAL' | 'HARAKIRI' | 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4';
export type Screen = 'dashboard' | 'start' | 'spiel' | 'einstellungen' | 'resultate' | 'geschichte' | 'kredite' | 'spillerverwaltung';
export type SyncStatus = 'idle' | 'syncing' | 'success' | 'error';

export interface Spieler {
  id: number;
  name: string;
  punkte: number;
  startPosten: number; // 1–5 starting post position
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
}

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
}

interface GameState extends Settings {
  screen: Screen;
  spieler: Spieler[];

  // In-game tracking
  lauf: number;
  taubeIndex: number;  // which machine in sequenz
  spielerIndex: number; // which player is shooting within this taube (0..N-1)
  sequenz: SequenzEintrag[];
  ergebnisse: Ergebnis[];
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

  // Local game history (last 50, persisted)
  gameHistory: FinishedGame[];
  lastFinishedGame: FinishedGame | null;

  // Day credits (valid only for kreditDatum = today; no carryover)
  kreditDatum: string;
  kredite: Record<number, KreditStand>;
  pendingKredite: KreditEvent[];
  krediteLaden: boolean;

  // Actions
  setScreen: (screen: Screen) => void;
  dismissResultate: () => void;
  setSpieler: (spieler: Spieler[]) => void;
  setSpielerAufPosten: (post: number, data: { id: number; name: string } | null) => void;
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
  /** Create a new local player (negative id) and queue them for portal upload on next sync */
  addLocalSpieler: (name: string) => PortalSpieler;
  syncAllPending: () => Promise<void>;
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

  /** Queue a player edit (name/email/portal activation) for the next sync */
  queueSpielerUpdate: (spielerId: number, changes: { name: string; email: string | null; portalAktiv: boolean }) => void;
  /** Queue a password reset for the next sync (server emails a new password) */
  queuePasswortReset: (spielerId: number) => void;
  /** Poll the portal for email status of already-synced changes */
  refreshSpielerUpdateStatus: () => Promise<void>;
  /** Remove finished (email_sent) entries from the change list */
  clearErledegtSpielerUpdates: () => void;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

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
const PENDING_KEY = 'rangemaster-pending-games';
const CACHED_SPIELER_KEY = 'rangemaster-cached-spieler';
const HISTORY_KEY = 'rangemaster-game-history';
const PENDING_SPIELER_KEY = 'rangemaster-pending-spieler';
const KREDITE_KEY = 'rangemaster-kredite';
const PENDING_KREDITE_KEY = 'rangemaster-pending-kredite';
const SPIELER_UPDATES_KEY = 'rangemaster-spieler-updates';

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

function saveToStorage(settings: Settings) {
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
  } catch {}
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

const INIT_SPIELER: Spieler[] = [];

export const useGameStore = create<GameState>((set, get) => {
  const modus: Modus = saved.modus ?? 'NORMAL';
  const maschinenAktiv = saved.maschinenAktiv ?? { ...DEFAULT_MASCHINEN_AKTIV };
  const apiUrl: string = saved.apiUrl ?? '';
  const apiKey: string = saved.apiKey ?? '';
  const customSequenzen = normalizeCustomSequenzen(saved.customSequenzen);
  const customLaeufe = saved.customLaeufe ?? { ...DEFAULT_CUSTOM_LAEUFE };

  return {
    // Settings (persisted)
    modus,
    maschinenAktiv,
    apiUrl,
    apiKey,
    customSequenzen,
    customLaeufe,

    // Volatile
    screen: 'dashboard',
    spieler: INIT_SPIELER,
    lauf: 1,
    taubeIndex: 0,
    spielerIndex: 0,
    sequenz: generateSequenz(modus, maschinenAktiv, customSequenzen),
    ergebnisse: [],
    spielId: null,
    activeGameCreditUses: [],
    portalSpieler: [],
    portalLaden: false,
    portalFehler: null,
    spielerAusCache: false,

    // Offline queue — restored from localStorage on startup
    pendingGames: loadPendingGames(),
    pendingSpieler: loadPendingSpieler(),
    spielerUpdates: loadSpielerUpdates(),

    // Local history
    gameHistory: loadGameHistory(),
    lastFinishedGame: null,

    // Day credits — restored from localStorage (auto-expired if from another day)
    ...loadKredite(),
    pendingKredite: loadPendingKredite(),
    krediteLaden: false,

    syncStatus: 'idle',
    lastSync: null,

    saveSettings: () => {
      const s = get();
      saveToStorage({
        modus: s.modus,
        maschinenAktiv: s.maschinenAktiv,
        apiUrl: s.apiUrl,
        apiKey: s.apiKey,
        customSequenzen: s.customSequenzen,
        customLaeufe: s.customLaeufe,
      });
    },

    setScreen: (screen) => set({ screen }),

    setSpieler: (spieler) => set({ spieler }),

    setSpielerAufPosten: (post, data) => set((state) => {
      // Remove any existing occupant at this post
      let next = state.spieler.filter(s => s.startPosten !== post);
      if (data !== null) {
        // Also remove this player from any other post they were assigned to
        next = next.filter(s => s.id !== data.id);
        next = [...next, { id: data.id, name: data.name, punkte: 0, startPosten: post }];
      }
      return { spieler: next.sort((a, b) => a.startPosten - b.startPosten) };
    }),

    updateSpielerName: (id, name) => set((state) => ({
      spieler: state.spieler.map(s => s.id === id ? { ...s, name } : s),
    })),

    setModus: (modus) => {
      set({ modus });
      const s = get();
      saveToStorage({ modus, maschinenAktiv: s.maschinenAktiv, apiUrl: s.apiUrl, apiKey: s.apiKey, customSequenzen: s.customSequenzen, customLaeufe: s.customLaeufe });
    },

    toggleMaschineAktiv: (m) => {
      set((state) => {
        const maschinenAktiv = { ...state.maschinenAktiv, [m]: !state.maschinenAktiv[m] };
        saveToStorage({ modus: state.modus, maschinenAktiv, apiUrl: state.apiUrl, apiKey: state.apiKey, customSequenzen: state.customSequenzen, customLaeufe: state.customLaeufe });
        return { maschinenAktiv };
      });
    },

    setCustomSequenz: (modus, seq) => {
      set((state) => {
        const customSequenzen = { ...state.customSequenzen, [modus]: seq };
        saveToStorage({ modus: state.modus, maschinenAktiv: state.maschinenAktiv, apiUrl: state.apiUrl, apiKey: state.apiKey, customSequenzen, customLaeufe: state.customLaeufe });
        return { customSequenzen };
      });
    },

    setCustomLaeufe: (modus, laeufe) => {
      set((state) => {
        const customLaeufe = { ...state.customLaeufe, [modus]: laeufe };
        saveToStorage({ modus: state.modus, maschinenAktiv: state.maschinenAktiv, apiUrl: state.apiUrl, apiKey: state.apiKey, customSequenzen: state.customSequenzen, customLaeufe });
        return { customLaeufe };
      });
    },

    startSpiel: () => set((state) => {
      // ── Deduct one day credit per participating player ────────────────────
      const { kreditDatum, kredite } = rolledKredite(state);
      if (state.pendingKredite.length + state.spieler.length >
          MAX_PENDING_KREDIT_EVENTS) {
        // Match the terminal: do not begin a game unless every USE event can
        // be kept for a safe retry or a future cancellation refund.
        return state;
      }
      const nextKredite = { ...kredite };
      const newEvents: KreditEvent[] = [];
      for (const s of state.spieler) {
        const stand = nextKredite[s.id] ?? { gewaehrt: 0, verbraucht: 0 };
        nextKredite[s.id] = { ...stand, verbraucht: stand.verbraucht + 1 };
        newEvents.push({
          externalId: generateSpielId(),
          spielerId: s.id,
          datum: kreditDatum,
          typ: 'USE',
          anzahl: 1,
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
        ergebnisse: [],
        sequenz: generateSequenz(state.modus, state.maschinenAktiv, state.customSequenzen),
        // Ensure consistent game order by startPosten; reset points
        spieler: [...state.spieler]
          .sort((a, b) => a.startPosten - b.startPosten)
          .map(s => ({ ...s, punkte: 0 })),
        spielId: generateSpielId(),
        syncStatus: 'idle' as SyncStatus,
        kreditDatum,
        kredite: nextKredite,
        pendingKredite,
        activeGameCreditUses: newEvents,
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
          spielerIndex: nextSpielerIndex,
          spieler: updatedSpieler,
        };
      }

      // All players shot this taube → advance taube
      const nextTaubeIndex = state.taubeIndex + 1;

      if (nextTaubeIndex < state.sequenz.length) {
        return {
          ergebnisse: [...state.ergebnisse, neuesErgebnis],
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
        };
      }

      return {
        ergebnisse: [...state.ergebnisse, neuesErgebnis],
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
        return { spielerIndex: nextSpielerIndex };
      }
      const nextTaubeIndex = state.taubeIndex + 1;
      if (nextTaubeIndex < state.sequenz.length) {
        return { spielerIndex: 0, taubeIndex: nextTaubeIndex };
      }
      const nextLauf = state.lauf + 1;
      const maxLaeufe: number =
        (state.modus === 'CUSTOM_1' || state.modus === 'CUSTOM_2' || state.modus === 'CUSTOM_3' || state.modus === 'CUSTOM_4')
          ? state.customLaeufe[state.modus]
          : 2;
      if (nextLauf > maxLaeufe) {
        // ueberspringen path: skip showing resultate, just go to dashboard
        return { screen: 'dashboard' as Screen, spielerIndex: 0, taubeIndex: 0, lauf: nextLauf };
      }
      return {
        spielerIndex: 0,
        taubeIndex: 0,
        lauf: nextLauf,
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
          spielId: null,
          activeGameCreditUses: [],
          kreditDatum,
          kredite: nextKredite,
          pendingKredite,
        };
      });
      return canceled;
    },

    werfenTaube: () => {
      // ESP32: triggers GPIO relay. Emulator: no-op.
    },

    setApiSettings: (apiUrl, apiKey) => {
      set({ apiUrl, apiKey });
      const s = get();
      saveToStorage({ modus: s.modus, maschinenAktiv: s.maschinenAktiv, apiUrl, apiKey, customSequenzen: s.customSequenzen, customLaeufe: s.customLaeufe });
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
      const pendingKredite: KreditEvent[] = [
        ...state.pendingKredite,
        { externalId: generateSpielId(), spielerId, datum: kreditDatum, typ: 'GRANT' as const, anzahl: -effective },
      ];
      saveKredite(kreditDatum, nextKredite);
      savePendingKredite(pendingKredite);
      return { kreditDatum, kredite: nextKredite, pendingKredite };
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
        saveKredite(today, merged);
        set({ kreditDatum: today, kredite: merged, krediteLaden: false });
      } catch {
        set({ krediteLaden: false });
      }
    },

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
          set({ portalSpieler: cached, portalLaden: false, spielerAusCache: true });
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
        set({ portalSpieler: merged, portalLaden: false, spielerAusCache: false });
      } catch {
        // Network failed — fall back to cache
        const cached = loadCachedSpieler();
        if (cached.length) {
          set({
            portalSpieler: cached,
            portalFehler: `Offline – ${cached.length} Spillesch aus Cache gelued`,
            portalLaden: false,
            spielerAusCache: true,
          });
        } else {
          set({
            portalFehler: 'Verbindung fehlgeschloen. Kee Cache disponibel.',
            portalLaden: false,
          });
        }
      }
    },

    syncAllPending: async () => {
      const state = get();
      if (!state.apiUrl || !state.apiKey) {
        set({ syncStatus: 'error' });
        return;
      }
      set({ syncStatus: 'syncing' });

      try {
        // ── Step 1: push locally created players first, get server IDs ──────
        if (state.pendingSpieler.length > 0) {
          const resSp = await fetch(`${state.apiUrl}/api/sync/spieler`, {
            method: 'POST',
            headers: {
              'Content-Type': 'application/json',
              'x-api-key': state.apiKey,
            },
            body: JSON.stringify({
              spieler: state.pendingSpieler.map(p => ({ id: p.localId, name: p.name })),
            }),
            signal: AbortSignal.timeout(15000),
          });
          if (!resSp.ok) throw new Error(`HTTP ${resSp.status}`);
          const spData = await resSp.json() as {
            mappings?: Array<{ localId: number; id: number; name: string }>;
          };

          const idMap = new Map<number, number>();
          for (const m of spData.mappings ?? []) idMap.set(m.localId, m.id);

          // Remap local IDs → server IDs everywhere they are referenced
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
            spielerNamen: Object.fromEntries(
              Object.entries(g.spielerNamen).map(([id, name]) => [remap(Number(id)), name])
            ),
          }));
          const remappedSpieler = s2.spieler.map(s => ({ ...s, id: remap(s.id) }));
            const remappedKreditEvents = s2.pendingKredite.map(e => ({ ...e, spielerId: remap(e.spielerId) }));
            const remappedActiveCreditUses = s2.activeGameCreditUses.map(e => ({
              ...e, spielerId: remap(e.spielerId),
            }));
          const remappedUpdates = s2.spielerUpdates.map(u => ({ ...u, spielerId: remap(u.spielerId) }));
          const remappedKredite = Object.fromEntries(
            Object.entries(s2.kredite).map(([id, stand]) => [remap(Number(id)), stand])
          ) as Record<number, KreditStand>;
          const remappedPortal = s2.portalSpieler.map(p =>
            idMap.has(p.id) ? { ...p, id: idMap.get(p.id)!, lokal: undefined } : p
          );
          // Only clear players the server acknowledged
          const stillPending = s2.pendingSpieler.filter(p => !idMap.has(p.localId));

          savePendingGames(remappedPending);
          savePendingSpieler(stillPending);
          saveCachedSpieler(remappedPortal);
          saveGameHistory(remappedHistory);
          savePendingKredite(remappedKreditEvents);
          saveKredite(s2.kreditDatum, remappedKredite);
          saveSpielerUpdates(remappedUpdates);
          set({
            pendingGames: remappedPending,
            pendingSpieler: stillPending,
            spieler: remappedSpieler,
            portalSpieler: remappedPortal,
            gameHistory: remappedHistory,
            pendingKredite: remappedKreditEvents,
              activeGameCreditUses: remappedActiveCreditUses,
            kredite: remappedKredite,
            spielerUpdates: remappedUpdates,
          });
        }

        // ── Step 1c: push pending player edits / password resets ─────────────
        {
          const pushable = get().spielerUpdates.filter(u => u.status === 'pending' && u.spielerId > 0);
          if (pushable.length > 0) {
            const resU = await fetch(`${state.apiUrl}/api/sync/spieler-updates`, {
              method: 'POST',
              headers: {
                'Content-Type': 'application/json',
                'x-api-key': state.apiKey,
              },
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
            const dataU = await resU.json() as {
              results: Array<{ externalId: string; status: string; emailStatus: string; error?: string }>;
            };
            const byId = new Map(dataU.results.map(r => [r.externalId, r]));
            const spielerUpdates = get().spielerUpdates.map(u => {
              const r = byId.get(u.externalId);
              if (!r) return u;
              if (r.status === 'error') return { ...u, status: 'email_failed' as const, fehler: r.error ?? 'Feeler' };
              if (r.emailStatus === 'SENT') return { ...u, status: 'email_sent' as const, fehler: undefined };
              if (r.emailStatus === 'FAILED' || r.emailStatus === 'PENDING') return { ...u, status: 'email_failed' as const, fehler: 'Email nach net verschéckt' };
              return { ...u, status: 'synced' as const, fehler: undefined };
            });
            saveSpielerUpdates(spielerUpdates);
            set({ spielerUpdates });
          }
          // Poll email status for anything still open (also retries failed sends server-side)
          await get().refreshSpielerUpdateStatus();
        }

        // ── Step 1b: push pending credit events (only remapped/server IDs) ───
        {
          const allEvents = get().pendingKredite;
          const pushable = allEvents.filter(e => e.spielerId > 0);
          if (pushable.length > 0) {
            const sentIds = new Set(pushable.map(event => event.externalId));
            for (const event of pushable) kreditEventsInFlight.add(event.externalId);
            try {
              const resK = await fetch(`${state.apiUrl}/api/sync/kredite`, {
                method: 'POST',
                headers: {
                  'Content-Type': 'application/json',
                  'x-api-key': state.apiKey,
                },
                body: JSON.stringify({ events: pushable }),
                signal: AbortSignal.timeout(15000),
              });
              if (!resK.ok) throw new Error(`HTTP ${resK.status}`);
              // Remove only the sent snapshot. A cancel during this request
              // may add a GRANT that must remain for the next sync.
              const stillPendingKredite = get().pendingKredite.filter(
                event => !sentIds.has(event.externalId),
              );
              savePendingKredite(stillPendingKredite);
              set({ pendingKredite: stillPendingKredite });
              // Reconcile today's state from the server
              await get().ladeKredite();
            } finally {
              for (const event of pushable) kreditEventsInFlight.delete(event.externalId);
            }
          }
        }

        // ── Step 2: push pending games ───────────────────────────────────────
        if (get().pendingGames.length > 0) {
          const res = await fetch(`${state.apiUrl}/api/sync/spiele`, {
            method: 'POST',
            headers: {
              'Content-Type': 'application/json',
              'x-api-key': state.apiKey,
            },
            body: JSON.stringify({ spiele: get().pendingGames }),
            signal: AbortSignal.timeout(30000),
          });
          if (!res.ok) throw new Error(`HTTP ${res.status}`);

          const data = await res.json() as { results: Array<{ status: string }> };
          const synced = data.results?.filter(r => r.status === 'created').length ?? 0;
          const skipped = data.results?.filter(r => r.status === 'skipped').length ?? 0;

          savePendingGames([]);
          set({ pendingGames: [] });
          console.log(`Sync: ${synced} created, ${skipped} skipped`);
        }

      // ── Step 3: pull recent games from server → merge into local history ──
      try {
        const pullRes = await fetch(`${get().apiUrl}/api/sync/spiele?limit=100`, {
          headers: { 'x-api-key': get().apiKey },
          signal: AbortSignal.timeout(15000),
        });
        if (pullRes.ok) {
          const pullData = await pullRes.json() as {
            spiele: Array<{
              externalId: string;
              datum: string;
              modus: Modus;
              lauf: number;
              taubenProLauf: number;
              abgeschlossen: boolean;
              teilnahmen: Array<{ spielerId: number; startPosten: number; punkte: number; lauf: number }>;
              spielerNamen: Record<number, string>;
            }>;
          };
          const existingIds = new Set(get().gameHistory.map(g => g.externalId));
          const newGames: FinishedGame[] = (pullData.spiele ?? [])
            .filter(g => !existingIds.has(g.externalId))
            .map(g => ({
              externalId: g.externalId,
              datum: g.datum,
              modus: g.modus,
              lauf: g.lauf,
              taubenProLauf: g.taubenProLauf,
              abgeschlossen: g.abgeschlossen,
              teilnahmen: g.teilnahmen,
              ergebnisse: [],
              finishedAt: g.datum,
              spielerNamen: Object.fromEntries(
                Object.entries(g.spielerNamen).map(([k, v]) => [Number(k), v])
              ),
            }));
          if (newGames.length > 0) {
            const merged = [...newGames, ...get().gameHistory]
              .sort((a, b) => new Date(b.finishedAt).getTime() - new Date(a.finishedAt).getTime())
              .slice(0, 50);
            saveGameHistory(merged);
            set({ gameHistory: merged });
          }
        }
      } catch {
        // pull failure is non-fatal — push already succeeded
      }

      set({
        syncStatus: 'success',
        lastSync: new Date().toLocaleTimeString('de-LU', {
          hour: '2-digit', minute: '2-digit', second: '2-digit',
        }),
      });
      } catch {
        set({ syncStatus: 'error' });
      }
    },
  };
});
