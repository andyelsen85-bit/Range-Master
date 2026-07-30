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

  syncStatus: SyncStatus;
  lastSync: string | null;

  // Actions
  setScreen: (screen: Screen) => void;
  setSpieler: (spieler: Spieler[]) => void;
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
  syncPortal: () => Promise<void>;
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
    // CUSTOM modes
    const key = modus as 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3';
    order = customSequenzen[key].filter(m => maschinenAktiv[m]);
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
  const n = Math.min(totalSpieler, 5); // only 5 posts
  return ((spieler.startPosten - 1 + taubeIndex) % 5) + 1;
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

// ─── Store ────────────────────────────────────────────────────────────────────

const saved = loadSettings();

const INIT_SPIELER: Spieler[] = [
  { id: 1, name: 'Schütze 1', punkte: 0, startPosten: 1 },
  { id: 2, name: 'Schütze 2', punkte: 0, startPosten: 2 },
  { id: 3, name: 'Schütze 3', punkte: 0, startPosten: 3 },
];

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

    setSpieler: (spieler) => set({ spieler: assignPostenToSpieler(spieler) }),

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
      spieler: state.spieler.map(s => ({ ...s, punkte: 0 })),
      spielId: generateSpielId(),
      syncStatus: 'idle',
    })),

    eintragenErgebnis: (schuss1, schuss2) => set((state) => {
      const pts = schuss1 ? 2 : schuss2 ? 1 : 0;
      const currentSpieler = state.spieler[state.spielerIndex];
      if (!currentSpieler) return state;

      const eintrag = state.sequenz[state.taubeIndex];
      const posten = getCurrentPosten(currentSpieler, state.taubeIndex, state.spieler.length);

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
        // Game over
        return {
          ergebnisse: [...state.ergebnisse, neuesErgebnis],
          spielerIndex: 0,
          taubeIndex: 0,
          lauf: nextLauf,
          screen: 'dashboard' as Screen,
          spieler: updatedSpieler,
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
      if (!state.apiUrl && !state.apiKey) {
        set({ portalFehler: 'API URL / Key net konfiguriert (Astellungen)', portalLaden: false });
        return;
      }
      set({ portalLaden: true, portalFehler: null });
      try {
        const baseUrl = state.apiUrl || '';
        const res = await fetch(`${baseUrl}/api/sync/spieler`, {
          headers: { 'x-api-key': state.apiKey },
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json() as { spieler: PortalSpieler[] };
        set({ portalSpieler: data.spieler, portalLaden: false });
      } catch (e: unknown) {
        set({
          portalFehler: e instanceof Error ? e.message : 'Onbekannte Feeler',
          portalLaden: false,
        });
      }
    },

    syncPortal: async () => {
      const state = get();
      if (!state.spielId || state.ergebnisse.length === 0) return;
      set({ syncStatus: 'syncing' });

      const teilnahmen = state.spieler.map(s => ({
        spielerId: s.id,
        startPosten: s.startPosten,
        punkte: s.punkte,
        lauf: state.lauf,
      }));

      const payload = {
        spiele: [{
          externalId: state.spielId,
          datum: new Date().toISOString(),
          modus: state.modus,
          lauf: state.lauf,
          abgeschlossen: false,
          teilnahmen,
          ergebnisse: state.ergebnisse,
        }],
      };

      try {
        const baseUrl = state.apiUrl || '';
        const res = await fetch(`${baseUrl}/api/sync/spiele`, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'x-api-key': state.apiKey,
          },
          body: JSON.stringify(payload),
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        set({
          syncStatus: 'success',
          lastSync: new Date().toLocaleTimeString('de-LU', { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
        });
      } catch {
        set({ syncStatus: 'error' });
      }
    },
  };
});
