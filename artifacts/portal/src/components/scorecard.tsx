import type { SharedScorecard } from "@workspace/api-client-react";
import { cn } from "@/lib/utils";

/**
 * One scorecard for the whole portal. Imitates the club's paper slip:
 * machine letters A..H as column headings (repeated per Lauf), one row per
 * player, joined/shaded pair columns, counts and total on the right.
 */

const MODE_LABELS: Record<string, string> = {
  NORMAL: "Normal",
  HARAKIRI: "Harakiri",
  HARAKIRI_CUSTOM: "Harakiri Custom",
  HARAKIRI_DELAYED: "Harakiri Delayed",
  HARAKIRI_FULL: "Harakiri Full",
};

export function modeLabel(mode: string) {
  if (MODE_LABELS[mode]) return MODE_LABELS[mode];
  const m = /^CUSTOM_(\d)$/.exec(mode);
  return m ? `Custom ${m[1]}` : mode;
}

export function formatGameDate(game: Pick<SharedScorecard, "date" | "completedAt">) {
  return new Date(game.completedAt ?? game.date).toLocaleString("de-LU", {
    day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit",
  });
}

interface ScorecardProps {
  game: SharedScorecard;
  /** Large type for the clubhouse TV */
  size?: "default" | "tv";
  /** Hide the header row (date / mode) */
  hideHeader?: boolean;
  className?: string;
}

