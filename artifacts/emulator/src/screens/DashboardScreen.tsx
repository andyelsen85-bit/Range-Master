import React from 'react';
import { useGameStore, MASCHINEN_LIST } from '@/store/gameStore';
import { Play, Settings, RefreshCw, CircleDot, Wifi, WifiOff } from 'lucide-react';
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

          {/* Machine grid — toggle aktiv/deaktiviert */}
          <div className="flex-1 bg-card border-2 border-border rounded-xl p-8 flex flex-col">
            <h2 className="text-xl font-bold mb-2 text-foreground/70 uppercase tracking-widest">Schanzen</h2>
            <p className="text-sm text-muted-foreground mb-6">Tippen zum Aktivieren / Deaktivieren</p>

            <div className="grid grid-cols-4 gap-6 flex-1">
              {MASCHINEN_LIST.map((m) => {
                const aktiv = store.maschinenAktiv[m];
                const isDoublette = m === 'H';
                return (
                  <button
                    key={m}
                    onClick={() => store.toggleMaschineAktiv(m)}
                    className={cn(
                      "flex flex-col items-center justify-center rounded-xl border-4 transition-all active:scale-95",
                      aktiv
                        ? isDoublette
                          ? 'border-amber-500/60 bg-amber-500/10 text-amber-400'
                          : 'border-primary/60 bg-primary/10 text-primary'
                        : 'border-border/40 bg-background/50 text-muted-foreground/40',
                    )}
                  >
                    <span className="text-5xl font-bold mb-2">{m}</span>
                    <span className="text-sm font-bold uppercase tracking-wider">
                      {isDoublette ? 'Doublette' : 'Single'}
                    </span>
                    <span className={cn(
                      "text-xs mt-1 font-bold uppercase tracking-widest",
                      aktiv ? "text-green-400" : "text-muted-foreground/40"
                    )}>
                      {aktiv ? '● Aktiv' : '○ Aus'}
                    </span>
                  </button>
                );
              })}
            </div>
          </div>

          {/* Current Players */}
          <div className="bg-card border-2 border-border rounded-xl p-6">
            <h2 className="text-lg font-bold mb-4 text-foreground/70 uppercase tracking-widest">
              Schützen ({store.spieler.length})
            </h2>
            <div className="flex gap-4">
              {store.spieler.map((s, i) => (
                <div key={s.id} className="bg-background border-2 border-border rounded-lg p-4 flex-1 flex items-center gap-4">
                  <div className="w-10 h-10 rounded-full bg-primary/20 text-primary flex items-center justify-center font-bold text-lg">
                    P{s.startPosten}
                  </div>
                  <div className="font-bold text-lg truncate">{s.name}</div>
                </div>
              ))}
              {store.spieler.length === 0 && (
                <div className="text-muted-foreground font-bold italic">Keng Schützen konfiguriert</div>
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
            <span>Astellungen</span>
          </TouchButton>

          {/* Sync status */}
          <div
            className="mt-auto border-2 border-border rounded-xl p-6 flex flex-col items-center gap-3 cursor-pointer hover:border-primary/40 transition-colors"
            onClick={() => store.syncPortal()}
          >
            {store.syncStatus === 'syncing' ? (
              <RefreshCw className="w-10 h-10 text-yellow-500 animate-spin" />
            ) : store.syncStatus === 'success' ? (
              <Wifi className="w-10 h-10 text-green-500" />
            ) : store.syncStatus === 'error' ? (
              <WifiOff className="w-10 h-10 text-red-500" />
            ) : (
              <CircleDot className="w-10 h-10 text-muted-foreground" />
            )}
            <div className="text-center">
              <div className="font-bold text-base">
                {store.syncStatus === 'success' ? 'Portal verbonnen' :
                 store.syncStatus === 'error' ? 'Sync Feeler' :
                 store.syncStatus === 'syncing' ? 'Syncing...' :
                 'Net verbonnen'}
              </div>
              <div className="text-xs text-muted-foreground mt-1 font-mono">
                {store.lastSync ? `Läschte Sync: ${store.lastSync}` : 'Tippen fir ze syncen'}
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
