import React, { useState } from 'react';
import { useGameStore, FinishedGame, Modus } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { Trophy, ChevronLeft, ChevronDown, ChevronUp, Calendar, Users, Target } from 'lucide-react';
import { cn } from '@/lib/utils';

const MODUS_LABEL: Record<Modus, string> = {
  NORMAL: 'Normal',
  HARAKIRI: 'Harakiri',
  HARAKIRI_DELAYED: 'H. Delayed',
  HARAKIRI_FULL: 'H. Full',
  CUSTOM_1: 'Custom 1',
  CUSTOM_2: 'Custom 2',
  CUSTOM_3: 'Custom 3',
};

const MODUS_COLOR: Record<Modus, string> = {
  NORMAL: 'bg-primary/15 text-primary border-primary/40',
  HARAKIRI: 'bg-red-500/15 text-red-400 border-red-500/40',
  HARAKIRI_DELAYED: 'bg-red-500/15 text-red-400 border-red-500/40',
  HARAKIRI_FULL: 'bg-red-500/20 text-red-300 border-red-500/50',
  CUSTOM_1: 'bg-purple-500/15 text-purple-400 border-purple-500/40',
  CUSTOM_2: 'bg-purple-500/15 text-purple-400 border-purple-500/40',
  CUSTOM_3: 'bg-purple-500/15 text-purple-400 border-purple-500/40',
};

function GameDetailPanel({ game }: { game: FinishedGame }) {
  const maxPerGame = game.taubenProLauf * 2 * 2;
  const maxPerLauf = game.taubenProLauf * 2;

  const playerIds = Object.keys(game.spielerNamen).map(Number);
  const rows = playerIds.map(id => {
    const lauf1 = game.ergebnisse.filter(e => e.spielerId === id && e.lauf === 1)
      .reduce((s, e) => s + e.punkte, 0);
    const lauf2 = game.ergebnisse.filter(e => e.spielerId === id && e.lauf === 2)
      .reduce((s, e) => s + e.punkte, 0);
    return { id, name: game.spielerNamen[id] ?? `Spiller ${id}`, lauf1, lauf2, total: lauf1 + lauf2 };
  }).sort((a, b) => b.total - a.total);

  return (
    <div className="mt-2 ml-12 bg-background border border-border/50 rounded-xl overflow-hidden">
      {/* Column headers */}
      <div className="grid grid-cols-[1fr_auto_auto_auto] gap-4 px-4 py-2 bg-secondary/20 border-b border-border/40">
        <div className="text-[9px] font-bold uppercase tracking-widest text-muted-foreground">Spiller</div>
        <div className="text-[9px] font-bold uppercase tracking-widest text-muted-foreground w-16 text-right">Lauf 1</div>
        <div className="text-[9px] font-bold uppercase tracking-widest text-muted-foreground w-16 text-right">Lauf 2</div>
        <div className="text-[9px] font-bold uppercase tracking-widest text-muted-foreground w-16 text-right">Total</div>
      </div>
      {rows.map((row, i) => (
        <div
          key={row.id}
          className={cn(
            "grid grid-cols-[1fr_auto_auto_auto] gap-4 px-4 py-2.5 items-center border-b border-border/20 last:border-0",
            i === 0 && "bg-amber-500/5"
          )}
        >
          <div className="flex items-center gap-2 min-w-0">
            {i === 0 && <Trophy className="w-3 h-3 text-amber-400 shrink-0" />}
            <span className={cn("font-bold text-sm truncate", i === 0 ? "text-amber-300" : "text-foreground")}>
              {row.name}
            </span>
          </div>
          <div className="font-mono text-sm text-muted-foreground w-16 text-right">
            {row.lauf1}<span className="text-muted-foreground/40 text-xs">/{maxPerLauf}</span>
          </div>
          <div className="font-mono text-sm text-muted-foreground w-16 text-right">
            {row.lauf2}<span className="text-muted-foreground/40 text-xs">/{maxPerLauf}</span>
          </div>
          <div className={cn("font-black text-sm w-16 text-right tabular-nums", i === 0 ? "text-amber-400" : "text-primary")}>
            {row.total}<span className="text-muted-foreground/40 font-normal text-xs">/{maxPerGame}</span>
          </div>
        </div>
      ))}
    </div>
  );
}

