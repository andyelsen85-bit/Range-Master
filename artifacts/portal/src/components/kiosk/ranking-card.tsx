import { useEffect } from "react";
import { keepPreviousData } from "@tanstack/react-query";
import { useGetKioskRanking, getGetKioskRankingQueryKey } from "@workspace/api-client-react";
import { Trophy } from "lucide-react";
import { reportKioskError } from "@/lib/kiosk";
import { cn } from "@/lib/utils";

interface RankEntry { rang: number; spielerId: number; name: string; bestPunkte: number; bestMaximum?: number | null }

/** Top 15 (best single result) on /live; only mounted when showRanking is on and no game runs. */
export function RankingCard() {
  const { data, isLoading, error } = useGetKioskRanking({
    query: { queryKey: getGetKioskRankingQueryKey(), refetchInterval: 30_000, placeholderData: keepPreviousData, retry: 1 },
  });
  useEffect(() => { if (error) reportKioskError(error); }, [error]);
  const rows = (((data as { rangliste?: RankEntry[] } | undefined)?.rangliste) ?? []).slice(0, 15);

  if (isLoading) return <div className="h-64 rounded-2xl bg-secondary/30 animate-pulse" />;
  if (rows.length === 0) return null;
  return (
    <section className="rounded-2xl border border-border/60 bg-card/50 p-5" data-testid="card-kiosk-ranking">
      <div className="flex items-baseline justify-between mb-3">
        <h2 className="text-3xl font-black tracking-tight flex items-center gap-3"><Trophy className="text-primary" size={28} /> Top 15</h2>
        <span className="text-lg text-muted-foreground">Bestes Einzelergebnis</span>
      </div>
      <ol className="grid grid-cols-2 gap-x-8">
        {rows.map((r) => (
          <li key={r.spielerId} className="flex items-center gap-4 py-2 border-b border-border/30 text-2xl" data-testid={`row-kiosk-rank-${r.spielerId}`}>
            <span className={cn("w-10 text-right font-mono font-black", r.rang <= 3 ? "text-primary" : "text-muted-foreground")}>{r.rang}</span>
            <span className="flex-1 truncate font-semibold">{r.name}</span>
            <span className="font-mono font-black tabular-nums">{r.bestPunkte}{r.bestMaximum ? <span className="text-muted-foreground text-lg"> / {r.bestMaximum}</span> : null}</span>
          </li>
        ))}
      </ol>
    </section>
  );
}
