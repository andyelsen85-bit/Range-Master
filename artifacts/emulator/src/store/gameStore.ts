import { create } from 'zustand';

export type Maschine = 'A' | 'B' | 'C' | 'D' | 'E' | 'F' | 'G' | 'H';
export type Modus = 'NORMAL' | 'HARAKIRI' | 'HARAKIRI_DELAYED' | 'HARAKIRI_FULL' | 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3';
export type Screen = 'dashboard' | 'start' | 'spiel' | 'einstellungen';
export type SyncStatus = 'idle' | 'syncing' | 'success' | 'error';

export interface Spieler {
  id: number;
  name: string;
  punkte: number;
  startPosten: number; // initial post 1-5
}

export interface Ergebnis {
  spielerId: number;
  lauf: number;
  taube: number;    // 1-9 within a Lauf
  maschine: Maschine;
  posten: number;
  schuss1: boolean;
  schuss2: boolean;
  punkte: number;
  wiederholt: boolean;
}

// Each entry in the sequenz: a machine letter + whether it's the 2nd Doublette pigeon
export interface SequenzEintrag {
  maschine: Maschine;
  doubletteNr?: 1 | 2; // undefined for single, 1 or 2 for H
}

interface GameState {
  screen: Screen;
  spieler: Spieler[];
  modus: Modus;
  lauf: number;        // 1 or 2
  taubeIndex: number;  // 0 to 8 (9 tauben per Lauf)
  sequenz: SequenzEintrag[];
  ergebnisse: Ergebnis[];
  maschinenAktiv: Record<Maschine, boolean>; // whether to include in sequence
  doubletteVersatz: number; // 0 to 3.0 seconds
  syncStatus: SyncStatus;
  lastSync: string | null;
  apiUrl: string;
  apiKey: string;
  spielId: string | null; // UUID for current session

  // Actions
  setScreen: (screen: Screen) => void;
  setSpieler: (spieler: Spieler[]) => void;
  updateSpielerName: (id: number, name: string) => void;
  setModus: (modus: Modus) => void;
  toggleMaschineAktiv: (m: Maschine) => void;
  startSpiel: () => void;
  eintragenErgebnis: (schuss1: boolean, schuss2: boolean) => void;
  wiederholenTaube: () => void;
  ueberspringenTaube: () => void;
  ofbriechenSpiel: () => void;
  werfenTaube: () => void;
  setDoubletteVersatz: (val: number) => void;
  setApiSettings: (url: string, key: string) => void;
  syncPortal: () => Promise<void>;
  getAktivenSpieler: () => Spieler;
}

const MASCHINEN: Maschine[] = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'];

const DEFAULT_MASCHINEN_AKTIV: Record<Maschine, boolean> = {
  A: true, B: true, C: true, D: true, E: true, F: true, G: true, H: true
};

const INITIAL_SPIELER: Spieler[] = [
  { id: 1, name: 'Demo Schütze 1', punkte: 0, startPosten: 1 },
  { id: 2, name: 'Demo Schütze 2', punkte: 0, startPosten: 2 },
  { id: 3, name: 'Demo Schütze 3', punkte: 0, startPosten: 3 },
];

function shuffleArray<T>(arr: T[]): T[] {
  const a = [...arr];
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}

function generateSequenz(modus: Modus, maschinenAktiv: Record<Maschine, boolean>): SequenzEintrag[] {
  const single = (['A', 'B', 'C', 'D', 'E', 'F', 'G'] as Maschine[]).filter(m => maschinenAktiv[m]);
  const hAktiv = maschinenAktiv['H'];

  let order: Maschine[];

  switch (modus) {
    case 'NORMAL':
      order = [...single, ...(hAktiv ? ['H' as Maschine] : [])];
      break;
    case 'HARAKIRI':
      // A-G shuffled, H last
      order = [...shuffleArray(single), ...(hAktiv ? ['H' as Maschine] : [])];
      break;
    case 'HARAKIRI_DELAYED':
      // A-G shuffled in pairs, H last
      order = [...shuffleArray(single), ...(hAktiv ? ['H' as Maschine] : [])];
      break;
    case 'HARAKIRI_FULL':
      // All A-H shuffled (H included in shuffle)
      order = shuffleArray([...single, ...(hAktiv ? ['H' as Maschine] : [])]);
      break;
    default:
      // CUSTOM modes: normal order for now
      order = [...single, ...(hAktiv ? ['H' as Maschine] : [])];
  }

  // Expand: single machines = 1 entry, H = 2 entries (Doublette)
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
  // Simple UUID v4-like
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => {
    const r = Math.random() * 16 | 0;
    return (c === 'x' ? r : (r & 0x3 | 0x8)).toString(16);
  });
}

/** Which player is at the shooting post for the current taubeIndex within a lauf */
function getAktivenSpielerFromState(state: Pick<GameState, 'spieler' | 'taubeIndex'>): Spieler {
  if (state.spieler.length === 0) return { id: 0, name: '?', punkte: 0, startPosten: 1 };
  // posten = ((startPosten - 1 + taubeIndex) % anzahlSpieler) + 1 would be per-player
  // But the correct rule is: activePosten = (taubeIndex % 5) + 1,
  // find which player's current posten matches.
  // Player current posten = ((startPosten - 1 + taubeIndex) % spielerCount) + 1
  const n = state.spieler.length;
  const activePosten = (state.taubeIndex % n) + 1;
  return state.spieler.find(s => {
    const curPosten = ((s.startPosten - 1 + state.taubeIndex) % n) + 1;
    return curPosten === activePosten;
  }) ?? state.spieler[state.taubeIndex % state.spieler.length];
}