export function SpillgeschichteScreen() {
  const store = useGameStore();
  const history = store.gameHistory; // newest first
  const [expandedId, setExpandedId] = useState<string | null>(null);

  const toggle = (id: string) => setExpandedId(prev => prev === id ? null : id);

  return (
    <div className="flex h-full w-full flex-col bg-background">
      {/* Header */}
      <header className="h-20 border-b-2 border-border flex items-center justify-between px-8 bg-card shrink-0">
        <div className="flex items-center gap-4">
          <TouchButton
            variant="ghost"
            className="gap-2"
            onClick={() => store.setScreen('dashboard')}
          >
            <ChevronLeft className="w-5 h-5" />
            Zréck
          </TouchButton>
          <div className="w-px h-8 bg-border" />
          <div>
            <h1 className="text-2xl font-bold tracking-wider text-primary">SPILLGESCHICHT</h1>
            <p className="text-xs font-mono text-muted-foreground uppercase tracking-widest">
              {history.length} {history.length === 1 ? 'Spill' : 'Spiller'} · läscht 50
            </p>
          </div>
        </div>
        {history.length > 0 && (
          <div className="text-xs font-mono text-muted-foreground">
            Tippen fir Detailer ze gesinn
          </div>
        )}
      </header>

      {/* Game list */}
      <div className="flex-1 overflow-y-auto p-6">
        {history.length === 0 ? (
          <div className="flex flex-col items-center justify-center h-full gap-4 text-muted-foreground/40">
            <Target className="w-16 h-16" />
            <div className="text-lg font-bold uppercase tracking-widest">Keng Spiller gespillt</div>
            <div className="text-sm">Nodeems dir e Spill fäerdeg stellt, erschéngt et hei.</div>
          </div>
        ) : (
          <div className="flex flex-col gap-2">
            {history.map((game, idx) => {
              const finishedAt = new Date(game.finishedAt);
              const dateStr = finishedAt.toLocaleDateString('de-DE', { day: '2-digit', month: '2-digit', year: 'numeric' });
              const timeStr = finishedAt.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });
              const isExpanded = expandedId === game.externalId;

              // Compute winner and player count
              const playerIds = Object.keys(game.spielerNamen).map(Number);
              const playerCount = playerIds.length;
              const maxPerGame = game.taubenProLauf * 2 * 2;

              const topPlayer = playerIds.map(id => ({
                name: game.spielerNamen[id],
                total: game.ergebnisse
                  .filter(e => e.spielerId === id)
                  .reduce((s, e) => s + e.punkte, 0),
              })).sort((a, b) => b.total - a.total)[0];

              return (
                <div key={game.externalId} className="flex flex-col">
                  <button
                    onClick={() => toggle(game.externalId)}
                    className={cn(
                      "flex items-center gap-4 rounded-xl border-2 px-5 py-4 transition-all text-left active:scale-[0.99]",
                      isExpanded
                        ? "border-primary/50 bg-primary/5 rounded-b-none"
                        : "border-border/40 bg-card hover:border-border hover:bg-card/80"
                    )}
                  >
                    {/* Game number */}
                    <div className="w-10 text-right">
                      <span className="text-xs font-bold text-muted-foreground/50 font-mono">#{history.length - idx}</span>
                    </div>

                    {/* Date & time */}
                    <div className="flex items-center gap-2 w-40 shrink-0">
                      <Calendar className="w-4 h-4 text-muted-foreground/50 shrink-0" />
                      <div>
                        <div className="text-sm font-bold text-foreground">{dateStr}</div>
                        <div className="text-xs font-mono text-muted-foreground">{timeStr}</div>
                      </div>
                    </div>

                    {/* Modus badge */}
                    <span className={cn(
                      "px-2.5 py-1 rounded-lg border text-xs font-bold uppercase tracking-wider shrink-0",
                      MODUS_COLOR[game.modus]
                    )}>
                      {MODUS_LABEL[game.modus]}
                    </span>

                    {/* Players */}
                    <div className="flex items-center gap-1.5 shrink-0">
                      <Users className="w-4 h-4 text-muted-foreground/50" />
                      <span className="text-sm font-bold text-muted-foreground">{playerCount}</span>
                    </div>

                    {/* Max score info */}
                    <div className="text-xs font-mono text-muted-foreground shrink-0">
                      max {maxPerGame} Pkt
                    </div>

                    {/* Winner */}
                    {topPlayer && (
                      <div className="flex-1 flex items-center gap-2 min-w-0 justify-end">
                        <Trophy className="w-3.5 h-3.5 text-amber-400 shrink-0" />
                        <span className="text-sm font-bold text-amber-300 truncate">{topPlayer.name}</span>
                        <span className="text-sm font-black text-amber-400 shrink-0 tabular-nums">
                          {topPlayer.total}
                          <span className="text-xs font-bold text-muted-foreground">/{maxPerGame}</span>
                        </span>
                      </div>
                    )}

                    {/* Expand chevron */}
                    <div className="shrink-0 ml-2">
                      {isExpanded
                        ? <ChevronUp className="w-5 h-5 text-primary" />
                        : <ChevronDown className="w-5 h-5 text-muted-foreground/50" />}
                    </div>
                  </button>

                  {/* Expanded detail */}
                  {isExpanded && (
                    <div className={cn(
                      "border-2 border-t-0 border-primary/50 bg-primary/5 rounded-b-xl px-5 pb-5 pt-3",
                    )}>
                      <GameDetailPanel game={game} />
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        )}
      </div>
    </div>
  );
}
