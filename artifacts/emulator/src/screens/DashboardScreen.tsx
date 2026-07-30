import React from 'react';
import { useGameStore, MASCHINEN_LIST } from '@/store/gameStore';
import { Play, Settings, RefreshCw, Upload, WifiOff, CheckCircle2, Clock, History, Coins } from 'lucide-react';
import { TouchButton } from '@/components/TouchButton';
import { cn } from '@/lib/utils';

export function DashboardScreen() {
  const store = useGameStore();
  const [time, setTime] = React.useState(new Date());

  React.useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 1000);
    return () => clearInterval(timer);
  }, []);

  const pendingCount = store.pendingGames.length + store.pendingSpieler.length + store.pendingKredite.length;
  const spillerMatKredit = Object.values(store.kredite)
    .filter(k => k.gewaehrt - k.verbraucht > 0).length;
  const isSyncing = store.syncStatus === 'syncing';

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
              {store.spieler.map((s) => (
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
            className="w-full flex-col gap-2 h-28 relative"
            onClick={() => store.setScreen('kredite')}
          >
            <Coins className="w-8 h-8" />
            <span>Spiller vum Dag</span>
            {spillerMatKredit > 0 && (
              <span className="absolute top-2 right-3 text-xs font-black text-primary bg-primary/15 border border-primary/40 rounded-lg px-2 py-0.5">
                {spillerMatKredit}
              </span>
            )}
          </TouchButton>

          <TouchButton
            size="lg"
            className="w-full flex-col gap-2 h-28"
            onClick={() => store.setScreen('einstellungen')}
          >
            <Settings className="w-8 h-8" />
            <span>Astellungen</span>
          </TouchButton>

          <TouchButton
            size="lg"
            variant="ghost"
            className="w-full flex-col gap-2 h-24 relative"
            onClick={() => store.setScreen('geschichte')}
          >
            <History className="w-7 h-7" />
            <span className="text-sm">Spillgeschicht</span>
            {store.gameHistory.length > 0 && (
              <span className="absolute top-2 right-3 text-xs font-black text-muted-foreground/60 font-mono">
                {store.gameHistory.length}
              </span>
            )}
          </TouchButton>

          {/* Offline Queue / Sync Panel */}
          <div className="mt-auto border-2 border-border rounded-xl overflow-hidden">
            {/* Header row with pending count badge */}
            <div className="bg-secondary/30 border-b-2 border-border px-5 py-3 flex items-center justify-between">
              <span className="text-xs font-black uppercase tracking-widest text-muted-foreground">
                Offline Queue
              </span>
              <span className={cn(
                "px-3 py-1 rounded-lg text-base font-black tabular-nums transition-colors",
                pendingCount > 0
                  ? "bg-amber-500/20 text-amber-400 border border-amber-500/40"
                  : "bg-background text-muted-foreground/40 border border-border"
              )}>
                {store.pendingGames.length} {store.pendingGames.length === 1 ? 'Spill' : 'Spiller'}
                {store.pendingSpieler.length > 0 && ` · ${store.pendingSpieler.length} Nei`}
              </span>
            </div>

            <div className="p-5 flex flex-col gap-4">
              {/* Sync button */}
              <button
                onClick={() => store.syncAllPending()}
                disabled={pendingCount === 0 || isSyncing}
                className={cn(
                  "w-full h-16 rounded-xl border-2 font-bold text-base flex items-center justify-center gap-3 transition-all active:scale-95",
                  pendingCount > 0 && !isSyncing
                    ? "border-primary/60 bg-primary/10 text-primary hover:bg-primary/20"
                    : "border-border/40 bg-background/50 text-muted-foreground/40 cursor-not-allowed"
                )}
              >
                {isSyncing ? (
                  <>
                    <RefreshCw className="w-5 h-5 animate-spin" />
                    Syncing…
                  </>
                ) : (
                  <>
                    <Upload className="w-5 h-5" />
                    Alles syncen
                  </>
                )}
              </button>

              {/* Status line */}
              <div className="flex items-center gap-2 text-xs font-mono text-muted-foreground">
                {store.syncStatus === 'success' ? (
                  <>
                    <CheckCircle2 className="w-4 h-4 text-green-500 shrink-0" />
                    <span className="text-green-400">Synced {store.lastSync}</span>
                  </>
                ) : store.syncStatus === 'error' ? (
                  <>
                    <WifiOff className="w-4 h-4 text-red-500 shrink-0" />
                    <span className="text-red-400">Sync fehlgeschloen</span>
                  </>
                ) : store.lastSync ? (
                  <>
                    <Clock className="w-4 h-4 shrink-0" />
                    <span>Läschte Sync: {store.lastSync}</span>
                  </>
                ) : (
                  <>
                    <Clock className="w-4 h-4 shrink-0 opacity-40" />
                    <span className="opacity-40">Nach net verbonnen</span>
                  </>
                )}
              </div>

              {/* Cache hint when player list was loaded from cache */}
              {store.spielerAusCache && (
                <div className="text-xs text-amber-400/80 font-medium text-center bg-amber-500/10 rounded-lg px-3 py-2">
                  Spillerlëscht aus lokalem Cache
                </div>
              )}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