export const useGameStore = create<GameState>((set, get) => ({
  screen: 'dashboard',
  spieler: INITIAL_SPIELER,
  modus: 'NORMAL',
  lauf: 1,
  taubeIndex: 0,
  sequenz: generateSequenz('NORMAL', DEFAULT_MASCHINEN_AKTIV),
  ergebnisse: [],
  maschinenAktiv: { ...DEFAULT_MASCHINEN_AKTIV },
  doubletteVersatz: 0,
  syncStatus: 'idle',
  lastSync: null,
  apiUrl: '',
  apiKey: '',
  spielId: null,

  setScreen: (screen) => set({ screen }),

  setSpieler: (spieler) => set({ spieler }),

  updateSpielerName: (id, name) => set((state) => ({
    spieler: state.spieler.map(s => s.id === id ? { ...s, name } : s)
  })),

  setModus: (modus) => set({ modus }),

  toggleMaschineAktiv: (m) => set((state) => ({
    maschinenAktiv: { ...state.maschinenAktiv, [m]: !state.maschinenAktiv[m] }
  })),

  startSpiel: () => set((state) => ({
    screen: 'spiel',
    lauf: 1,
    taubeIndex: 0,
    ergebnisse: [],
    sequenz: generateSequenz(state.modus, state.maschinenAktiv),
    spieler: state.spieler.map(s => ({ ...s, punkte: 0 })),
    spielId: generateSpielId(),
    syncStatus: 'idle',
  })),

  eintragenErgebnis: (schuss1, schuss2) => set((state) => {
    const pts = schuss1 ? 2 : schuss2 ? 1 : 0;

    const aktiverSpieler = getAktivenSpielerFromState(state);
    const eintrag = state.sequenz[state.taubeIndex];

    const neuesErgebnis: Ergebnis = {
      spielerId: aktiverSpieler.id,
      lauf: state.lauf,
      taube: state.taubeIndex + 1,
      maschine: eintrag.maschine,
      posten: ((aktiverSpieler.startPosten - 1 + state.taubeIndex) % state.spieler.length) + 1,
      schuss1,
      schuss2,
      punkte: pts,
      wiederholt: false,
    };

    const nextIndex = state.taubeIndex + 1;
    const laufLen = state.sequenz.length;
    let nextLauf = state.lauf;
    let nextTaubeIndex = nextIndex;
    let nextScreen: Screen = state.screen;
    let nextSequenz = state.sequenz;

    if (nextIndex >= laufLen) {
      // Lauf complete
      nextLauf = state.lauf + 1;
      nextTaubeIndex = 0;
      if (nextLauf > 2) {
        // Game over — go to results/dashboard
        nextScreen = 'dashboard';
      } else {
        nextSequenz = generateSequenz(state.modus, state.maschinenAktiv);
      }
    }

    return {
      ergebnisse: [...state.ergebnisse, neuesErgebnis],
      taubeIndex: nextTaubeIndex,
      lauf: nextLauf,
      screen: nextScreen,
      sequenz: nextSequenz,
      spieler: state.spieler.map(s =>
        s.id === aktiverSpieler.id ? { ...s, punkte: s.punkte + pts } : s
      ),
    };
  }),

  wiederholenTaube: () => set((state) => {
    // Remove the last result and step back one taube (broken bird re-throw)
    if (state.ergebnisse.length === 0) return state;
    return {
      ergebnisse: state.ergebnisse.slice(0, -1),
      taubeIndex: state.taubeIndex > 0 ? state.taubeIndex - 1 : 0,
      spieler: state.spieler.map(s => {
        const last = state.ergebnisse[state.ergebnisse.length - 1];
        if (last && last.spielerId === s.id) {
          return { ...s, punkte: Math.max(0, s.punkte - last.punkte) };
        }
        return s;
      }),
    };
  }),

  ueberspringenTaube: () => set((state) => {
    const nextIndex = state.taubeIndex + 1;
    const laufLen = state.sequenz.length;
    if (nextIndex >= laufLen) {
      const nextLauf = state.lauf + 1;
      if (nextLauf > 2) {
        return { screen: 'dashboard', taubeIndex: 0, lauf: nextLauf };
      }
      return {
        taubeIndex: 0,
        lauf: nextLauf,
        sequenz: generateSequenz(state.modus, state.maschinenAktiv),
      };
    }
    return { taubeIndex: nextIndex };
  }),

  ofbriechenSpiel: () => set({ screen: 'dashboard' }),

  werfenTaube: () => {
    // In the real ESP32, this triggers a GPIO relay pulse.
    // In the emulator this is a no-op — the button is just visual confirmation.
  },

  setDoubletteVersatz: (val) => set({ doubletteVersatz: val }),

  setApiSettings: (apiUrl, apiKey) => set({ apiUrl, apiKey }),

  getAktivenSpieler: () => getAktivenSpielerFromState(get()),

  syncPortal: async () => {
    const state = get();
    if (!state.spielId || state.ergebnisse.length === 0) return;

    set({ syncStatus: 'syncing' });

    // Build the payload for POST /api/sync/spiele
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
}));

export const MASCHINEN_LIST = MASCHINEN;
