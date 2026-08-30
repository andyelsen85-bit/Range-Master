import React from 'react';
import { useGameStore } from '@/store/gameStore';
import { Play, Settings, RefreshCw, Upload, WifiOff, CheckCircle2, Clock, History, Coins, UserCog } from 'lucide-react';
import { TouchButton } from '@/components/TouchButton';
import { cn } from '@/lib/utils';

export function DashboardScreen() {
  const store = useGameStore();
  const [time, setTime] = React.useState(new Date());

  React.useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 1000);
    return () => clearInterval(timer);
  }, []);

  const pendingUpdates = store.spielerUpdates.filter(u => u.status === 'pending').length;
  const pendingCount = store.pendingGames.length + store.pendingSpieler.length + store.pendingKredite.length + pendingUpdates;
  const spillerMatKredit = Object.values(store.kredite)
    .filter(k => k.gewaehrt - k.verbraucht > 0).length;
  const isSyncing = store.syncStatus === 'syncing';
  const hasApiConfig = !!store.apiUrl && !!store.apiKey;

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

          {/* Last games */}
          <div className="flex-1 bg-card border-2 border-border rounded-xl p-8 flex flex-col overflow-hidden">
            <h2 className="text-xl font-bold mb-6 text-foreground/70 uppercase tracking-widest">Leschte Spiller</h2>
            {store.gameHistory.length === 0 ? (
              <div className="flex-1 flex items-center justify-center text-muted-foreground/50 font-bold italic text-lg">
                Nach keng Spiller gespillt
              </div>
            ) : (
              <div className="flex flex-col gap-4 flex-1 overflow-y-auto">
                {store.gameHistory.slice(0, 4).map((game) => {
                  const playerIds = [...new Set(game.teilnahmen.map(t => t.spielerId))];
                  const maxPts = game.lauf * game.taubenProLauf * 2;
                  return (
                    <div key={game.externalId} className="bg-background border-2 border-border rounded-xl p-5">
                      <div className="flex items-center justify-between mb-3">
                        <div className="flex items-center gap-3">
                          <span className="text-base font-mono text-muted-foreground">
                            {new Date(game.finishedAt).toLocaleDateString('de-LU', { day: '2-digit', month: '2-digit', year: 'numeric' })}
                            {' '}
                            {new Date(game.finishedAt).toLocaleTimeString('de-LU', { hour: '2-digit', minute: '2-digit' })}
                          </span>
                          <span className="text-xs font-black uppercase tracking-widest px-2 py-1 rounded-lg bg-primary/10 text-primary border border-primary/30">
                            {game.modus.replace(/_/g, ' ')}
                          </span>
                        </div>
                        <span className="text-xs font-mono text-muted-foreground/60">
                          {game.lauf}× {game.taubenProLauf} · max {maxPts} Pkt
                        </span>
                      </div>
                      <div className="flex gap-3 flex-wrap">
                        {playerIds.map(id => {
                          const total = game.teilnahmen
                            .filter(t => t.spielerId === id)
                            .reduce((s, t) => s + t.punkte, 0);
                          const pct = maxPts > 0 ? total / maxPts : 0;
                          return (
                            <div key={id} className="flex items-center gap-2 bg-card border border-border rounded-lg px-3 py-2">
                              <span className="font-bold text-sm">{game.spielerNamen[id] ?? `#${id}`}</span>
                              <span className={cn(
                                "text-lg font-black tabular-nums",
                                pct >= 0.9 ? "text-green-400" : pct >= 0.7 ? "text-primary" : "text-muted-foreground"
                              )}>{total}</span>
                              <span className="text-xs text-muted-foreground/50">/{maxPts}</span>
                            </div>
                          );
                        })}
                      </div>
                    </div>
                  );
                })}
              </div>
            )}
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

          <div className="flex gap-4">
            <TouchButton
              size="lg"
              className="flex-1 flex-col gap-2 h-28 relative"
              onClick={() => store.setScreen('spillerverwaltung')}
            >
              <UserCog className="w-8 h-8" />
              <span className="text-sm">Spiller</span>
              {pendingUpdates > 0 && (
                <span className="absolute top-2 right-3 text-xs font-black text-amber-400 bg-amber-500/15 border border-amber-500/40 rounded-lg px-2 py-0.5">
                  {pendingUpdates}
                </span>
              )}
            </TouchButton>
            <TouchButton
              size="lg"
              className="flex-1 flex-col gap-2 h-28"
              onClick={() => store.setScreen('einstellungen')}
            >
              <Settings className="w-8 h-8" />
              <span className="text-sm">Astellungen</span>
            </TouchButton>
          </div>

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
            <div className="bg-secondary/30 border-b-2 border-border px-3 py-1.5 flex items-center justify-between">
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

            <div className="p-2 flex flex-col gap-1">
              {/* Sync button */}
              <button
                onClick={() => store.syncAllPending()}
                disabled={!hasApiConfig || isSyncing}
                className={cn(
                  "w-full h-10 rounded-lg border-2 font-bold text-sm flex items-center justify-center gap-2 transition-all active:scale-95",
                  hasApiConfig && !isSyncing
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

              <div className="grid grid-cols-2 gap-2 text-[10px] font-mono text-muted-foreground/70">
                <span>Full {store.autoSyncSeconds}s · {store.lastFullSync ?? '—'}</span>
                <span>Billing {store.billingSyncSeconds}s · {store.lastBillingSync ?? '—'}</span>
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