export function Scorecard({ game, size = "default", hideHeader, className }: ScorecardProps) {
  const tv = size === "tv";
  const cols = game.columns;
  const maxTotal = cols.length * 2;

  const cellW = tv ? "min-w-[3.1rem] w-[3.1rem]" : "min-w-[2rem] w-[2rem]";
  const cellText = tv ? "text-3xl" : "text-base";
  const headText = tv ? "text-2xl" : "text-xs";
  const nameW = tv ? "min-w-[14rem] max-w-[18rem]" : "min-w-[7.5rem] max-w-[10rem]";
  const sumW = tv ? "min-w-[4.5rem]" : "min-w-[2.6rem]";

  const isRunStart = (i: number) => i > 0 && cols[i].run !== cols[i - 1].run;

  return (
    <div className={cn("scorecard-paper rounded-lg overflow-hidden", className)} data-testid="scorecard">
      {!hideHeader && (
        <div className={cn("flex flex-wrap items-baseline justify-between gap-x-6 gap-y-1 border-b-2 border-[hsl(var(--slip-ink))] px-4", tv ? "py-4" : "py-2.5")}>
          <div className={cn("font-bold tracking-tight", tv ? "text-4xl" : "text-base")} data-testid="text-scorecard-date">
            {formatGameDate(game)}
          </div>
          <div className={cn("font-mono uppercase tracking-widest text-[hsl(var(--slip-muted))]", tv ? "text-xl" : "text-[11px] font-bold")}>
            {modeLabel(game.mode)} · {game.runs} {game.runs === 1 ? "Lauf" : "Läufe"} · {game.claysPerRun} Tauben / Lauf · max. {maxTotal}
          </div>
        </div>
      )}

      <div className="overflow-x-auto overscroll-x-contain">
        <table className="border-collapse w-max min-w-full">
          <thead>
            <tr>
              <th className={cn("slip-sticky slip-cell text-left px-3 font-bold uppercase tracking-widest text-[hsl(var(--slip-muted))]", nameW, tv ? "text-lg" : "text-[10px]")}>
                Nom
              </th>
              {cols.map((c, i) => (
                <th
                  key={`h-${i}`}
                  className={cn(
                    "slip-cell text-center font-black py-1.5",
                    cellW, headText,
                    c.pair && "slip-pair",
                    c.pair === "start" && "slip-pair-start",
                    c.pair === "end" && "slip-pair-end",
                    isRunStart(i) && "slip-run-start",
                  )}
                  title={`Lauf ${c.run}, Taube ${c.clay}${c.pair ? " (Doublé)" : ""}`}
                >
                  {c.machine}
                </th>
              ))}
              {game.runs > 1 && Array.from({ length: game.runs }, (_, r) => (
                <th key={`rt-${r}`} className={cn("slip-cell slip-sum-start text-center font-bold leading-tight px-1 text-[hsl(var(--slip-muted))]", sumW, tv ? "text-base" : "text-[9px]")}>
                  TOT<br />{r + 1}. LAUF
                </th>
              ))}
              <th className={cn("slip-cell text-center font-bold leading-tight px-1 text-[hsl(var(--slip-two))]", sumW, tv ? "text-base" : "text-[9px]", game.runs <= 1 && "slip-sum-start")}>2 PTS</th>
              <th className={cn("slip-cell text-center font-bold leading-tight px-1 text-[hsl(var(--slip-one))]", sumW, tv ? "text-base" : "text-[9px]")}>1 PT</th>
              <th className={cn("slip-cell text-center font-bold leading-tight px-1 text-[hsl(var(--slip-muted))]", sumW, tv ? "text-base" : "text-[9px]")}>0</th>
              <th className={cn("slip-cell slip-total text-center font-black leading-tight px-2", tv ? "text-lg min-w-[6rem]" : "text-[10px] min-w-[3.5rem]")}>TOTAL<br />POINTS</th>
            </tr>
          </thead>
          <tbody>
            {game.players.length === 0 && (
              <tr>
                <td colSpan={cols.length + game.runs + 5} className="slip-cell text-center py-6 italic text-[hsl(var(--slip-muted))]">
                  Keine Spieler erfasst
                </td>
              </tr>
            )}
            {game.players.map((p, pi) => {
              const twos = p.cells.filter((v) => v === 2).length;
              const ones = p.cells.filter((v) => v === 1).length;
              const zeros = p.cells.filter((v) => v === 0).length;
              return (
                <tr key={pi} data-testid={`row-scorecard-player-${pi}`}>
                  <td className={cn("slip-sticky slip-cell px-3 font-semibold truncate", nameW, tv ? "text-3xl py-3" : "text-sm py-1.5")} title={p.name}>
                    {p.name}
                  </td>
                  {cols.map((c, i) => {
                    const v = p.cells[i];
                    return (
                      <td
                        key={i}
                        className={cn(
                          "slip-cell text-center font-mono font-bold tabular-nums",
                          cellW, cellText,
                          c.pair && "slip-pair",
                          c.pair === "start" && "slip-pair-start",
                          c.pair === "end" && "slip-pair-end",
                          isRunStart(i) && "slip-run-start",
                          v === 2 && "text-[hsl(var(--slip-ink))]",
                          v === 1 && "text-[hsl(var(--slip-one))]",
                          v === 0 && "text-[hsl(var(--slip-two))]",
                          v == null && "text-[hsl(var(--slip-muted))] opacity-40",
                        )}
                      >
                        {v == null ? "·" : v}
                      </td>
                    );
                  })}
                  {game.runs > 1 && p.runTotals.map((t, r) => (
                    <td key={`t-${r}`} className={cn("slip-cell slip-sum-start text-center font-mono font-bold tabular-nums", tv ? "text-2xl" : "text-sm")}>{t}</td>
                  ))}
                  <td className={cn("slip-cell text-center font-mono tabular-nums text-[hsl(var(--slip-two))]", tv ? "text-2xl" : "text-sm", game.runs <= 1 && "slip-sum-start")}>{twos}</td>
                  <td className={cn("slip-cell text-center font-mono tabular-nums text-[hsl(var(--slip-one))]", tv ? "text-2xl" : "text-sm")}>{ones}</td>
                  <td className={cn("slip-cell text-center font-mono tabular-nums text-[hsl(var(--slip-muted))]", tv ? "text-2xl" : "text-sm")}>{zeros}</td>
                  <td className={cn("slip-cell slip-total text-center font-mono font-black tabular-nums", tv ? "text-4xl" : "text-lg")} data-testid={`text-scorecard-total-${pi}`}>
                    {p.total}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </div>
  );
}

export function ScorecardSkeleton() {
  return <div className="h-40 w-full rounded-lg bg-secondary/30 animate-pulse" />;
}
