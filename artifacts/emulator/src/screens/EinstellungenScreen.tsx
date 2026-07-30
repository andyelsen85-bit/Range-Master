import React, { useState, useEffect } from 'react';
import { useGameStore, Maschine, CustomSequenz } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import {
  ArrowLeft, Save, RefreshCw, CheckCircle2, AlertTriangle, GripVertical, Plus, X,
} from 'lucide-react';
import { cn } from '@/lib/utils';

const ALL_MASCHINEN: Maschine[] = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'];

function CustomSequenzEditor({
  modus,
  label,
}: {
  modus: 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3';
  label: string;
}) {
  const store = useGameStore();
  const seq = store.customSequenzen[modus];
  const [draft, setDraft] = useState<Maschine[]>(seq);

  useEffect(() => { setDraft(store.customSequenzen[modus]); }, [modus]);

  const addMaschine = (m: Maschine) => {
    if (draft.length >= 9) return;
    setDraft([...draft, m]);
  };

  const removeLast = () => setDraft(draft.slice(0, -1));

  const save = () => {
    store.setCustomSequenz(modus, draft);
  };

  const isDirty = JSON.stringify(draft) !== JSON.stringify(seq);

  return (
    <div className="bg-background border-2 border-border rounded-xl p-5 flex flex-col gap-4">
      <div className="flex items-center justify-between">
        <h3 className="font-bold text-base uppercase tracking-wider">{label}</h3>
        <div className="flex gap-2">
          {isDirty && (
            <TouchButton size="sm" variant="primary" className="h-8 px-3 gap-1 text-xs" onClick={save}>
              <Save className="w-3 h-3" /> Späicheren
            </TouchButton>
          )}
          <TouchButton size="sm" variant="ghost" className="h-8 px-2" onClick={removeLast} disabled={draft.length === 0}>
            <X className="w-4 h-4" />
          </TouchButton>
        </div>
      </div>

      {/* Current sequence */}
      <div className="flex gap-2 flex-wrap min-h-[48px] bg-black/20 rounded-lg p-2">
        {draft.map((m, i) => (
          <div
            key={i}
            className={cn(
              "w-10 h-10 rounded-lg border-2 flex items-center justify-center font-black text-lg",
              m === 'H'
                ? "border-amber-500/60 bg-amber-500/15 text-amber-400"
                : "border-primary/60 bg-primary/15 text-primary",
            )}
          >
            {m}
          </div>
        ))}
        {draft.length === 0 && (
          <span className="text-muted-foreground/50 text-sm italic self-center px-2">
            Keng Schanzen — dréckt hei drënner
          </span>
        )}
      </div>

      {/* Machine picker */}
      <div className="grid grid-cols-8 gap-1">
        {ALL_MASCHINEN.map(m => (
          <button
            key={m}
            onClick={() => addMaschine(m)}
            disabled={draft.length >= 9}
            className={cn(
              "h-10 rounded-lg border-2 font-black text-base transition-all active:scale-95",
              m === 'H'
                ? "border-amber-500/50 bg-amber-500/10 text-amber-400 hover:bg-amber-500/30"
                : "border-primary/40 bg-primary/10 text-primary hover:bg-primary/30",
              draft.length >= 9 && "opacity-30 cursor-not-allowed",
            )}
          >
            {m}
          </button>
        ))}
      </div>

      <div className="text-xs text-muted-foreground">
        {draft.length} / 9 Schanzen · H = Doublette (2 Tauben)
      </div>
    </div>
  );
}

