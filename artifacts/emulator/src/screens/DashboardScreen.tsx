import React from 'react';
import { useGameStore, getAktiverSpieler } from '@/store/gameStore';
import { Play, Settings, RefreshCw, CircleDot } from 'lucide-react';
import { TouchButton } from '@/components/TouchButton';
import { cn } from '@/lib/utils';

export function DashboardScreen() {
  const store = useGameStore();
  const [time, setTime] = React.useState(new Date());

  React.useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 1000);
    return () => clearInterval(timer);
  }, []);

  return (
    <div className="flex h-full w-full bg-background flex-col">
      {/* Header */}
      <header className="h-20 border-b-2 border-border flex items-center justify-between px-8 bg-card">
        <h1 className="text-2xl font-bold tracking-wider text-primary">F.S.H.C.L. SEKTIOUN WOLZ</h1>
        <div className="text-xl font-mono text-foreground/80">
          {time.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' })}
        </div>
      </header>

      <div className="flex flex-1 overflow-hidden">
        {/* Main Content */}
        <div className="flex-1 p-8 flex flex-col gap-8">
          
          <div className="flex-1 bg-card border-2 border-border rounded-xl p-8 flex flex-col">
            <h2 className="text-xl font-bold mb-6 text-foreground/70 uppercase tracking-widest">Maschinen Status</h2>
            
            <div className="grid grid-cols-4 gap-6 flex-1">
              {(Object.keys(store.maschinenStatus) as Array<keyof typeof store.maschinenStatus>).map((m) => {
                const status = store.maschinenStatus[m];
                return (
                  <button
                    key={m}
                    onClick={() => store.toggleMaschineStatus(m)}
                    className={cn(
                      "flex flex-col items-center justify-center rounded-xl border-4 transition-all",
                      "active:scale-95 active:brightness-110",
                      {
                        'border-green-500/50 bg-green-500/10 text-green-500': status === 'ok',
                        'border-red-500/50 bg-red-500/10 text-red-500': status === 'fehler',
                        'border-border bg-background text-muted-foreground': status === 'offline',
                      }
                    )}
                  >
                    <span className="text-5xl font-bold mb-2">{m}</span>
                    <span className="text-lg font-bold uppercase tracking-wider">{status}</span>
                  </button>
                );
              })}
            </div>
          </div>

          <div className="bg-card border-2 border-border rounded-xl p-6">
            <h2 className="text-lg font-bold mb-4 text-foreground/70 uppercase tracking-widest">Aktuelle Spieler</h2>
            <div className="flex gap-4">
              {store.spieler.map((s, i) => (
                <div key={s.id} className="bg-background border-2 border-border rounded-lg p-4 flex-1 flex items-center gap-4">
                  <div className="w-10 h-10 rounded-full bg-primary/20 text-primary flex items-center justify-center font-bold text-lg">
                    {i + 1}
                  </div>
                  <div className="font-bold text-lg truncate">{s.name}</div>
                </div>
              ))}
              {store.spieler.length < 5 && (
                <div className="bg-background/50 border-2 border-dashed border-border rounded-lg p-4 flex-1 flex items-center justify-center text-muted-foreground font-bold">
                  Leer
                </div>
              )}
            </div>
          </div>

        </div>

        {/* Sidebar */}
        <div className="w-80 border-l-2 border-border bg-card p-6 flex flex-col gap-6">
          <TouchButton 
            size="xl" 
            variant="primary"
            className="w-full flex-col gap-2 h-40"
            onClick={() => store.setScreen('start')}
          >
            <Play className="w-12 h-12" />
            <span>Spill Start</span>
          </TouchButton>

          <TouchButton 
            size="lg" 
            className="w-full flex-col gap-2 h-32"
            onClick={() => store.setScreen('einstellungen')}
          >
            <Settings className="w-8 h-8" />
            <span>Einstellungen</span>
          </TouchButton>

          <div className="mt-auto border-2 border-border rounded-xl p-6 flex flex-col items-center gap-4">
            <CircleDot className={cn(
              "w-12 h-12",
              store.syncStatus === 'success' ? "text-green-500" :
              store.syncStatus === 'error' ? "text-red-500" :
              store.syncStatus === 'syncing' ? "text-yellow-500 animate-spin" :
              "text-muted-foreground"
            )} />
            <div className="text-center">
              <div className="font-bold text-lg">
                {store.syncStatus === 'success' ? 'Portal verbonnen' : 'Net verbonnen'}
              </div>
              <div className="text-sm text-muted-foreground mt-1 font-mono">
                {store.lastSync ? `Letzter Sync: ${store.lastSync}` : 'Kein Sync'}
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
