import React, { useState, useEffect } from 'react';
import { useGameStore, Maschine, CustomSequenz } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import {
  ArrowLeft, Save, RefreshCw, CheckCircle2, AlertTriangle, GripVertical, Plus, X,
} from 'lucide-react';
import { cn } from '@/lib/utils';

const ALL_MASCHINEN: Maschine[] = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'];

const MAX_CUSTOM_MASCHINEN = 20;

function CustomSequenzEditor({
  modus,
  label,
}: {
  modus: 'CUSTOM_1' | 'CUSTOM_2';
  label: string;
}) {
  const store = useGameStore();
  const seq = store.customSequenzen[modus];
  const laeufe = store.customLaeufe[modus];
  const [draft, setDraft] = useState<Maschine[]>(seq);

  useEffect(() => { setDraft(store.customSequenzen[modus]); }, [modus]);

  const addMaschine = (m: Maschine) => {
    if (draft.length >= MAX_CUSTOM_MASCHINEN) return;
    setDraft([...draft, m]);
  };

  // Remove the entry at a specific index (click-to-remove)
  const removeAt = (idx: number) => setDraft(draft.filter((_, i) => i !== idx));

  const clearAll = () => setDraft([]);

  const save = () => store.setCustomSequenz(modus, draft);

  const isDirty = JSON.stringify(draft) !== JSON.stringify(seq);

  // Live stats — reflect chosen Läufe count
  const taubenPerLauf = draft.reduce((sum, m) => sum + (m === 'H' ? 2 : 1), 0);
  const maxPunktePerLauf = taubenPerLauf * 2;
  const maxPunkteSpill = maxPunktePerLauf * laeufe;

  return (
    <div className="bg-background border-2 border-border rounded-xl p-5 flex flex-col gap-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <h3 className="font-bold text-base uppercase tracking-wider">{label}</h3>
        <div className="flex gap-2">
          {isDirty && (
            <TouchButton variant="primary" className="h-8 px-3 gap-1 text-xs" onClick={save}>
              <Save className="w-3 h-3" /> Späicheren
            </TouchButton>
          )}
          <TouchButton variant="ghost" className="h-8 px-2 text-destructive hover:bg-destructive/10"
            onClick={clearAll} disabled={draft.length === 0} title="Alles läschen">
            <X className="w-4 h-4" />
          </TouchButton>
        </div>
      </div>

      {/* Current sequence — click a badge to remove it */}
      <div>
        <div className="text-[10px] font-bold uppercase tracking-widest text-muted-foreground mb-2">
          Sequenz <span className="text-muted-foreground/50 normal-case font-normal">(tippen op eng Schanz fir ze läschen)</span>
        </div>
        <div className="flex gap-2 flex-wrap min-h-[52px] bg-black/20 rounded-lg p-2">
          {draft.map((m, i) => (
            <button
              key={i}
              onClick={() => removeAt(i)}
              title="Läschen"
              className={cn(
                "w-10 h-10 rounded-lg border-2 flex items-center justify-center font-black text-lg",
                "transition-all active:scale-90 hover:opacity-60 hover:border-destructive/60 hover:bg-destructive/10",
                m === 'H'
                  ? "border-amber-500/60 bg-amber-500/15 text-amber-400"
                  : "border-primary/60 bg-primary/15 text-primary",
              )}
            >
              {m}
            </button>
          ))}
          {draft.length === 0 && (
            <span className="text-muted-foreground/50 text-sm italic self-center px-2">
              Keng Schanzen — dréckt hei drënner fir hinzezefügen
            </span>
          )}
        </div>
      </div>

      {/* Machine picker — tap to add */}
      <div>
        <div className="text-[10px] font-bold uppercase tracking-widest text-muted-foreground mb-2">
          Schanz dobäisetzen
        </div>
        <div className="grid grid-cols-8 gap-1">
          {ALL_MASCHINEN.map(m => (
            <button
              key={m}
              onClick={() => addMaschine(m)}
              disabled={draft.length >= MAX_CUSTOM_MASCHINEN}
              className={cn(
                "h-11 rounded-lg border-2 font-black text-base transition-all active:scale-95",
                m === 'H'
                  ? "border-amber-500/50 bg-amber-500/10 text-amber-400 hover:bg-amber-500/30"
                  : "border-primary/40 bg-primary/10 text-primary hover:bg-primary/30",
                draft.length >= MAX_CUSTOM_MASCHINEN && "opacity-30 cursor-not-allowed",
              )}
            >
              {m}
            </button>
          ))}
        </div>
      </div>

      {/* Läufe toggle */}
      <div>
        <div className="text-[10px] font-bold uppercase tracking-widest text-muted-foreground mb-2">
          Unzuel vun de Läuf
        </div>
        <div className="flex gap-2">
          {([1, 2] as const).map((n) => (
            <button
              key={n}
              onClick={() => store.setCustomLaeufe(modus, n)}
              className={cn(
                "flex-1 h-11 rounded-lg border-2 font-bold text-base transition-all",
                laeufe === n
                  ? "border-primary bg-primary/20 text-primary"
                  : "border-border bg-background text-muted-foreground hover:border-primary/50 hover:text-foreground",
              )}
            >
              {n} {n === 1 ? 'Lauf' : 'Läuf'}
            </button>
          ))}
        </div>
      </div>

      {/* Live stats */}
      {draft.length > 0 ? (
        <div className="grid grid-cols-3 gap-2 mt-1">
          {[
            { label: 'Tauben / Lauf', value: taubenPerLauf },
            { label: 'Max Pkt / Lauf', value: maxPunktePerLauf },
            { label: 'Max Pkt / Spill', value: maxPunkteSpill },
          ].map(({ label: l, value: v }) => (
            <div key={l} className="bg-primary/5 border border-primary/20 rounded-lg p-2 text-center">
              <div className="text-xl font-black text-primary">{v}</div>
              <div className="text-[9px] font-bold uppercase tracking-wider text-muted-foreground mt-0.5">{l}</div>
            </div>
          ))}
        </div>
      ) : (
        <div className="text-xs text-muted-foreground/60 italic text-center py-1">
          Fügt Schanzen dobäi fir d'Statistik ze gesinn
        </div>
      )}

      <div className="text-xs text-muted-foreground">
        {draft.length} / {MAX_CUSTOM_MASCHINEN} Schanzen · H = Doublette (2 Tauben, 4 Pkt max) ·
        <span className="text-primary/70 font-medium"> Schanzen-Aktiv-Filter gëtt ignoréiert</span>
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
    await store.syncAllPending();
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
