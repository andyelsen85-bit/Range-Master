import React, { useState, useEffect } from 'react';
import { useGameStore, Maschine, CustomSequenz, CustomSequenzEintrag, MASCHINEN_LIST } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import {
  ArrowLeft, Save, RefreshCw, CheckCircle2, AlertTriangle, GripVertical, Plus, X,
} from 'lucide-react';
import { cn } from '@/lib/utils';
import { WifiScreen } from '@/screens/WifiScreen';
import { BluetoothScreen } from '@/screens/BluetoothScreen';

const ALL_MASCHINEN: Maschine[] = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'];

const MAX_CUSTOM_MASCHINEN = 20;

function CustomSequenzEditor({
  modus,
  label,
}: {
  modus: 'CUSTOM_1' | 'CUSTOM_2' | 'CUSTOM_3' | 'CUSTOM_4';
  label: string;
}) {
  const store = useGameStore();
  const seq = store.customSequenzen[modus];
  const laeufe = store.customLaeufe[modus];
  const [draft, setDraft] = useState<CustomSequenz>(seq);
  const [pairFirst, setPairFirst] = useState<Maschine | null>(null);
  const [delaySeconds, setDelaySeconds] = useState('1.0');

  useEffect(() => { setDraft(store.customSequenzen[modus]); }, [modus]);

  const addEntry = (entry: CustomSequenzEintrag) => {
    if (draft.length >= MAX_CUSTOM_MASCHINEN) return;
    setDraft([...draft, entry]);
  };
  const addMaschine = (m: Maschine) => addEntry({ maschine: m });
  const addHDoublette = () => addEntry({ maschine: 'H', isDoublette: true });

  const selectPairMachine = (m: Maschine) => {
    if (pairFirst === null) {
      setPairFirst(m);
      return;
    }
    if (pairFirst === m) return;
    const parsed = Number(delaySeconds);
    const seconds = Number.isFinite(parsed) ? Math.min(10, Math.max(0, parsed)) : 1;
    addEntry({ maschine: pairFirst, partner: m, isDoublette: true, delaySeconds: seconds });
    setPairFirst(null);
  };

  // Remove the entry at a specific index (click-to-remove)
  const removeAt = (idx: number) => setDraft(draft.filter((_, i) => i !== idx));

  const clearAll = () => setDraft([]);

  const save = () => store.setCustomSequenz(modus, draft);

  const isDirty = JSON.stringify(draft) !== JSON.stringify(seq);

  // Live stats — reflect chosen Läufe count
  const taubenPerLauf = draft.reduce(
    (sum, entry) => sum + (entry.maschine === 'H' || entry.isDoublette ? 2 : 1), 0,
  );
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
          {draft.map((entry, i) => (
            <button
              key={i}
              onClick={() => removeAt(i)}
              title="Läschen"
              className={cn(
                "h-10 rounded-lg border-2 flex items-center justify-center font-black text-lg whitespace-pre-line",
                entry.isDoublette ? "min-w-16 px-2 text-xs leading-tight" : "w-10",
                "transition-all active:scale-90 hover:opacity-60 hover:border-destructive/60 hover:bg-destructive/10",
                entry.maschine === 'H'
                  ? "border-amber-500/60 bg-amber-500/15 text-amber-400"
                  : "border-primary/60 bg-primary/15 text-primary",
              )}
            >
              {entry.maschine === 'H'
                ? 'H1/H2'
                : entry.isDoublette
                  ? `${entry.maschine}+${entry.partner}\n${(entry.delaySeconds ?? 1).toFixed(1)}s`
                  : entry.maschine}
            </button>
          ))}
          {draft.length === 0 && (
            <span className="text-muted-foreground/50 text-sm italic self-center px-2">
              Keng Schanzen — dréckt hei drënner fir dobäisetzen
            </span>
          )}
        </div>
      </div>

      {/* Machine picker — tap to add */}
      <div>
        <div className="text-[10px] font-bold uppercase tracking-widest text-muted-foreground mb-2">
          Eenzel Schanz dobäisetzen
        </div>
        <div className="grid grid-cols-7 gap-1">
          {ALL_MASCHINEN.filter(m => m !== 'H').map(m => (
            <button
              key={m}
              onClick={() => addMaschine(m)}
              disabled={draft.length >= MAX_CUSTOM_MASCHINEN}
              className={cn(
                "h-11 rounded-lg border-2 font-black text-base transition-all active:scale-95",
                "border-primary/40 bg-primary/10 text-primary hover:bg-primary/30",
                draft.length >= MAX_CUSTOM_MASCHINEN && "opacity-30 cursor-not-allowed",
              )}
            >
              {m}
            </button>
          ))}
        </div>
      </div>

      {/* Custom doublettes */}
      <div className="flex flex-col gap-2">
        <div className="text-[10px] font-bold uppercase tracking-widest text-muted-foreground">
          Doublette: A–G + Partner
        </div>
        <div className="flex items-center gap-2">
          <label className="text-xs font-bold text-muted-foreground uppercase">Delay (Sek.)</label>
          <input
            type="number"
            min="0"
            max="10"
            step="0.1"
            value={delaySeconds}
            onChange={(e) => setDelaySeconds(e.target.value)}
            className="w-24 h-10 rounded-lg border-2 border-border bg-background px-3 font-mono font-bold focus:border-primary focus:outline-none"
          />
          <span className="text-xs text-muted-foreground">0–10</span>
        </div>
        <div className="grid grid-cols-7 gap-1">
          {ALL_MASCHINEN.filter(m => m !== 'H').map(m => (
            <button
              key={m}
              onClick={() => selectPairMachine(m)}
              disabled={draft.length >= MAX_CUSTOM_MASCHINEN}
              className={cn(
                "h-11 rounded-lg border-2 font-black text-base transition-all active:scale-95",
                pairFirst === m
                  ? "border-primary bg-primary text-black"
                  : "border-primary/40 bg-primary/10 text-primary hover:bg-primary/30",
                draft.length >= MAX_CUSTOM_MASCHINEN && "opacity-30 cursor-not-allowed",
              )}
            >
              {m}
            </button>
          ))}
        </div>
        <div className="flex items-center gap-3">
          <button
            onClick={addHDoublette}
            disabled={draft.length >= MAX_CUSTOM_MASCHINEN}
            className={cn(
              "h-11 px-4 rounded-lg border-2 border-amber-500/60 bg-amber-500/15 text-amber-400 font-bold hover:bg-amber-500/30 active:scale-95",
              draft.length >= MAX_CUSTOM_MASCHINEN && "opacity-30 cursor-not-allowed",
            )}
          >
            H · H1/H2
          </button>
          <span className="text-xs text-muted-foreground">
            {pairFirst ? `${pairFirst} gewielt — Partner tippen` : 'H: 1 FIRE — d’Maschinn mécht H2'}
          </span>
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
        {draft.length} / {MAX_CUSTOM_MASCHINEN} Launch-Elementer · H = 1 FIRE, H1/H2 ·
        <span className="text-primary/70 font-medium"> Schanzen-Aktiv-Filter gëtt ignoréiert</span>
      </div>
    </div>
  );
}

