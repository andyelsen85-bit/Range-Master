import { useEffect, useRef } from "react";
import { keepPreviousData } from "@tanstack/react-query";
import { useGetPublicLastGame, getGetPublicLastGameQueryKey } from "@workspace/api-client-react";
import type { SharedScorecard } from "@workspace/api-client-react";
import { Scorecard, formatGameDate, modeLabel } from "@/components/scorecard";
import { reportKioskError } from "@/lib/kiosk";
import { EyeOff, Target, WifiOff } from "lucide-react";

export function gameSignature(g: SharedScorecard | undefined | null) {
  return g ? `${g.date}|${g.completedAt ?? ""}|${g.players.length}` : null;
}

export function LastGamePanel({ enabled, compact, onGame, onConnection }: {
  enabled: boolean;
  compact: boolean;
  onGame?: (signature: string | null) => void;
  onConnection?: (ok: boolean) => void;
}) {
  const { data, isLoading, error } = useGetPublicLastGame({
    query: {
      queryKey: getGetPublicLastGameQueryKey(),
      enabled,
      refetchInterval: 10_000,
      refetchIntervalInBackground: true,
      placeholderData: keepPreviousData,
      retry: (count, err) => {
        const s = (err as { status?: number })?.status;
        return s !== 404 && s !== 401 && s !== 403 && count < 2;
      },
    },
  });
  useEffect(() => { if (error) reportKioskError(error); }, [error]);
  const status = (error as { status?: number } | null)?.status;
  const disabled = status === 404;

  const cbRef = useRef({ onGame, onConnection });
  cbRef.current = { onGame, onConnection };
  const sig = gameSignature(data);
  useEffect(() => { cbRef.current.onGame?.(sig); }, [sig]);
  useEffect(() => { cbRef.current.onConnection?.(!error || disabled); }, [error, disabled]);

  if (isLoading) {
    return (
      <div className="space-y-4" data-testid="panel-last-game-loading">
        <div className="h-12 w-1/2 rounded-lg bg-secondary/30 animate-pulse" />
        <div className="h-72 w-full rounded-lg bg-secondary/30 animate-pulse" />
      </div>
    );
  }
  if (disabled) return <PanelMessage icon={<EyeOff size={48} />} title="Diese Seite ist nicht öffentlich" text="Der Verein hat die Anzeige derzeit ausgeschaltet." />;
  if (error && !data) return <PanelMessage icon={<WifiOff size={48} />} title="Keine Verbindung" text="Die Anzeige versucht es alle 10 Sekunden erneut." />;
  if (!data || data.players.length === 0) return <PanelMessage icon={<Target size={48} />} title="Noch kein Spiel" text="Sobald ein Spiel abgeschlossen ist, erscheint es hier." />;

  return (
    <section className="space-y-4" data-testid="panel-last-game">
      <div className="flex flex-wrap items-end justify-between gap-4">
        <div>
          <p className="text-base uppercase tracking-[0.3em] font-bold text-muted-foreground">Letztes Spiel</p>
          <h2 className={compact ? "text-3xl font-black tracking-tight" : "text-5xl font-black tracking-tight"}>{formatGameDate(data)}</h2>
        </div>
        <span className={compact ? "text-xl font-bold uppercase tracking-widest text-primary" : "text-3xl font-bold uppercase tracking-widest text-primary"}>{modeLabel(data.mode)}</span>
      </div>
      <Scorecard game={data} size={compact ? "default" : "tv"} hideHeader className={compact ? "kiosk-compact-card" : undefined} />
      {!compact && (
        <p className="text-xl text-muted-foreground font-mono">
          {data.runs} {data.runs === 1 ? "Lauf" : "Läufe"} · {data.claysPerRun} Tauben pro Lauf · grau hinterlegt = Doublé
        </p>
      )}
    </section>
  );
}

export function PanelMessage({ icon, title, text }: { icon: React.ReactNode; title: string; text: string }) {
  return (
    <div className="flex flex-col items-center justify-center text-center gap-4 py-16" data-testid="text-live-message">
      <div className="text-muted-foreground/50">{icon}</div>
      <h2 className="text-4xl font-black tracking-tight">{title}</h2>
      <p className="text-2xl text-muted-foreground max-w-xl">{text}</p>
    </div>
  );
}
