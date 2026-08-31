import React, { useState, useEffect } from 'react';
import { Wifi, WifiOff, Lock, Eye, EyeOff, RefreshCw, CheckCircle2, AlertTriangle, Trash2, Star } from 'lucide-react';
import { TouchButton } from '@/components/TouchButton';
import { cn } from '@/lib/utils';

interface Network {
  ssid: string;
  signal: number; // 1–4
  secured: boolean;
}

interface SavedNetwork extends Network {
  password: string;
}

const WIFI_STORAGE_KEY = 'trapmaster-known-wifi-networks';
const WIFI_PREFERRED_KEY = 'trapmaster-preferred-wifi-network';
const MAX_SAVED_NETWORKS = 5;

function loadSavedNetworks(): SavedNetwork[] {
  try {
    const raw = localStorage.getItem(WIFI_STORAGE_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as SavedNetwork[];
    return Array.isArray(parsed)
      ? parsed.filter(n => n && typeof n.ssid === 'string' && n.ssid.length > 0).slice(0, MAX_SAVED_NETWORKS)
      : [];
  } catch {
    return [];
  }
}

function persistSavedNetworks(networks: SavedNetwork[], preferredSsid: string | null) {
  try {
    localStorage.setItem(WIFI_STORAGE_KEY, JSON.stringify(networks));
    if (preferredSsid) localStorage.setItem(WIFI_PREFERRED_KEY, preferredSsid);
    else localStorage.removeItem(WIFI_PREFERRED_KEY);
  } catch {}
}

const MOCK_NETWORKS: Network[] = [
  { ssid: 'RangeMaster-HQ',     signal: 4, secured: true  },
  { ssid: 'WLAN-Büro',          signal: 3, secured: true  },
  { ssid: 'TrapshotGuest',      signal: 3, secured: false },
  { ssid: 'iPhone von Operator', signal: 2, secured: true  },
  { ssid: 'TelenetWifi-9A1B',   signal: 1, secured: true  },
];

function SignalBars({ level, className }: { level: number; className?: string }) {
  return (
    <div className={cn('flex items-end gap-[2px]', className)}>
      {[1, 2, 3, 4].map(i => (
        <div
          key={i}
          style={{ height: `${i * 4 + 4}px`, width: '4px' }}
          className={cn(
            'rounded-[1px]',
            i <= level ? 'bg-primary' : 'bg-border/60',
          )}
        />
      ))}
    </div>
  );
}

export function WifiScreen() {
  const [scanning, setScanning] = useState(false);
  const [networks, setNetworks] = useState<Network[]>([]);
  const [selected, setSelected] = useState<Network | null>(null);
  const [password, setPassword] = useState('');
  const [showPw, setShowPw] = useState(false);
  const [connectState, setConnectState] = useState<'idle' | 'connecting' | 'connected' | 'error' | 'full'>('idle');
  const [connectedSsid, setConnectedSsid] = useState<string | null>(null);
  const [savedNetworks, setSavedNetworks] = useState<SavedNetwork[]>(loadSavedNetworks);
  const [preferredSsid, setPreferredSsid] = useState<string | null>(() => {
    try { return localStorage.getItem(WIFI_PREFERRED_KEY); } catch { return null; }
  });

  const scan = () => {
    setScanning(true);
    setNetworks([]);
    setSelected(null);
    setConnectState('idle');
    setTimeout(() => {
      setNetworks(MOCK_NETWORKS);
      setScanning(false);
    }, 1800);
  };

  useEffect(() => { scan(); }, []);

  const connect = () => {
    if (!selected) return;
    if (savedNetworks.length >= MAX_SAVED_NETWORKS &&
        !savedNetworks.some(network => network.ssid === selected.ssid)) {
      setConnectState('full');
      return;
    }
    setConnectState('connecting');
    setTimeout(() => {
      // Simulate: wrong password if shorter than 3 chars and network is secured
      if (selected.secured && password.length > 0 && password.length < 3) {
        setConnectState('error');
      } else {
        setConnectState('connected');
        setConnectedSsid(selected.ssid);
        const saved: SavedNetwork = { ...selected, password };
        setSavedNetworks(previous => {
          const existing = previous.findIndex(network => network.ssid === selected.ssid);
          const next = existing >= 0
            ? previous.map((network, index) => index === existing ? saved : network)
            : previous.length < MAX_SAVED_NETWORKS ? [...previous, saved] : previous;
          setPreferredSsid(selected.ssid);
          persistSavedNetworks(next, selected.ssid);
          return next;
        });
      }
    }, 2200);
  };

  const selectNetwork = (network: Network) => {
    const saved = savedNetworks.find(item => item.ssid === network.ssid);
    setSelected(network);
    setPassword(saved?.password ?? '');
    setConnectState('idle');
  };

  const removeSavedNetwork = (ssid: string) => {
    const next = savedNetworks.filter(network => network.ssid !== ssid);
    const nextPreferred = preferredSsid === ssid ? next[0]?.ssid ?? null : preferredSsid;
    setSavedNetworks(next);
    setPreferredSsid(nextPreferred);
    persistSavedNetworks(next, nextPreferred);
    if (selected?.ssid === ssid) {
      setSelected(null);
      setPassword('');
    }
  };

  const disconnect = () => {
    setConnectState('idle');
    setConnectedSsid(null);
    setSelected(null);
    setPassword('');
  };

  return (
    <div className="flex flex-col gap-5 max-w-2xl">

      {/* Simulation banner */}
      <div className="flex items-center gap-3 bg-amber-500/10 border-2 border-amber-500/40 rounded-xl px-5 py-3">
        <AlertTriangle className="w-5 h-5 text-amber-400 shrink-0" />
        <p className="text-sm font-bold text-amber-300">
          Dëse Bildschirm ass nëmmen um physesche Terminal disponibel
        </p>
      </div>

      {/* Connected status bar */}
      {connectedSsid && (
        <div className="flex items-center justify-between bg-green-500/10 border-2 border-green-500/40 rounded-xl px-5 py-3">
          <div className="flex items-center gap-3">
            <CheckCircle2 className="w-5 h-5 text-green-400" />
            <div>
              <span className="text-sm font-bold text-green-300">Verbonnen mat</span>
              <span className="text-sm font-mono text-green-200 ml-2">{connectedSsid}</span>
            </div>
          </div>
          <TouchButton variant="ghost" className="h-8 px-3 text-xs text-destructive hover:bg-destructive/10" onClick={disconnect}>
            Trennen
          </TouchButton>
        </div>
      )}

      {/* Saved networks */}
      <div className="bg-card border-2 border-border rounded-xl p-5 flex flex-col gap-4">
        <div className="flex items-center justify-between">
          <h2 className="text-base font-bold uppercase tracking-widest">Gespeicherte Netzwerke</h2>
          <span className="text-xs font-mono text-muted-foreground">{savedNetworks.length}/{MAX_SAVED_NETWORKS}</span>
        </div>
        {savedNetworks.length === 0 ? (
          <p className="text-sm text-muted-foreground">Noch kein Netzwerk gespeichert.</p>
        ) : (
          <div className="flex flex-col gap-2">
            {savedNetworks.map(network => (
              <div key={network.ssid} className="flex items-center gap-2">
                <button
                  onClick={() => selectNetwork(network)}
                  className={cn(
                    'flex min-w-0 flex-1 items-center justify-between px-4 py-3 rounded-lg border-2 text-left',
                    selected?.ssid === network.ssid ? 'border-primary bg-primary/10' : 'border-border bg-background',
                  )}
                >
                  <span className="flex min-w-0 items-center gap-3">
                    <Wifi className="w-5 h-5 shrink-0 text-primary" />
                    <span className="truncate font-bold text-sm">{network.ssid}</span>
                    {preferredSsid === network.ssid && (
                      <span className="flex items-center gap-1 text-xs text-amber-300">
                        <Star className="w-3 h-3 fill-current" /> Bevorzugt
                      </span>
                    )}
                  </span>
                  {network.secured ? <Lock className="w-4 h-4 text-muted-foreground" /> : <span className="text-xs text-muted-foreground">offen</span>}
                </button>
                <TouchButton
                  variant="ghost"
                  className="h-12 w-12 p-0 text-destructive hover:bg-destructive/10"
                  onClick={() => removeSavedNetwork(network.ssid)}
                  aria-label={`${network.ssid} löschen`}
                >
                  <Trash2 className="w-5 h-5" />
                </TouchButton>
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Available network list */}
      <div className="bg-card border-2 border-border rounded-xl p-5 flex flex-col gap-4">
        <div className="flex items-center justify-between">
          <h2 className="text-base font-bold uppercase tracking-widest">Netzwierker</h2>
          <TouchButton
            variant="ghost"
            className="h-9 px-3 gap-2 text-xs"
            onClick={scan}
            disabled={scanning}
          >
            <RefreshCw className={cn('w-4 h-4', scanning && 'animate-spin')} />
            {scanning ? 'Sichen...' : 'Nei sichen'}
          </TouchButton>
        </div>

        {scanning && (
          <div className="flex items-center justify-center py-8 gap-3 text-muted-foreground">
            <RefreshCw className="w-5 h-5 animate-spin" />
            <span className="text-sm">Sichen no WiFi Netzwierker...</span>
          </div>
        )}

        {!scanning && networks.length === 0 && (
          <div className="flex items-center justify-center py-8 gap-3 text-muted-foreground">
            <WifiOff className="w-5 h-5" />
            <span className="text-sm">Keng Netzwierker fonnt</span>
          </div>
        )}

        {!scanning && networks.length > 0 && (
          <div className="flex flex-col gap-2">
            {networks.map((n) => {
              const isSelected = selected?.ssid === n.ssid;
              const isConnected = connectedSsid === n.ssid;
              return (
                <button
                  key={n.ssid}
                  onClick={() => {
                    if (!isConnected) {
                      selectNetwork(n);
                    }
                  }}
                  className={cn(
                    'flex items-center justify-between px-4 py-3 rounded-lg border-2 transition-all text-left',
                    isConnected
                      ? 'border-green-500/50 bg-green-500/10'
                      : isSelected
                        ? 'border-primary bg-primary/10'
                        : 'border-border bg-background hover:border-primary/50',
                  )}
                >
                  <div className="flex items-center gap-3">
                    <Wifi className={cn('w-5 h-5', isConnected ? 'text-green-400' : isSelected ? 'text-primary' : 'text-muted-foreground')} />
                    <div>
                      <div className={cn('font-bold text-sm', isConnected ? 'text-green-300' : isSelected ? 'text-primary' : 'text-foreground')}>
                        {n.ssid}
                      </div>
                      <div className="text-xs text-muted-foreground flex items-center gap-1 mt-0.5">
                        {n.secured ? <Lock className="w-3 h-3" /> : <span className="text-xs">oppen</span>}
                        {n.secured && <span>geschützt</span>}
                        {isConnected && <span className="text-green-400 font-bold ml-1">● Verbonnen</span>}
                      </div>
                    </div>
                  </div>
                  <SignalBars level={n.signal} />
                </button>
              );
            })}
          </div>
        )}
      </div>

      {/* Password + connect form */}
      {selected && !connectedSsid && (
        <div className="bg-card border-2 border-border rounded-xl p-5 flex flex-col gap-4">
          <h2 className="text-base font-bold uppercase tracking-widest">
             Verbinden mit <span className="text-primary font-mono normal-case">{selected.ssid}</span>
          </h2>

          {selected.secured ? (
            <div>
              <label className="block text-sm font-bold text-muted-foreground mb-2 uppercase tracking-wider">
                 Passwort
              </label>
              <div className="relative">
                <input
                  type={showPw ? 'text' : 'password'}
                  value={password}
                  onChange={e => { setPassword(e.target.value); setConnectState('idle'); }}
                   placeholder="WiFi-Passwort eingeben..."
                  className="w-full bg-background border-2 border-border rounded-lg h-14 px-4 pr-12 text-base font-mono focus:border-primary focus:outline-none"
                />
                <button
                  type="button"
                  onClick={() => setShowPw(v => !v)}
                  className="absolute right-3 top-1/2 -translate-y-1/2 text-muted-foreground hover:text-foreground"
                >
                  {showPw ? <EyeOff className="w-5 h-5" /> : <Eye className="w-5 h-5" />}
                </button>
              </div>
            </div>
          ) : (
            <p className="text-sm text-muted-foreground">Dieses Netzwerk ist offen — kein Passwort erforderlich.</p>
          )}

          <TouchButton
            size="lg"
            variant="primary"
            className="gap-2"
            onClick={connect}
            disabled={connectState === 'connecting' || (selected.secured && password.length === 0)}
          >
            {connectState === 'connecting' ? (
              <><RefreshCw className="w-5 h-5 animate-spin" /> Verbindet...</>
            ) : (
              <><Wifi className="w-5 h-5" /> Verbinden und speichern</>
            )}
          </TouchButton>

          {connectState === 'connected' && (
            <div className="flex items-center gap-3 bg-green-500/15 border border-green-500/40 rounded-lg px-4 py-3 font-bold text-green-400">
               <CheckCircle2 className="w-5 h-5" /> Verbindung erfolgreich
            </div>
          )}
          {connectState === 'error' && (
            <div className="flex items-center gap-3 bg-red-500/15 border border-red-500/40 rounded-lg px-4 py-3 font-bold text-red-400">
               <AlertTriangle className="w-5 h-5" /> Falsches Passwort — erneut versuchen
            </div>
          )}
          {connectState === 'full' && (
            <div className="flex items-center gap-3 bg-amber-500/15 border border-amber-500/40 rounded-lg px-4 py-3 font-bold text-amber-300">
              <AlertTriangle className="w-5 h-5" /> Maximal fünf Netzwerke — zuerst ein gespeichertes Netzwerk löschen
            </div>
          )}
        </div>
      )}
    </div>
  );
}
