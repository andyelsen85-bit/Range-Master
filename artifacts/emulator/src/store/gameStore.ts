import { create } from 'zustand';

export type Maschine = 'A' | 'B' | 'C' | 'D' | 'E' | 'F' | 'G' | 'H';
export type Modus = 'NORMAL' | 'HARAKIRI' | 'CUSTOM_1';
export type Screen = 'dashboard' | 'start' | 'spiel' | 'einstellungen';
export type MaschineStatus = 'ok' | 'fehler' | 'offline';
export type SyncStatus = 'idle' | 'syncing' | 'success' | 'error';

export interface Spieler { 
  id: number; 
  name: string; 
  punkte: number; 
  posten: number; 
}

export interface Ergebnis { 
  spielerId: number; 
  taube: number; 
  maschine: Maschine; 
  posten: number; 
  schuss1: boolean; 
  schuss2: boolean; 
  punkte: number; 
}

interface GameState {
  screen: Screen;
  spieler: Spieler[];
  modus: Modus;
  lauf: number;
  taubeIndex: number; // 0 to 7
  sequenz: Maschine[];
  ergebnisse: Ergebnis[];
  maschinenStatus: Record<Maschine, MaschineStatus>;
  mikrofon: boolean;
  doubletteVersatz: number; // 0 to 3.0
  syncStatus: SyncStatus;
  lastSync: string | null;
  apiUrl: string;
  apiKey: string;

  // Actions
  setScreen: (screen: Screen) => void;
  setSpieler: (spieler: Spieler[]) => void;
  updateSpielerName: (id: number, name: string) => void;
  setModus: (modus: Modus) => void;
  startSpiel: () => void;
  eintragenErgebnis: (schuss1: boolean, schuss2: boolean) => void;
  wiederholenTaube: () => void;
  ueberspringenTaube: () => void;
  ofbriechenSpiel: () => void;
  werfenTaube: () => void;
  toggleMikrofon: () => void;
  setMaschineStatus: (m: Maschine, status: MaschineStatus) => void;
  toggleMaschineStatus: (m: Maschine) => void;
  setDoubletteVersatz: (val: number) => void;
  setApiSettings: (url: string, key: string) => void;
  syncPortal: () => Promise<void>;
}

const DEFAULT_MASCHINEN: Record<Maschine, MaschineStatus> = {
  A: 'ok', B: 'ok', C: 'ok', D: 'ok', E: 'ok', F: 'ok', G: 'ok', H: 'ok'
};

const INITIAL_SPIELER: Spieler[] = [
  { id: 1, name: 'Demo Schütze 1', punkte: 0, posten: 1 },
  { id: 2, name: 'Demo Schütze 2', punkte: 0, posten: 2 },
  { id: 3, name: 'Demo Schütze 3', punkte: 0, posten: 3 },
  { id: 4, name: 'Demo Schütze 4', punkte: 0, posten: 4 },
  { id: 5, name: 'Demo Schütze 5', punkte: 0, posten: 5 },
];

function generateSequenz(modus: Modus): Maschine[] {
  if (modus === 'NORMAL') {
    return ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'];
  } else if (modus === 'HARAKIRI') {
    const arr: Maschine[] = ['A', 'B', 'C', 'D', 'E', 'F', 'G'];
    // Fisher-Yates shuffle
    for (let i = arr.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [arr[i], arr[j]] = [arr[j], arr[i]];
    }
    return [...arr, 'H'];
  }
  return ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'];
}