export function EinstellungenScreen() {
  const store = useGameStore();
  const [url, setUrl] = useState(store.apiUrl);
  const [key, setKey] = useState(store.apiKey);
  const [activeTab, setActiveTab] = useState<'api' | 'custom' | 'system'>('api');

  const save = () => {
    store.setApiSettings(url, key);
  };

  const testSync = async () => {
    save();
    await store.syncPortal();
  };

  return (
    <div className="flex h-full w-full bg-background flex-col">
      <header className="h-16 border-b-2 border-border flex items-center px-6 bg-card gap-4 shrink-0">
        <TouchButton onClick={() => store.setScreen('dashboard')} className="w-12 h-12 p-0">
          <ArrowLeft className="w-6 h-6" />
        </TouchButton>
        <h1 className="text-xl font-bold tracking-wider text-primary">ASTELLUNGEN</h1>
      </header>

      {/* Tabs */}
      <div className="flex border-b-2 border-border bg-card/50 shrink-0">
        {([
          { key: 'api',    label: 'Portal API' },
          { key: 'custom', label: 'Custom Modi' },
          { key: 'system', label: 'System' },
        ] as const).map(t => (
          <button
            key={t.key}
            onClick={() => setActiveTab(t.key)}
            className={cn(
              "px-8 py-4 font-bold text-base uppercase tracking-wider transition-colors border-b-4",
              activeTab === t.key
                ? "border-primary text-primary bg-primary/5"
                : "border-transparent text-muted-foreground hover:text-foreground",
            )}
          >
            {t.label}
          </button>
        ))}
      </div>

      <div className="flex-1 overflow-y-auto p-6">

        {/* ── API Tab ── */}
        {activeTab === 'api' && (
          <div className="max-w-2xl flex flex-col gap-5">
            <div className="bg-card border-2 border-border rounded-xl p-6 flex flex-col gap-5">
              <h2 className="text-base font-bold uppercase tracking-widest">Portal Verbindung</h2>

              <div>
                <label className="block text-sm font-bold text-muted-foreground mb-2 uppercase tracking-wider">
                  API Endpoint URL
                </label>
                <input
                  type="text"
                  value={url}
                  onChange={(e) => setUrl(e.target.value)}
                  placeholder="https://api.trapmaster.lu  oder  leer lassen"
                  className="w-full bg-background border-2 border-border rounded-lg h-14 px-4 text-base font-mono focus:border-primary focus:outline-none"
                />
              </div>

              <div>
                <label className="block text-sm font-bold text-muted-foreground mb-2 uppercase tracking-wider">
                  API Key
                </label>
                <input
                  type="password"
                  value={key}
                  onChange={(e) => setKey(e.target.value)}
                  placeholder="tm_..."
                  className="w-full bg-background border-2 border-border rounded-lg h-14 px-4 text-base font-mono focus:border-primary focus:outline-none"
                />
              </div>

              <div className="flex gap-3 pt-2">
                <TouchButton size="lg" variant="primary" className="flex-1 gap-2" onClick={save}>
                  <Save className="w-5 h-5" /> Späicheren
                </TouchButton>
                <TouchButton size="lg" variant="outline" className="flex-1 gap-2" onClick={testSync}
                  disabled={store.syncStatus === 'syncing'}>
                  <RefreshCw className={cn("w-5 h-5", store.syncStatus === 'syncing' && "animate-spin")} />
                  Verbindung testen
                </TouchButton>
              </div>

              {store.syncStatus !== 'idle' && (
                <div className={cn(
                  "p-4 rounded-lg flex items-center gap-3 font-bold",
                  store.syncStatus === 'success' && "bg-green-500/15 text-green-400 border border-green-500/40",
                  store.syncStatus === 'error'   && "bg-red-500/15 text-red-400 border border-red-500/40",
                  store.syncStatus === 'syncing' && "bg-yellow-500/15 text-yellow-400 border border-yellow-500/40",
                )}>
                  {store.syncStatus === 'success' && <><CheckCircle2 className="w-5 h-5" /> Verbindung erfollegräich</>}
                  {store.syncStatus === 'error'   && <><AlertTriangle className="w-5 h-5" /> Feeler — URL an Key iwwerpréiwen</>}
                  {store.syncStatus === 'syncing' && <><RefreshCw className="w-5 h-5 animate-spin" /> Verbënnt...</>}
                </div>
              )}
            </div>

            {/* Last sync info */}
            {store.lastSync && (
              <div className="text-sm text-muted-foreground font-mono text-center">
                Leschte Sync: {store.lastSync}
              </div>
            )}
          </div>
        )}

        {/* ── Custom Modi Tab ── */}
        {activeTab === 'custom' && (
          <div className="max-w-2xl flex flex-col gap-5">
            <p className="text-sm text-muted-foreground">
              Definéiert d'Reihenfolg vun de Schanzen fir Custom Modi.
              H = Doublette (gëtt als 2 Tauben gezielt).
            </p>
            <CustomSequenzEditor modus="CUSTOM_1" label="Custom 1" />
            <CustomSequenzEditor modus="CUSTOM_2" label="Custom 2" />
          </div>
        )}

        {/* ── System Tab ── */}
        {activeTab === 'system' && (
          <div className="max-w-2xl flex flex-col gap-5">
            <div className="bg-card border-2 border-border rounded-xl p-6">
              <h2 className="text-base font-bold uppercase tracking-widest mb-4">System Info</h2>
              <div className="grid grid-cols-2 gap-3 font-mono text-base">
                {[
                  { label: 'Software Version', value: 'v1.5.0' },
                  { label: 'Hardware', value: 'ESP32-P4 (Emulator)' },
                  { label: 'Display', value: '1280×800 Touch' },
                  { label: 'Protokoll', value: 'LoRa 433 MHz (Phase 2)' },
                  { label: 'Mikrofon', value: 'Faze 3 (net aktiv)' },
                  { label: 'Build', value: new Date().toLocaleDateString('de-LU') },
                ].map(({ label, value }) => (
                  <div key={label} className="p-3 bg-background border-2 border-border rounded-lg">
                    <span className="text-muted-foreground block text-xs uppercase font-sans font-bold mb-1">{label}</span>
                    <span className="text-foreground">{value}</span>
                  </div>
                ))}
              </div>
            </div>

            <div className="bg-card border-2 border-border rounded-xl p-6">
              <h2 className="text-base font-bold uppercase tracking-widest mb-4">Modus Erklärung</h2>
              <div className="flex flex-col gap-2 text-sm text-muted-foreground">
                <div><span className="text-foreground font-bold">Normal:</span> A → B → C → D → E → F → G → H (Doublette)</div>
                <div><span className="text-foreground font-bold">Harakiri:</span> A–G zufälleg, H ëmmer um Enn</div>
                <div><span className="text-foreground font-bold">Harakiri Verspéit:</span> Wéi Harakiri, Sequenz mat méi Versatz</div>
                <div><span className="text-foreground font-bold">Harakiri Full:</span> All Schanzen inkl. H zufälleg</div>
                <div><span className="text-foreground font-bold">Custom 1/2:</span> Fräi konfiguréierbar (Tab: Custom Modi)</div>
              </div>
            </div>
          </div>
        )}

      </div>
    </div>
  );
}
