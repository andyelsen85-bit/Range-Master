import React, { useState, useEffect } from 'react';
import { Bluetooth, BluetoothOff, BluetoothSearching, CheckCircle2, AlertTriangle, RefreshCw, X, Keyboard } from 'lucide-react';
import { TouchButton } from '@/components/TouchButton';
import { cn } from '@/lib/utils';

interface BtDevice {
  id: string;
  name: string;
  type: 'keyboard' | 'other';
  rssi: number; // mock signal strength -40 to -90
}

const MOCK_DEVICES: BtDevice[] = [
  { id: 'BT:AA:11:22:33:44', name: 'RangeMaster Keyboard Pro',  type: 'keyboard', rssi: -42 },
  { id: 'BT:BB:55:66:77:88', name: 'Logitech K380',              type: 'keyboard', rssi: -61 },
  { id: 'BT:CC:99:AA:BB:CC', name: 'Bluetooth HID Device',       type: 'keyboard', rssi: -74 },
  { id: 'BT:DD:DE:AD:BE:EF', name: 'Unknown Device',             type: 'other',    rssi: -88 },
];

function RssiDots({ rssi }: { rssi: number }) {
  // Map rssi to 1–4 bars: ≥-50 → 4, ≥-65 → 3, ≥-75 → 2, else 1
  const bars = rssi >= -50 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  return (
    <div className="flex items-end gap-[2px]">
      {[1, 2, 3, 4].map(i => (
        <div
          key={i}
          style={{ height: `${i * 4 + 4}px`, width: '4px' }}
          className={cn('rounded-[1px]', i <= bars ? 'bg-primary' : 'bg-border/60')}
        />
      ))}
    </div>
  );
}

type PairState = 'idle' | 'pairing' | 'paired' | 'error';