export const useGameStore = create<GameState>((set, get) => ({
  screen: 'dashboard',
  spieler: INITIAL_SPIELER.slice(0, 3), // default 3 players
  modus: 'NORMAL',
  lauf: 1,
  taubeIndex: 0,
  sequenz: ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'],
  ergebnisse: [],
  maschinenStatus: { ...DEFAULT_MASCHINEN },
  mikrofon: false,
  doubletteVersatz: 0,
  syncStatus: 'idle',
  lastSync: null,
  apiUrl: 'https://api.trapmaster.lu',
  apiKey: 'tm_demokey_123',

  setScreen: (screen) => set({ screen }),
  
  setSpieler: (spieler) => set({ spieler }),
  
  updateSpielerName: (id, name) => set((state) => ({
    spieler: state.spieler.map(s => s.id === id ? { ...s, name } : s)
  })),
  
  setModus: (modus) => set({ modus }),
  
  startSpiel: () => set((state) => ({
    screen: 'spiel',
    lauf: 1,
    taubeIndex: 0,
    ergebnisse: [],
    sequenz: generateSequenz(state.modus),
    spieler: state.spieler.map(s => ({ ...s, punkte: 0 }))
  })),
  
  eintragenErgebnis: (schuss1, schuss2) => set((state) => {
    let pts = 0;
    if (schuss1) pts = 2;
    else if (schuss2) pts = 1;

    // determine current player based on posten
    // posten runs 1-5. It rotates. 
    // formula from prompt: ((spielerIndex + taubeIndex) % 5) + 1
    // Wait, the prompt says: aktiverSpieler() = spieler at posten matching current (taubeIndex % 5) + 1
    // actually let's just find the spieler who is at that posten.
    const activePosten = (state.taubeIndex % 5) + 1;
    const currentSpieler = state.spieler.find(s => {
      // The prompt says: aktiverSpieler() = spieler at posten matching current (taubeIndex % 5) + 1
      // Initial posten is s.posten
      // We assume s.posten is the spielerIndex + 1 (1-based).
      // So spielerIndex = s.posten - 1.
      const p = ((s.posten - 1 + state.taubeIndex) % state.spieler.length) + 1;
      return p === activePosten;
    }) || state.spieler[0];

    const newErgebnis: Ergebnis = {
      spielerId: currentSpieler.id,
      taube: state.taubeIndex + 1,
      maschine: state.sequenz[state.taubeIndex],
      posten: activePosten,
      schuss1,
      schuss2,
      punkte: pts
    };

    const nextTaube = state.taubeIndex + 1;
    let nextLauf = state.lauf;
    let nextScreen = state.screen;
    let nextSequenz = state.sequenz;

    if (nextTaube >= 8) {
      nextLauf += 1;
      if (nextLauf > 5) {
        // Game over
        nextScreen = 'dashboard';
      } else {
        nextSequenz = generateSequenz(state.modus);
      }
    }

    return {
      ergebnisse: [...state.ergebnisse, newErgebnis],
      taubeIndex: nextTaube >= 8 ? 0 : nextTaube,
      lauf: nextLauf,
      screen: nextScreen,
      sequenz: nextSequenz,
      spieler: state.spieler.map(s => 
        s.id === currentSpieler.id ? { ...s, punkte: s.punkte + pts } : s
      )
    };
  }),

  wiederholenTaube: () => {
    // Just re-throw or something, no state change to taubeIndex
  },

  ueberspringenTaube: () => set((state) => {
    const nextTaube = state.taubeIndex + 1;
    if (nextTaube >= 8) {
      return { taubeIndex: 0, lauf: state.lauf + 1, sequenz: generateSequenz(state.modus) };
    }
    return { taubeIndex: nextTaube };
  }),

  ofbriechenSpiel: () => set({
    screen: 'dashboard'
  }),
  
  werfenTaube: () => {
    // Hardware trigger placeholder
  },
  
  toggleMikrofon: () => set((state) => ({ mikrofon: !state.mikrofon })),
  
  setMaschineStatus: (m, status) => set((state) => ({
    maschinenStatus: { ...state.maschinenStatus, [m]: status }
  })),

  toggleMaschineStatus: (m) => set((state) => {
    const current = state.maschinenStatus[m];
    const next = current === 'ok' ? 'fehler' : current === 'fehler' ? 'offline' : 'ok';
    return { maschinenStatus: { ...state.maschinenStatus, [m]: next } };
  }),
  
  setDoubletteVersatz: (val) => set({ doubletteVersatz: val }),
  
  setApiSettings: (apiUrl, apiKey) => set({ apiUrl, apiKey }),
  
  syncPortal: async () => {
    set({ syncStatus: 'syncing' });
    return new Promise((resolve) => {
      setTimeout(() => {
        set({ 
          syncStatus: 'success', 
          lastSync: new Date().toLocaleTimeString('en-US', { hour12: false }) 
        });
        resolve();
      }, 1500);
    });
  }
}));

export const getAktiverSpieler = (state: GameState) => {
  const activePosten = (state.taubeIndex % 5) + 1;
  return state.spieler.find(s => {
    const p = ((s.posten - 1 + state.taubeIndex) % state.spieler.length) + 1;
    return p === activePosten;
  }) || state.spieler[0];
};
