import { useEffect, useState } from "react";
import { useGetPublicLastGame, getGetPublicLastGameQueryKey } from "@workspace/api-client-react";
import { Scorecard, formatGameDate, modeLabel } from "@/components/scorecard";
import { Target, EyeOff, WifiOff } from "lucide-react";

export default function Live() {
  const { data, isLoading, error, dataUpdatedAt, isFetching } = useGetPublicLastGame({
    query: {
      queryKey: getGetPublicLastGameQueryKey(),
      refetchInterval: 10_000,
      refetchIntervalInBackground: true,
      retry: (count, err) => (err as { status?: number })?.status !== 404 && count < 2,
    },
  });
  const [now, setNow] = useState(() => new Date());
  useEffect(() => { const t = setInterval(() => setNow(new Date()), 1000); return () => clearInterval(t); }, []);

  const status = (error as { status?: number } | null)?.status;
  const disabled = status === 404;
  const empty = !isLoading && !error && (!data || data.players.length === 0);

  return (
    <div className="min-h-[100dvh] bg-background text-foreground flex flex-col">
      <header className="flex items-center justify-between gap-6 px-6 md:px-12 py-6 border-b border-border/50">
        <div className="flex items-center gap-4">
          <div className="h-12 w-12 rounded-xl bg-primary/15 text-primary flex items-center justify-center"><Target size={28} /></div>
          <div>
            <p className="text-xs md:text-sm uppercase tracking-[0.3em] font-bold text-muted-foreground">Letztes Spiel</p>
            <p className="text-xl md:text-3xl font-black tracking-tight">Rangemaster</p>
          </div>
        </div>
        <div className="text-right">
          <p className="text-3xl md:text-5xl font-black font-mono tabular-nums">{now.toLocaleTimeString("de-LU", { hour: "2-digit", minute: "2-digit" })}</p>
          <p className="text-xs text-muted-foreground font-mono flex items-center justify-end gap-1.5 mt-1">
            <span className={`h-1.5 w-1.5 rounded-full ${error && !disabled ? "bg-destructive" : "bg-emerald-500"} ${isFetching ? "animate-pulse" : ""}`} />
            {dataUpdatedAt ? `aktualisiert ${new Date(dataUpdatedAt).toLocaleTimeString("de-LU")}` : "verbinde…"}
          </p>
        </div>
      </header>

      <main className="flex-1 px-4 md:px-12 py-8 md:py-12 flex flex-col">
        {isLoading ? (
          <div className="space-y-4">
            <div className="h-14 w-1/2 rounded-lg bg-secondary/30 animate-pulse" />
            <div className="h-80 w-full rounded-lg bg-secondary/30 animate-pulse" />
          </div>
        ) : disabled ? (
          <Message icon={<EyeOff size={56} />} title="Diese Seite ist nicht öffentlich" text="Der Verein hat die öffentliche Anzeige derzeit ausgeschaltet." />
        ) : error && !data ? (
          <Message icon={<WifiOff size={56} />} title="Keine Verbindung" text="Die Anzeige versucht es alle 10 Sekunden erneut." />
        ) : empty || !data ? (
          <Message icon={<Target size={56} />} title="Noch kein Spiel" text="Sobald ein Spiel abgeschlossen ist, erscheint es hier." />
        ) : (
          <div className="space-y-6 animate-in fade-in duration-700">
            <div className="flex flex-wrap items-end justify-between gap-4">
              <h1 className="text-4xl md:text-6xl font-black tracking-tight">{formatGameDate(data)}</h1>
              <span className="text-xl md:text-3xl font-bold uppercase tracking-widest text-primary">{modeLabel(data.mode)}</span>
            </div>
            <Scorecard game={data} size="tv" hideHeader />
            <p className="text-sm md:text-base text-muted-foreground font-mono">
              {data.runs} {data.runs === 1 ? "Lauf" : "Läufe"} · {data.claysPerRun} Tauben pro Lauf · grau hinterlegt = Doublé
            </p>
          </div>
        )}
      </main>
    </div>
  );
}

function Message({ icon, title, text }: { icon: React.ReactNode; title: string; text: string }) {
  return (
    <div className="flex-1 flex flex-col items-center justify-center text-center gap-5 py-20" data-testid="text-live-message">
      <div className="text-muted-foreground/50">{icon}</div>
      <h1 className="text-4xl md:text-6xl font-black tracking-tight">{title}</h1>
      <p className="text-lg md:text-2xl text-muted-foreground max-w-2xl">{text}</p>
    </div>
  );
}
