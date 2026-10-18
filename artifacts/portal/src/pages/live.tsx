import { useCallback, useEffect, useRef, useState } from "react";
import { useIsFetching } from "@tanstack/react-query";
import { Target, KeyRound } from "lucide-react";
import { useKioskToken } from "@/lib/kiosk";
import { LiveGamePanel } from "@/components/kiosk/live-game-panel";
import { LastGamePanel } from "@/components/kiosk/last-game-panel";
import { QueuePanel } from "@/components/kiosk/queue-panel";
import { asQueue, useKioskQueue } from "@/components/kiosk/types";
import { RankingCard } from "@/components/kiosk/ranking-card";
import { useGetKioskLiveGame, getGetKioskLiveGameQueryKey } from "@workspace/api-client-react";

const IDLE_MS = 60_000;

export default function Live() {
  const token = useKioskToken();
  if (!token) return <KioskAccessScreen />;
  return <LiveBoard />;
}

export function KioskAccessScreen() {
  return (
    <div className="kiosk-root min-h-[100dvh] bg-background text-foreground flex flex-col items-center justify-center gap-6 text-center p-8" data-testid="screen-kiosk-access">
      <div className="h-24 w-24 rounded-2xl bg-primary/15 text-primary flex items-center justify-center"><KeyRound size={52} /></div>
      <h1 className="text-5xl font-black tracking-tight">Zugriff nur über den Stand-PC</h1>
      <p className="text-2xl text-muted-foreground max-w-2xl">Diese Seite ist für den Touch-PC am Stand eingerichtet. Bitte wende dich an den Vorstand.</p>
    </div>
  );
}

/** Idle reset: 60 s without a touch clears every selection. */
export function useIdleReset(ms = IDLE_MS) {
  const [tick, setTick] = useState(0);
  useEffect(() => {
    let t = setTimeout(() => setTick((n) => n + 1), ms);
    const bump = () => {
      clearTimeout(t);
      t = setTimeout(() => setTick((n) => n + 1), ms);
    };
    window.addEventListener("pointerdown", bump, { passive: true });
    window.addEventListener("keydown", bump);
    return () => {
      clearTimeout(t);
      window.removeEventListener("pointerdown", bump);
      window.removeEventListener("keydown", bump);
    };
  }, [ms]);
  return tick;
}

function LiveBoard() {
  const resetSignal = useIdleReset();
  const queueQ = useKioskQueue(true);
  const settings = asQueue(queueQ.data)?.settings;
  const showQueue = !!settings?.queueEnabled;

  // Read-only subscription to the live query (shared cache, no extra request) to size the last game.
  const live = useGetKioskLiveGame({ query: { queryKey: getGetKioskLiveGameQueryKey(), enabled: false } });
  const running = !!live.data;

  const conn = useRef({ live: true, last: true });
  const [connOk, setConnOk] = useState(true);
  const onLiveConn = useCallback((ok: boolean) => { conn.current.live = ok; setConnOk(conn.current.live && conn.current.last); }, []);
  const onLastConn = useCallback((ok: boolean) => { conn.current.last = ok; setConnOk(conn.current.live && conn.current.last); }, []);
  const fetching = useIsFetching() > 0;

  const [now, setNow] = useState(() => new Date());
  useEffect(() => { const t = setInterval(() => setNow(new Date()), 1000); return () => clearInterval(t); }, []);

  return (
    <div className="kiosk-root h-[100dvh] max-xl:h-auto max-xl:min-h-[100dvh] bg-background text-foreground flex flex-col xl:overflow-hidden">
      <header className="shrink-0 flex items-center justify-between gap-6 px-8 h-20 border-b border-border/50">
        <div className="flex items-center gap-4">
          <div className="h-12 w-12 rounded-xl bg-primary/15 text-primary flex items-center justify-center"><Target size={28} /></div>
          <p className="text-3xl font-black tracking-tight">Rangemaster</p>
        </div>
        <div className="flex items-center gap-5">
          <span className="flex items-center gap-2 text-lg text-muted-foreground" data-testid="status-connection">
            <span className={`h-3 w-3 rounded-full ${connOk ? "bg-emerald-500" : "bg-destructive"} ${fetching ? "animate-pulse" : ""}`} />
            {connOk ? "verbunden" : "keine Verbindung"}
          </span>
          <span className="text-4xl font-black font-mono tabular-nums" data-testid="text-clock">
            {now.toLocaleTimeString("de-LU", { hour: "2-digit", minute: "2-digit" })}
          </span>
        </div>
      </header>

      <main className={`flex-1 min-h-0 grid gap-6 p-6 grid-cols-1 ${showQueue ? "xl:grid-cols-[58fr_42fr]" : ""}`}>
        <section className="min-h-0 xl:overflow-y-auto kiosk-scroll space-y-6 pr-1" aria-label="Live-Resultate" data-testid="column-results">
          <p className="text-base uppercase tracking-[0.3em] font-bold text-muted-foreground">Live-Resultate</p>
          <LiveGamePanel enabled onConnection={onLiveConn} />
          <LastGamePanel enabled compact={running} onConnection={onLastConn} />
          {settings?.showRanking && !running && <RankingCard />}
        </section>
        {showQueue && (
          <section className="min-h-0 max-xl:min-h-[900px]" aria-label="Warteschlange" data-testid="column-queue">
            <QueuePanel enabled resetSignal={resetSignal} />
          </section>
        )}
      </main>
    </div>
  );
}
