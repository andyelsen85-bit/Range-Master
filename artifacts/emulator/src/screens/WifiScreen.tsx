import React, { useState, useEffect } from 'react';
import { Wifi, WifiOff, Lock, Eye, EyeOff, RefreshCw, CheckCircle2, AlertTriangle, Signal } from 'lucide-react';
import { TouchButton } from '@/components/TouchButton';
import { cn } from '@/lib/utils';

interface Network {
  ssid: string;
  signal: number; // 1–4
  secured: boolean;
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
  const [connectState, setConnectState] = useState<'idle' | 'connecting' | 'connected' | 'error'>('idle');
  const [connectedSsid, setConnectedSsid] = useState<string | null>(null);

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
    setConnectState('connecting');
    setTimeout(() => {
      // Simulate: wrong password if shorter than 3 chars and network is secured
      if (selected.secured && password.length > 0 && password.length < 3) {
        setConnectState('error');
      } else {
        setConnectState('connected');
        setConnectedSsid(selected.ssid);
      }
    }, 2200);
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

      {/* Network list */}
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
                      setSelected(n);
                      setPassword('');
                      setConnectState('idle');
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
            Verbanne mat <span className="text-primary font-mono normal-case">{selected.ssid}</span>
          </h2>

          {selected.secured ? (
            <div>
              <label className="block text-sm font-bold text-muted-foreground mb-2 uppercase tracking-wider">
                Passwuert
              </label>
              <div className="relative">
                <input
                  type={showPw ? 'text' : 'password'}
                  value={password}
                  onChange={e => { setPassword(e.target.value); setConnectState('idle'); }}
                  placeholder="WiFi Passwuert aginn..."
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
            <p className="text-sm text-muted-foreground">Dëst Netzwierk ass oppen — kee Passwuert néideg.</p>
          )}

          <TouchButton
            size="lg"
            variant="primary"
            className="gap-2"
            onClick={connect}
            disabled={connectState === 'connecting' || (selected.secured && password.length === 0)}
          >
            {connectState === 'connecting' ? (
              <><RefreshCw className="w-5 h-5 animate-spin" /> Verbënnt...</>
            ) : (
              <><Wifi className="w-5 h-5" /> Verbannen</>
            )}
          </TouchButton>

          {connectState === 'connected' && (
            <div className="flex items-center gap-3 bg-green-500/15 border border-green-500/40 rounded-lg px-4 py-3 font-bold text-green-400">
              <CheckCircle2 className="w-5 h-5" /> Verbindung erfollegräich
            </div>
          )}
          {connectState === 'error' && (
            <div className="flex items-center gap-3 bg-red-500/15 border border-red-500/40 rounded-lg px-4 py-3 font-bold text-red-400">
              <AlertTriangle className="w-5 h-5" /> Falscht Passwuert — probéiert nach eng Kéier
            </div>
          )}
        </div>
      )}
    </div>
  );
}