export function EinstellungenScreen() {
  const store = useGameStore();
  const [url, setUrl] = useState(store.apiUrl);
  const [key, setKey] = useState(store.apiKey);
  const [activeTab, setActiveTab] = useState<'api' | 'produkte' | 'schanzen' | 'custom' | 'wifi' | 'bluetooth' | 'system' | 'kiosk' | 'daySummary'>('api');

  const [oldPin, setOldPin] = useState('');
  const [newPin, setNewPin] = useState('');
  const [pinMessage, setPinMessage] = useState('');
  const [pinError, setPinError] = useState('');

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
          { key: 'api',       label: 'Portal API' },
          { key: 'daySummary',label: 'Dag Bilan' },
           { key: 'produkte',  label: 'Präisser' },
          { key: 'schanzen',  label: 'Schanzen' },
          { key: 'custom',    label: 'Custom Modi' },
          { key: 'wifi',      label: 'WiFi' },
          { key: 'bluetooth', label: 'Bluetooth' },
          { key: 'system',    label: 'System' },
          { key: 'kiosk',     label: 'Bar / Kiosk' },
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
                  placeholder="https://rangemaster.hostzone.lu  (keng /api um Enn)"
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
{activeTab === 'daySummary' && (
  <div className="max-w-3xl flex flex-col gap-5">
    <div className="flex items-center justify-between">
      <h2 className="text-base font-bold uppercase tracking-widest text-primary">Dag Bilan — {store.verkaufDatum}</h2>
      <TouchButton className="h-10 px-4 gap-2" variant="outline" onClick={() => store.ladeDaySummary()} disabled={store.daySummaryLaden}>
        <RefreshCw className={cn("w-4 h-4", store.daySummaryLaden && "animate-spin")} />
        Aktualiséieren
      </TouchButton>
    </div>

    {!store.daySummary ? (
      <div className="p-8 border-2 border-border border-dashed rounded-xl text-center text-muted-foreground">
        Keen Dag Bilan verfügbar. Dréckt Aktualiséieren.
      </div>
    ) : (
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {/* Main stats */}
        <div className="bg-card border-2 border-border rounded-xl p-5 flex flex-col gap-4">
          <h3 className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Allgemeng</h3>
          <div className="grid grid-cols-2 gap-3">
            <div className="bg-background border border-border rounded-lg p-3 text-center">
              <div className="text-[10px] font-bold text-muted-foreground uppercase mb-1">Spiller</div>
              <div className="text-2xl font-black text-foreground">{store.daySummary.uniquePlayers}</div>
            </div>
            <div className="bg-background border border-border rounded-lg p-3 text-center">
              <div className="text-[10px] font-bold text-muted-foreground uppercase mb-1">Bezuelt</div>
              <div className="text-2xl font-black text-green-500">{store.daySummary.paidPlayers}</div>
            </div>
            <div className="bg-background border border-border rounded-lg p-3 text-center">
              <div className="text-[10px] font-bold text-muted-foreground uppercase mb-1">Gespillt</div>
              <div className="text-2xl font-black">{store.daySummary.games}</div>
              <div className="text-[10px] text-muted-foreground mt-0.5">{store.daySummary.completedGames} ofgeschloss</div>
            </div>
            <div className="bg-background border border-border rounded-lg p-3 text-center">
              <div className="text-[10px] font-bold text-muted-foreground uppercase mb-1">Tauben (Konfirméiert)</div>
              <div className="text-2xl font-black text-primary" data-testid="confirmed-clays">{store.daySummary.confirmedClays}</div>
            </div>
          </div>
        </div>

        {/* Categories */}
        <div className="bg-card border-2 border-border rounded-xl p-5 flex flex-col gap-4">
          <h3 className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Umsaz (Net-Bezuelt Inklusiv)</h3>
          <div className="flex flex-col gap-2 font-mono text-sm">
            {Object.entries(store.daySummary.categorySubtotals).map(([cat, cents]) => (
              <div key={cat} className="flex justify-between p-2 bg-background border border-border rounded-lg">
                <span className="font-bold text-muted-foreground uppercase tracking-wider text-[10px] self-center">{cat}</span>
                <span className="font-black">{(cents / 100).toFixed(2)} €</span>
              </div>
            ))}
            <div className="flex justify-between p-2 bg-primary/10 border-2 border-primary/30 rounded-lg mt-2">
              <span className="font-bold text-primary uppercase tracking-wider text-xs self-center">TOTAL</span>
              <span className="font-black text-primary text-lg">{(store.daySummary.generalTotalCents / 100).toFixed(2)} €</span>
            </div>
          </div>
        </div>
      </div>
    )}
  </div>
)}

        {activeTab === 'produkte' && (
          <div className="max-w-2xl flex flex-col gap-5">
            <div className="bg-card border-2 border-border rounded-xl p-6">
              <h2 className="text-base font-bold uppercase tracking-widest mb-2">Terminal-Produkter</h2>
              <p className="text-sm text-muted-foreground mb-5">Aktuell Präisser aus dem Portal, als exakt Cent gelueden. Präisverwaltung ass nëmmen am Portal fir Administrateuren disponibel.</p>
              <div className="flex flex-col gap-3">
                {store.produkte.map(produkt => (
                  <div key={produkt.id} className="flex gap-3 items-center border border-border rounded-lg p-4">
                    <div className="flex-1"><b>{produkt.name}</b><div className="text-xs text-muted-foreground">{produkt.category}</div></div>
                    <div data-testid={`text-price-${produkt.id}`} className="font-mono font-bold">{produkt.currentPrice ? `${(produkt.currentPrice.unitPriceCents / 100).toFixed(2)} €` : 'Kee Präis'}</div>
                  </div>
                ))}
              </div>
            </div>
          </div>
        )}

        {/* ── Schanzen Tab ── */}
        {activeTab === 'schanzen' && (
          <div className="max-w-2xl flex flex-col gap-5">
            <div className="bg-card border-2 border-border rounded-xl p-6">
              <h2 className="text-base font-bold uppercase tracking-widest mb-1">Schanzen</h2>
              <p className="text-sm text-muted-foreground mb-6">Tippen zum Aktivieren / Deaktivieren</p>
              <div className="grid grid-cols-4 gap-4">
                {MASCHINEN_LIST.map((m) => {
                  const aktiv = store.maschinenAktiv[m];
                  const isDoublette = m === 'H';
                  return (
                    <button
                      key={m}
                      onClick={() => store.toggleMaschineAktiv(m)}
                      className={cn(
                        "flex flex-col items-center justify-center h-28 rounded-xl border-4 transition-all active:scale-95",
                        aktiv
                          ? isDoublette
                            ? 'border-amber-500/60 bg-amber-500/10 text-amber-400'
                            : 'border-primary/60 bg-primary/10 text-primary'
                          : 'border-border/40 bg-background/50 text-muted-foreground/40',
                      )}
                    >
                      <span className="text-4xl font-bold mb-1">{m}</span>
                      <span className="text-sm font-bold uppercase tracking-wider">
                        {isDoublette ? 'Doublette' : 'Single'}
                      </span>
                      <span className={cn(
                        "text-xs mt-0.5 font-bold uppercase tracking-widest",
                        aktiv ? "text-green-400" : "text-muted-foreground/40"
                      )}>
                        {aktiv ? '● Aktiv' : '○ Aus'}
                      </span>
                    </button>
                  );
                })}
              </div>
            </div>
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
            <CustomSequenzEditor modus="CUSTOM_3" label="Custom 3" />
            <CustomSequenzEditor modus="CUSTOM_4" label="Custom 4" />
          </div>
        )}

        {/* ── WiFi Tab ── */}
        {activeTab === 'wifi' && <WifiScreen />}

        {/* ── Bluetooth Tab ── */}
        {activeTab === 'bluetooth' && <BluetoothScreen />}

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
                <div><span className="text-foreground font-bold">Custom 1–4:</span> Fräi konfiguréierbar (Tab: Custom Modi)</div>
              </div>
            </div>
          </div>
        )}

        {/* ── Kiosk Tab ── */}
        {activeTab === 'kiosk' && (
          <div className="max-w-2xl flex flex-col gap-5" data-testid="settings-kiosk-tab">
            <div className="bg-card border-2 border-border rounded-xl p-6">
              <h2 className="text-base font-bold uppercase tracking-widest mb-2">Catering Kiosk Modus</h2>
              <p className="text-sm text-muted-foreground mb-6">
                Aktivéiert e séchere Modus fir d'Bar, deen den Zougang zu de Spiller an Astellunge blockéiert.
                De Kiosk Modus kann nëmme mat engem PIN verlooss ginn.
              </p>

              <div className="bg-background border-2 border-border rounded-lg p-5 mb-6">
                <label className="block text-sm font-bold text-muted-foreground mb-2 uppercase tracking-wider">PIN Astellen (4-Zifferen)</label>
                <div className="flex flex-col gap-3">
                  {store.kioskPinHash && (
                    <div className="flex items-center gap-3">
                      <input
                        type="password"
                        maxLength={4}
                        value={oldPin}
                        onChange={(e) => setOldPin(e.target.value.replace(/\D/g, ''))}
                        placeholder="Aktuellen PIN"
                        className="w-48 bg-card border-2 border-border rounded-lg h-12 px-4 text-center tracking-[1em] font-mono text-xl focus:border-primary focus:outline-none"
                        data-testid="input-kiosk-old-pin"
                      />
                      <span className="text-xs text-muted-foreground font-bold">Néideg fir z'änneren</span>
                    </div>
                  )}
                  <div className="flex items-center gap-3">
                    <input
                      type="password"
                      maxLength={4}
                      value={newPin}
                      onChange={(e) => setNewPin(e.target.value.replace(/\D/g, ''))}
                      placeholder={store.kioskPinHash ? 'Neie PIN' : '0000'}
                      className="w-48 bg-card border-2 border-border rounded-lg h-12 px-4 text-center tracking-[1em] font-mono text-xl focus:border-primary focus:outline-none"
                      data-testid="input-kiosk-new-pin"
                    />
                    <TouchButton
                      variant="outline"
                      className="h-12"
                      disabled={(newPin.length !== 4 && newPin !== '') || (!!store.kioskPinHash && oldPin.length !== 4)}
                      onClick={async () => {
                        setPinMessage('');
                        setPinError('');

                        const result = await store.setKioskPin(store.kioskPinHash ? oldPin : null, newPin || null);
                        if (result.success) {
                          setPinMessage(newPin ? 'Neie PIN gespäichert.' : 'PIN geläscht.');
                          setNewPin('');
                          setOldPin('');
                        } else {
                          setPinError(result.error || 'Feeler beim PIN späicheren');
                        }

                        setTimeout(() => {
                          setPinMessage('');
                          setPinError('');
                        }, 3000);
                      }}
                      data-testid="button-save-kiosk-pin"
                    >
                      <Save className="w-4 h-4 mr-2" />
                      Späicheren
                    </TouchButton>
                  </div>
                </div>
                {pinMessage && <div className="text-green-400 font-bold text-sm mt-2">{pinMessage}</div>}
                {pinError && <div className="text-destructive font-bold text-sm mt-2">{pinError}</div>}
                <div className="text-xs text-muted-foreground mt-2">
                  {store.kioskPinHash ? 'E PIN ass aktuell agestallt.' : 'Et ass kee PIN agestallt.'}
                </div>
              </div>

              <TouchButton
                variant="primary"
                size="xl"
                className="w-full h-16 text-xl font-bold"
                disabled={!store.kioskPinHash}
                onClick={() => store.setKioskMode('CATERING')}
                data-testid="button-enter-kiosk-mode"
              >
                CATERING MODUS STARTEN
              </TouchButton>
              {!store.kioskPinHash && (
                <div className="text-destructive text-sm font-bold text-center mt-2">
                  Setzt e PIN an, fir de Catering Modus z'aktivéieren.
                </div>
              )}
            </div>
          </div>
        )}

      </div>
    </div>
  );
}
