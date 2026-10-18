import { memo, useEffect, useMemo, useRef, useState } from "react";
import { keepPreviousData } from "@tanstack/react-query";
import { useGetKioskLiveGame, getGetKioskLiveGameQueryKey } from "@workspace/api-client-react";
import { modeLabel } from "@/components/scorecard";
import { cn } from "@/lib/utils";
import { reportKioskError } from "@/lib/kiosk";
import type { LiveGame, LiveResult } from "./types";

/**
 * Running game (display only). Columns are the raw taube slots per Lauf of each
 * player's own shots; nothing about future machines or a common Harakiri order
 * is inferred. Cells are memoised with stable keys so a new shot only updates
 * its own cell.
 */
export function LiveGamePanel({ enabled, onConnection }: {
  enabled: boolean;
  onConnection?: (ok: boolean) => void;
}) {
  const { data, error, isError } = useGetKioskLiveGame({
    query: {
      queryKey: getGetKioskLiveGameQueryKey(),
      enabled,
      refetchInterval: 3_000,
      refetchIntervalInBackground: true,
      placeholderData: keepPreviousData,
      retry: 1,
    },
  });
  useEffect(() => { if (error) reportKioskError(error); }, [error]);
  const onConnRef = useRef(onConnection);
  onConnRef.current = onConnection;
  useEffect(() => { onConnRef.current?.(!isError); }, [isError]);

  // Spec 8.1/8.2: null (not running, finished or stale > 120 s) means no panel at all.
  // A "BEENDET" bridge is not possible: the API never returns FINISHED snapshots.
  const live = (data ?? null) as unknown as LiveGame | null;
  if (!live || isError) return null;
  return <LiveGameCard game={live} />;
}

export function LiveGameCard({ game, finished }: { game: LiveGame; finished?: boolean }) {
  return (
    <section className="kiosk-slide-in rounded-2xl border border-primary/40 bg-card/80 p-5 space-y-4" data-testid="panel-live-game">
      <div className="flex flex-wrap items-center justify-between gap-4">
        <div className="flex items-center gap-4">
          {finished ? (
            <span className="inline-flex items-center gap-2 rounded-full bg-secondary px-4 py-1.5 text-lg font-black tracking-[0.2em] text-muted-foreground" data-testid="badge-live-finished">BEENDET</span>
          ) : (
            <span className="inline-flex items-center gap-2.5 rounded-full bg-destructive/15 border border-destructive/40 px-4 py-1.5 text-lg font-black tracking-[0.2em] text-destructive" data-testid="badge-live-running">
              <span className="relative flex h-3 w-3">
                <span className="absolute inline-flex h-full w-full animate-ping rounded-full bg-destructive opacity-70" />
                <span className="relative inline-flex h-3 w-3 rounded-full bg-destructive" />
              </span>
              LÄUFT
            </span>
          )}
          <span className="text-3xl font-black tracking-tight">{modeLabel(game.modus)}</span>
          <span className="text-2xl font-bold text-muted-foreground">Lauf {game.lauf}</span>
        </div>
        <UpdatedAgo iso={game.updatedAt} />
      </div>
      <LiveGameTable game={game} />
    </section>
  );
}

function UpdatedAgo({ iso }: { iso: string }) {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => { const t = setInterval(() => setNow(Date.now()), 1000); return () => clearInterval(t); }, []);
  const s = Math.max(0, Math.round((now - new Date(iso).getTime()) / 1000));
  return <span className="text-xl font-mono text-muted-foreground" data-testid="text-live-updated">aktualisiert vor {s} s</span>;
}

