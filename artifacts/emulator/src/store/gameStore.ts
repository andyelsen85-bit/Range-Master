import { create } from 'zustand';

export type Maschine = 'A' | 'B' | 'C' | 'D' | 'E' | 'F' | 'G' | 'H';
export type Modus = 'NORMAL' | 'HARAKIRI' | 'HARAKIRI_DELAYED' | 'HARAKIRI_FULL' | 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3';
export type Screen = 'dashboard' | 'start' | 'spiel' | 'einstellungen';
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
}

export interface PortalSpieler {
  id: number;
  name: string;
  mitgliedNr: string | null;
}

// Custom mode machine sequences
export type CustomSequenz = Maschine[];

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
  customSequenzen: Record<'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3', CustomSequenz>;
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

  // Portal player cache
  portalSpieler: PortalSpieler[];
  portalLaden: boolean;
  portalFehler: string | null;
  spielerAusCache: boolean;  // true when player list was loaded from offline cache

  // Offline queue
  pendingGames: PendingGame[];

  syncStatus: SyncStatus;
  lastSync: string | null;

  // Actions
  setScreen: (screen: Screen) => void;
  setSpieler: (spieler: Spieler[]) => void;
  setSpielerAufPosten: (post: number, data: { id: number; name: string } | null) => void;
  updateSpielerName: (id: number, name: string) => void;
  setModus: (modus: Modus) => void;
  toggleMaschineAktiv: (m: Maschine) => void;
  setCustomSequenz: (modus: 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3', seq: CustomSequenz) => void;
  startSpiel: () => void;
  eintragenErgebnis: (schuss1: boolean, schuss2: boolean) => void;
  wiederholenTaube: () => void;
  ueberspringenTaube: () => void;
  ofbriechenSpiel: () => void;
  werfenTaube: () => void;
  setApiSettings: (url: string, key: string) => void;
  syncAllPending: () => Promise<void>;
  ladeSpielerVomPortal: () => Promise<void>;
  getAktivenSpieler: () => Spieler | undefined;
  saveSettings: () => void;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

const MASCHINEN: Maschine[] = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'];
export { MASCHINEN as MASCHINEN_LIST };

const DEFAULT_MASCHINEN_AKTIV: Record<Maschine, boolean> = {
  A: true, B: true, C: true, D: true, E: true, F: true, G: true, H: true,
};

const DEFAULT_CUSTOM: Record<'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3', CustomSequenz> = {
  CUSTOM_1: ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'],
  CUSTOM_2: ['A', 'C', 'E', 'G', 'B', 'D', 'F', 'H'],
  CUSTOM_3: ['H', 'G', 'F', 'E', 'D', 'C', 'B', 'A'],
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
  customSequenzen: Record<'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3', CustomSequenz>,
): SequenzEintrag[] {
  const single = (['A', 'B', 'C', 'D', 'E', 'F', 'G'] as Maschine[]).filter(m => maschinenAktiv[m]);
  const hAktiv = maschinenAktiv['H'];

  let order: Maschine[];

  if (modus === 'NORMAL') {
    order = [...single, ...(hAktiv ? (['H'] as Maschine[]) : [])];
  } else if (modus === 'HARAKIRI') {
    order = [...shuffleArray(single), ...(hAktiv ? (['H'] as Maschine[]) : [])];
  } else if (modus === 'HARAKIRI_DELAYED') {
    order = [...shuffleArray(single), ...(hAktiv ? (['H'] as Maschine[]) : [])];
  } else if (modus === 'HARAKIRI_FULL') {
    order = shuffleArray([...single, ...(hAktiv ? (['H'] as Maschine[]) : [])]);
  } else {
    // CUSTOM modes: use the sequence exactly as defined — ignore maschinenAktiv
    const key = modus as 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3';
    order = customSequenzen[key];
  }

  // Expand: A–G = 1 entry each; H = 2 entries (Doublette, relay fires once)
  const result: SequenzEintrag[] = [];
  for (const m of order) {
    if (m === 'H') {
      result.push({ maschine: 'H', doubletteNr: 1 });
      result.push({ maschine: 'H', doubletteNr: 2 });
    } else {
      result.push({ maschine: m });
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

/** True for all three Harakiri variants — in those modes each post faces a different machine */
export function isHarakiriModus(modus: Modus): boolean {
  return modus === 'HARAKIRI' || modus === 'HARAKIRI_DELAYED' || modus === 'HARAKIRI_FULL';
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

const SETTINGS_KEY = 'trapmaster-emulator-settings';
const PENDING_KEY = 'trapmaster-pending-games';
const CACHED_SPIELER_KEY = 'trapmaster-cached-spieler';

function loadSettings(): Partial<Settings> {
  try {
    const raw = typeof localStorage !== 'undefined' ? localStorage.getItem(SETTINGS_KEY) : null;
    if (!raw) return {};
    return JSON.parse(raw) as Partial<Settings>;
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

// ─── Store ────────────────────────────────────────────────────────────────────

const saved = loadSettings();

const INIT_SPIELER: Spieler[] = [];

export const useGameStore = create<GameState>((set, get) => {
  const modus: Modus = saved.modus ?? 'NORMAL';
  const maschinenAktiv = saved.maschinenAktiv ?? { ...DEFAULT_MASCHINEN_AKTIV };
  const apiUrl: string = saved.apiUrl ?? '';
  const apiKey: string = saved.apiKey ?? '';
  const customSequenzen = saved.customSequenzen ?? { ...DEFAULT_CUSTOM };

  return {
    // Settings (persisted)
    modus,
    maschinenAktiv,
    apiUrl,
    apiKey,
    customSequenzen,

    // Volatile
    screen: 'dashboard',
    spieler: INIT_SPIELER,
    lauf: 1,
    taubeIndex: 0,
    spielerIndex: 0,
    sequenz: generateSequenz(modus, maschinenAktiv, customSequenzen),
    ergebnisse: [],
    spielId: null,
    portalSpieler: [],
    portalLaden: false,
    portalFehler: null,
    spielerAusCache: false,

    // Offline queue — restored from localStorage on startup
    pendingGames: loadPendingGames(),

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
      saveToStorage({ modus, maschinenAktiv: s.maschinenAktiv, apiUrl: s.apiUrl, apiKey: s.apiKey, customSequenzen: s.customSequenzen });
    },

    toggleMaschineAktiv: (m) => {
      set((state) => {
        const maschinenAktiv = { ...state.maschinenAktiv, [m]: !state.maschinenAktiv[m] };
        saveToStorage({ modus: state.modus, maschinenAktiv, apiUrl: state.apiUrl, apiKey: state.apiKey, customSequenzen: state.customSequenzen });
        return { maschinenAktiv };
      });
    },

    setCustomSequenz: (modus, seq) => {
      set((state) => {
        const customSequenzen = { ...state.customSequenzen, [modus]: seq };
        saveToStorage({ modus: state.modus, maschinenAktiv: state.maschinenAktiv, apiUrl: state.apiUrl, apiKey: state.apiKey, customSequenzen });
        return { customSequenzen };
      });
    },

    startSpiel: () => set((state) => ({
      screen: 'spiel',
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
      syncStatus: 'idle',
    })),

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
      const posten = getCurrentPosten(currentSpieler, effectiveTaubeIdx, state.spieler.length);

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
      if (nextLauf > 2) {
        // ── Game over: enqueue in offline pending list ──────────────────────
        const allErgebnisse = [...state.ergebnisse, neuesErgebnis];

        // Compute per-lauf points for each player (needed for spiel_teilnahmen rows)
        const teilnahmen = updatedSpieler.flatMap(s =>
          ([1, 2] as const).map(l => ({
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
          lauf: 2,
          taubenProLauf: state.sequenz.length,
          abgeschlossen: true,
          teilnahmen,
          ergebnisse: allErgebnisse,
        };

        const newPendingGames = [...state.pendingGames, newPendingGame];
        savePendingGames(newPendingGames);

        return {
          ergebnisse: allErgebnisse,
          spielerIndex: 0,
          taubeIndex: 0,
          lauf: nextLauf,
          screen: 'dashboard' as Screen,
          spieler: updatedSpieler,
          pendingGames: newPendingGames,
          syncStatus: 'idle',
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
      if (nextLauf > 2) {
        return { screen: 'dashboard' as Screen, spielerIndex: 0, taubeIndex: 0, lauf: nextLauf };
      }
      return {
        spielerIndex: 0,
        taubeIndex: 0,
        lauf: nextLauf,
        sequenz: generateSequenz(state.modus, state.maschinenAktiv, state.customSequenzen),
      };
    }),

    ofbriechenSpiel: () => set({ screen: 'dashboard' }),

    werfenTaube: () => {
      // ESP32: triggers GPIO relay. Emulator: no-op.
    },

    setApiSettings: (apiUrl, apiKey) => {
      set({ apiUrl, apiKey });
      const s = get();
      saveToStorage({ modus: s.modus, maschinenAktiv: s.maschinenAktiv, apiUrl, apiKey, customSequenzen: s.customSequenzen });
    },

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
        saveCachedSpieler(data.spieler);
        set({ portalSpieler: data.spieler, portalLaden: false, spielerAusCache: false });
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
      if (state.pendingGames.length === 0) return;
      if (!state.apiUrl || !state.apiKey) {
        set({ syncStatus: 'error' });
        return;
      }
      set({ syncStatus: 'syncing' });

      try {
        const res = await fetch(`${state.apiUrl}/api/sync/spiele`, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'x-api-key': state.apiKey,
          },
          body: JSON.stringify({ spiele: state.pendingGames }),
          signal: AbortSignal.timeout(30000),
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);

        const data = await res.json() as { results: Array<{ status: string }> };
        const synced = data.results?.filter(r => r.status === 'created').length ?? 0;
        const skipped = data.results?.filter(r => r.status === 'skipped').length ?? 0;

        savePendingGames([]);
        set({
          pendingGames: [],
          syncStatus: 'success',
          lastSync: new Date().toLocaleTimeString('de-LU', {
            hour: '2-digit', minute: '2-digit', second: '2-digit',
          }),
        });
        console.log(`Sync: ${synced} created, ${skipped} skipped`);
      } catch {
        set({ syncStatus: 'error' });
      }
    },
  };
});