export function BluetoothScreen() {
  const [scanning, setScanning] = useState(false);
  const [devices, setDevices] = useState<BtDevice[]>([]);
  const [pairedId, setPairedId] = useState<string | null>(null);
  const [pairingId, setPairingId] = useState<string | null>(null);
  const [pairState, setPairState] = useState<PairState>('idle');

  const scan = () => {
    setScanning(true);
    setDevices([]);
    setPairState('idle');
    // Simulate progressive discovery
    setTimeout(() => setDevices(MOCK_DEVICES.slice(0, 1)), 600);
    setTimeout(() => setDevices(MOCK_DEVICES.slice(0, 2)), 1100);
    setTimeout(() => setDevices(MOCK_DEVICES.slice(0, 3)), 1600);
    setTimeout(() => {
      setDevices(MOCK_DEVICES);
      setScanning(false);
    }, 2200);
  };

  useEffect(() => { scan(); }, []);

  const pair = (device: BtDevice) => {
    setPairingId(device.id);
    setPairState('pairing');
    setTimeout(() => {
      // Simulate: "unknown" device fails, keyboards succeed
      if (device.type === 'other') {
        setPairState('error');
        setPairingId(null);
      } else {
        setPairState('paired');
        setPairedId(device.id);
        setPairingId(null);
      }
    }, 2400);
  };

  const unpair = () => {
    setPairedId(null);
    setPairState('idle');
    setPairingId(null);
  };

  const pairedDevice = devices.find(d => d.id === pairedId);

  return (
    <div className="flex flex-col gap-5 max-w-2xl">

      {/* Simulation banner */}
      <div className="flex items-center gap-3 bg-amber-500/10 border-2 border-amber-500/40 rounded-xl px-5 py-3">
        <AlertTriangle className="w-5 h-5 text-amber-400 shrink-0" />
        <p className="text-sm font-bold text-amber-300">
          Dëse Bildschirm ass nëmmen um physesche Terminal disponibel
        </p>
      </div>

      {/* Paired device status */}
      {pairedDevice && (
        <div className="flex items-center justify-between bg-green-500/10 border-2 border-green-500/40 rounded-xl px-5 py-3">
          <div className="flex items-center gap-3">
            <CheckCircle2 className="w-5 h-5 text-green-400 shrink-0" />
            <div>
              <span className="text-sm font-bold text-green-300">Verbonnen</span>
              <span className="text-sm font-mono text-green-200 ml-2">{pairedDevice.name}</span>
              <div className="text-xs text-muted-foreground font-mono mt-0.5">{pairedDevice.id}</div>
            </div>
          </div>
          <TouchButton variant="ghost" className="h-8 px-3 text-xs text-destructive hover:bg-destructive/10 gap-1" onClick={unpair}>
            <X className="w-3 h-3" /> Trennen
          </TouchButton>
        </div>
      )}

      {/* Device list */}
      <div className="bg-card border-2 border-border rounded-xl p-5 flex flex-col gap-4">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <h2 className="text-base font-bold uppercase tracking-widest">Geräter</h2>
            {scanning && (
              <div className="flex items-center gap-1 text-xs text-primary">
                <BluetoothSearching className="w-4 h-4 animate-pulse" />
                <span>Sichen...</span>
              </div>
            )}
          </div>
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

        {!scanning && devices.length === 0 && (
          <div className="flex items-center justify-center py-8 gap-3 text-muted-foreground">
            <BluetoothOff className="w-5 h-5" />
            <span className="text-sm">Keng Geräter fonnt</span>
          </div>
        )}

        {devices.length > 0 && (
          <div className="flex flex-col gap-2">
            {devices.map(device => {
              const isPaired   = pairedId === device.id;
              const isPairing  = pairingId === device.id;
              return (
                <div
                  key={device.id}
                  className={cn(
                    'flex items-center justify-between px-4 py-3 rounded-lg border-2 transition-all',
                    isPaired
                      ? 'border-green-500/50 bg-green-500/10'
                      : 'border-border bg-background',
                  )}
                >
                  <div className="flex items-center gap-3">
                    {device.type === 'keyboard' ? (
                      <Keyboard className={cn('w-5 h-5', isPaired ? 'text-green-400' : 'text-primary')} />
                    ) : (
                      <Bluetooth className="w-5 h-5 text-muted-foreground" />
                    )}
                    <div>
                      <div className={cn('font-bold text-sm', isPaired ? 'text-green-300' : 'text-foreground')}>
                        {device.name}
                        {isPaired && <span className="text-green-400 font-bold text-xs ml-2">● Verbonnen</span>}
                      </div>
                      <div className="text-xs text-muted-foreground font-mono mt-0.5">
                        {device.id}
                        {device.type === 'keyboard' && (
                          <span className="ml-2 text-primary/70">· Tastatur</span>
                        )}
                      </div>
                    </div>
                  </div>

                  <div className="flex items-center gap-3">
                    <RssiDots rssi={device.rssi} />
                    {!isPaired && (
                      <TouchButton
                        variant="outline"
                        className="h-8 px-3 text-xs gap-1"
                        onClick={() => pair(device)}
                        disabled={isPairing || !!pairedId}
                      >
                        {isPairing ? (
                          <><RefreshCw className="w-3 h-3 animate-spin" /> Koppelen...</>
                        ) : (
                          <>Koppelen</>
                        )}
                      </TouchButton>
                    )}
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>

      {/* Pairing result feedback */}
      {pairState === 'paired' && pairedDevice && (
        <div className="flex items-center gap-3 bg-green-500/15 border border-green-500/40 rounded-lg px-4 py-3 font-bold text-green-400">
          <CheckCircle2 className="w-5 h-5" />
          Kopplung erfollegräich — <span className="font-mono">{pairedDevice.name}</span> ass bereet
        </div>
      )}
      {pairState === 'error' && (
        <div className="flex items-center gap-3 bg-red-500/15 border border-red-500/40 rounded-lg px-4 py-3 font-bold text-red-400">
          <AlertTriangle className="w-5 h-5" />
          Kopplung fehlgeschloen — Gerät net ënnerstëtzt
        </div>
      )}

      {/* Info box */}
      <div className="bg-card border-2 border-border rounded-xl p-5">
        <h3 className="text-sm font-bold uppercase tracking-widest mb-3">Hinweis</h3>
        <p className="text-sm text-muted-foreground leading-relaxed">
          Den Terminal akzeptéiert nëmmen Bluetooth <span className="text-foreground font-bold">HID Tastature</span>.
          Nodeems eng Tastatur gekoppelt ass, kann se direkt benotzt ginn fir Spillernimm an Scores aginn.
          D'Kopplung bleift gespäichert och no engem Neustart.
        </p>
      </div>
    </div>
  );
}
