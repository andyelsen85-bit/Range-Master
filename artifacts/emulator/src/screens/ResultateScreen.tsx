import React from 'react';
import { useGameStore, FinishedGame, Modus } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { Trophy, Medal, ChevronRight, Clock, Layers } from 'lucide-react';
import { cn } from '@/lib/utils';

const MODUS_LABEL: Record<Modus, string> = {
  NORMAL: 'Normal',
  HARAKIRI: 'Harakiri',
  HARAKIRI_DELAYED: 'Harakiri Delayed',
  HARAKIRI_FULL: 'Harakiri Full',
  CUSTOM_1: 'Custom 1',
  CUSTOM_2: 'Custom 2',
  CUSTOM_3: 'Custom 3',
};

function RankBadge({ rank }: { rank: number }) {
  if (rank === 1) return (
    <div className="w-9 h-9 rounded-full bg-amber-500/20 border-2 border-amber-500/60 flex items-center justify-center shrink-0">
      <Trophy className="w-4 h-4 text-amber-400" />
    </div>
  );
  if (rank === 2) return (
    <div className="w-9 h-9 rounded-full bg-slate-400/20 border-2 border-slate-400/50 flex items-center justify-center shrink-0">
      <Medal className="w-4 h-4 text-slate-300" />
    </div>
  );
  if (rank === 3) return (
    <div className="w-9 h-9 rounded-full bg-orange-700/20 border-2 border-orange-700/50 flex items-center justify-center shrink-0">
      <Medal className="w-4 h-4 text-orange-600" />
    </div>
  );
  return (
    <div className="w-9 h-9 rounded-full bg-background border-2 border-border flex items-center justify-center shrink-0">
      <span className="text-sm font-black text-muted-foreground">{rank}</span>
    </div>
  );
}

function ScoreBar({ value, max }: { value: number; max: number }) {
  const pct = max > 0 ? Math.round((value / max) * 100) : 0;
  return (
    <div className="flex items-center gap-2 w-full">
      <div className="flex-1 h-2 bg-background rounded-full overflow-hidden">
        <div
          className="h-full bg-primary rounded-full transition-all"
          style={{ width: `${pct}%` }}
        />
      </div>
      <span className="text-xs font-bold text-muted-foreground w-8 text-right tabular-nums">{pct}%</span>
    </div>
  );
}

