import { useMemo } from "react";
import { useAuthStore } from "@/store/use-auth-store";
import {
  useGetSpielerErgebnisse, getGetSpielerErgebnisseQueryKey,
  useGetSharedScorecard, getGetSharedScorecardQueryKey,
} from "@workspace/api-client-react";
import { Skeleton } from "@/components/ui/skeleton";
import { AlertCircle, RotateCcw } from "lucide-react";
import { Scorecard, ScorecardSkeleton } from "@/components/scorecard";

interface ResultaterProps {
  spielerId?: number;
}

export default function Resultater({ spielerId }: ResultaterProps = {}) {
  const user = useAuthStore((s) => s.user);
  const effectiveId = spielerId ?? user?.id ?? 0;

  const { data, isLoading, isError, refetch } = useGetSpielerErgebnisse(effectiveId, {
    query: { enabled: !!effectiveId, queryKey: getGetSpielerErgebnisseQueryKey(effectiveId) },
  });

  // Distinct games, newest first — each rendered as one shared scorecard
  const spielIds = useMemo(() => {
    const latest = new Map<number, number>();
    for (const e of data?.ergebnisse ?? []) {
      const t = new Date(e.spiel.datum).getTime();
      if (!latest.has(e.spielId) || t > latest.get(e.spielId)!) latest.set(e.spielId, t);
    }
    return [...latest.entries()].sort((a, b) => b[1] - a[1]).map(([id]) => id);
  }, [data]);

  return (
    <div className="space-y-8 animate-in fade-in duration-500">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight">Meine Ergebnisse</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">Jedes Spiel als Schießzettel – wie auf Papier.</p>
      </header>

      {isLoading ? (
        <div className="space-y-6">
          {[1, 2, 3].map((i) => <Skeleton key={i} className="h-40 w-full rounded-xl bg-card border border-border/50" />)}
        </div>
      ) : isError ? (
        <ErrorBox text="Ergebnisse konnten nicht geladen werden." onRetry={() => refetch()} />
      ) : spielIds.length > 0 ? (
        <div className="space-y-6">
          {spielIds.map((id) => <GameScorecard key={id} spielId={id} />)}
        </div>
      ) : (
        <div className="p-12 text-center bg-card rounded-xl border border-border/50">
          <AlertCircle className="mx-auto h-12 w-12 text-muted-foreground mb-4 opacity-50" />
          <h3 className="text-lg font-bold mb-1">Noch keine Ergebnisse</h3>
          <p className="text-muted-foreground font-medium">In dieser Saison wurden noch keine Spiele abgeschlossen.</p>
        </div>
      )}
    </div>
  );
}

function GameScorecard({ spielId }: { spielId: number }) {
  const { data, isLoading, isError, refetch } = useGetSharedScorecard(spielId, {
    query: { enabled: !!spielId, queryKey: getGetSharedScorecardQueryKey(spielId) },
  });
  if (isLoading) return <ScorecardSkeleton />;
  if (isError || !data) return <ErrorBox text={`Spiel #${spielId} konnte nicht geladen werden.`} onRetry={() => refetch()} />;
  return <Scorecard game={data} />;
}

function ErrorBox({ text, onRetry }: { text: string; onRetry: () => void }) {
  return (
    <div className="p-6 flex items-center justify-between gap-4 bg-card rounded-xl border border-destructive/30 text-sm">
      <span className="flex items-center gap-2 text-destructive font-medium"><AlertCircle size={16} /> {text}</span>
      <button onClick={onRetry} data-testid="button-retry"
        className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg border border-border/60 text-xs font-bold hover:bg-secondary/40">
        <RotateCcw size={13} /> Erneut versuchen
      </button>
    </div>
  );
}