export function LiveGameTable({ game }: { game: LiveGame }) {
  const players = useMemo(() => [...game.players].sort((a, b) => a.posten - b.posten), [game.players]);
  const resultMap = useMemo(() => {
    const m = new Map<string, LiveResult>();
    for (const r of game.results) m.set(`${r.spielerId}-${r.lauf}-${r.taube}`, r);
    return m;
  }, [game.results]);
  const laeufe = useMemo(() => {
    const maxLauf = Math.max(game.lauf || 1, ...game.results.map((r) => r.lauf));
    return Array.from({ length: maxLauf }, (_, i) => i + 1);
  }, [game.lauf, game.results]);
  const tauben = Array.from({ length: Math.max(1, game.taubenProLauf) }, (_, i) => i + 1);

  return (
    <div className="overflow-x-auto overscroll-x-contain">
      <table className="scorecard-paper rounded-lg border-collapse w-max min-w-full" data-testid="table-live">
        <thead>
          <tr>
            <th className="slip-sticky slip-cell text-left px-3 text-lg font-bold uppercase tracking-widest text-[hsl(var(--slip-muted))] min-w-[13rem]">Name</th>
            {laeufe.map((l) => tauben.map((t) => {
              const current = l === game.lauf && t === game.currentTaube;
              return (
                <th key={`h-${l}-${t}`} className={cn("slip-cell text-center text-xl font-black w-[3.1rem] min-w-[3.1rem] py-1", t === 1 && l > 1 && "slip-run-start", current && "live-current-col")}>
                  {t}
                </th>
              );
            }))}
            <th className="slip-cell slip-total text-center text-lg font-black px-2 min-w-[5.5rem]">TOTAL</th>
          </tr>
        </thead>
        <tbody>
          {players.map((p) => {
            const isCurrent = p.spielerId === game.currentSpielerId;
            let total = 0;
            for (const r of game.results) if (r.spielerId === p.spielerId) total += r.punkte;
            return (
              <tr key={p.spielerId} className={cn(isCurrent && "live-current-row")} data-testid={`row-live-${p.spielerId}`}>
                <td className="slip-sticky slip-cell px-3 py-2.5 text-3xl font-semibold truncate max-w-[18rem]">
                  {isCurrent && <span className="inline-block mr-2 h-3 w-3 rounded-full bg-[hsl(var(--slip-two))] align-middle" aria-label="schießt gerade" />}
                  {p.name}
                </td>
                {laeufe.map((l) => tauben.map((t) => {
                  const r = resultMap.get(`${p.spielerId}-${l}-${t}`);
                  return (
                    <LiveCell key={`${p.spielerId}-${l}-${t}`} id={`${p.spielerId}-${l}-${t}`}
                      punkte={r?.punkte ?? null} maschine={r?.maschine ?? null}
                      runStart={t === 1 && l > 1} current={l === game.lauf && t === game.currentTaube} />
                  );
                }))}
                <td className="slip-cell slip-total text-center font-mono font-black text-4xl tabular-nums" data-testid={`text-live-total-${p.spielerId}`}>{total}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}

const LiveCell = memo(function LiveCell({ id, punkte, maschine, runStart, current }: {
  id: string; punkte: number | null; maschine: string | null; runStart: boolean; current: boolean;
}) {
  const prev = useRef(punkte);
  const [flash, setFlash] = useState(false);
  useEffect(() => {
    const fresh = prev.current === null && punkte !== null;
    prev.current = punkte;
    if (!fresh) return undefined;
    setFlash(true);
    const t = setTimeout(() => setFlash(false), 1400);
    return () => clearTimeout(t);
  }, [punkte]);
  return (
    <td data-testid={`cell-live-${id}`}
      className={cn(
        "slip-cell text-center font-mono font-bold tabular-nums w-[3.1rem] min-w-[3.1rem] leading-none py-1",
        runStart && "slip-run-start", current && "live-current-col", flash && "live-flash",
        punkte === 2 && "text-[hsl(var(--slip-ink))]",
        punkte === 1 && "text-[hsl(var(--slip-one))]",
        punkte === 0 && "text-[hsl(var(--slip-two))]",
      )}>
      {punkte === null ? "" : (
        <span className="flex flex-col items-center">
          <span className="text-[11px] font-sans font-bold text-[hsl(var(--slip-muted))]">{maschine}</span>
          <span className="text-3xl">{punkte}</span>
        </span>
      )}
    </td>
  );
});