export function ResultateScreen() {
  const store = useGameStore();
  const game = store.lastFinishedGame;

  if (!game) {
    store.dismissResultate();
    return null;
  }

  // Build per-player summary
  const maxPerGame = game.taubenProLauf * 2 * 2; // 2 Läufe
  const maxPerLauf = game.taubenProLauf * 2;

  const playerIds = Object.keys(game.spielerNamen).map(Number);

  const rows = playerIds.map(id => {
    const lauf1 = game.ergebnisse.filter(e => e.spielerId === id && e.lauf === 1)
      .reduce((s, e) => s + e.punkte, 0);
    const lauf2 = game.ergebnisse.filter(e => e.spielerId === id && e.lauf === 2)
      .reduce((s, e) => s + e.punkte, 0);
    const total = lauf1 + lauf2;
    return { id, name: game.spielerNamen[id] ?? `Spiller ${id}`, lauf1, lauf2, total };
  }).sort((a, b) => b.total - a.total);

  const finishedAt = new Date(game.finishedAt);
  const dateStr = finishedAt.toLocaleDateString('de-DE', { day: '2-digit', month: '2-digit', year: 'numeric' });
  const timeStr = finishedAt.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });

  const winner = rows[0];

  return (
    <div className="flex h-full w-full flex-col bg-background">
      {/* Header */}
      <header className="h-20 border-b-2 border-border flex items-center justify-between px-8 bg-card shrink-0">
        <div className="flex items-center gap-4">
          <Trophy className="w-7 h-7 text-amber-400" />
          <div>
            <h1 className="text-2xl font-bold tracking-wider text-primary">RESULTATER</h1>
            <p className="text-xs font-mono text-muted-foreground uppercase tracking-widest">
              {MODUS_LABEL[game.modus]} · {game.taubenProLauf} Tauben/Lauf · max {maxPerGame} Pkt
            </p>
          </div>
        </div>
        <div className="flex items-center gap-2 text-sm font-mono text-muted-foreground">
          <Clock className="w-4 h-4" />
          <span>{dateStr} {timeStr}</span>
        </div>
      </header>

      <div className="flex flex-1 overflow-hidden">
        {/* Left: ranking table */}
        <div className="flex-1 p-8 flex flex-col gap-4 overflow-y-auto">

          {/* Winner spotlight */}
          {winner && (
            <div className="bg-amber-500/10 border-2 border-amber-500/40 rounded-2xl p-5 flex items-center gap-5">
              <div className="w-14 h-14 rounded-full bg-amber-500/20 border-2 border-amber-500/60 flex items-center justify-center shrink-0">
                <Trophy className="w-7 h-7 text-amber-400" />
              </div>
              <div className="flex-1 min-w-0">
                <div className="text-xs font-bold uppercase tracking-widest text-amber-500/70 mb-0.5">Gewënner</div>
                <div className="text-2xl font-black text-amber-300 truncate">{winner.name}</div>
                <ScoreBar value={winner.total} max={maxPerGame} />
              </div>
              <div className="text-right shrink-0">
                <div className="text-4xl font-black text-amber-400 tabular-nums">{winner.total}</div>
                <div className="text-sm font-bold text-amber-500/60">/ {maxPerGame}</div>
              </div>
            </div>
          )}

          {/* All players */}
          <div className="flex flex-col gap-2">
            {rows.map((row, i) => {
              const isWinner = i === 0;
              return (
                <div
                  key={row.id}
                  className={cn(
                    "flex items-center gap-4 rounded-xl border-2 px-5 py-3 transition-colors",
                    isWinner
                      ? "border-amber-500/30 bg-amber-500/5"
                      : "border-border/40 bg-card"
                  )}
                >
                  <RankBadge rank={i + 1} />

                  <div className="flex-1 min-w-0">
                    <div className={cn("font-bold text-base truncate", isWinner ? "text-amber-300" : "text-foreground")}>
                      {row.name}
                    </div>
                    <div className="flex items-center gap-3 mt-1">
                      <span className="text-xs font-mono text-muted-foreground">
                        L1: <span className="text-foreground font-bold">{row.lauf1}</span>/{maxPerLauf}
                      </span>
                      <span className="text-muted-foreground/30">·</span>
                      <span className="text-xs font-mono text-muted-foreground">
                        L2: <span className="text-foreground font-bold">{row.lauf2}</span>/{maxPerLauf}
                      </span>
                    </div>
                  </div>

                  {/* Score bar */}
                  <div className="w-36 hidden sm:block">
                    <ScoreBar value={row.total} max={maxPerGame} />
                  </div>

                  <div className="text-right shrink-0">
                    <div className={cn("text-2xl font-black tabular-nums", isWinner ? "text-amber-400" : "text-primary")}>
                      {row.total}
                    </div>
                    <div className="text-xs font-bold text-muted-foreground">/ {maxPerGame}</div>
                  </div>
                </div>
              );
            })}
          </div>
        </div>

        {/* Right sidebar: actions */}
        <div className="w-72 border-l-2 border-border flex flex-col gap-4 p-6">
          <div className="bg-card border-2 border-border rounded-xl p-4 flex flex-col gap-1">
            <div className="text-[10px] font-bold uppercase tracking-widest text-muted-foreground">Spilltyp</div>
            <div className="font-bold text-primary">{MODUS_LABEL[game.modus]}</div>
            <div className="text-xs text-muted-foreground font-mono mt-1">
              {game.taubenProLauf} Tauben/Lauf · 2 Läufe
            </div>
            <div className="flex items-center gap-2 mt-2 pt-2 border-t border-border/40">
              <Layers className="w-3 h-3 text-muted-foreground" />
              <span className="text-xs font-mono text-muted-foreground">{rows.length} Spiller</span>
            </div>
          </div>

          <div className="mt-auto flex flex-col gap-3">
            <TouchButton
              size="lg"
              variant="primary"
              className="w-full flex items-center justify-center gap-3"
              onClick={() => store.dismissResultate()}
            >
              <ChevronRight className="w-6 h-6" />
              <span>Weider</span>
            </TouchButton>

            <TouchButton
              size="sm"
              variant="ghost"
              className="w-full text-muted-foreground"
              onClick={() => store.setScreen('geschichte')}
            >
              Spillgeschicht
            </TouchButton>
          </div>
        </div>
      </div>
    </div>
  );
}
