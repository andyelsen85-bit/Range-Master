import { useEffect, useState } from "react";
import { useLocation, useParams, useSearch } from "wouter";
import { useGetKioskPrint, getGetKioskPrintQueryKey } from "@workspace/api-client-react";
import type { SharedScorecard } from "@workspace/api-client-react";
import { Scorecard } from "@/components/scorecard";
import { DayStatsBlock } from "@/components/kiosk/day-stats";
import type { DayStats } from "@/components/kiosk/types";
import { apiErrorText, formatDay, reportKioskError, todayIso } from "@/lib/kiosk";
import { useIdleReset } from "@/pages/live";
import { KioskGate } from "@/components/kiosk/kiosk-gate";
import { ArrowLeft, Printer } from "lucide-react";
import { cn } from "@/lib/utils";

interface PrintData { name: string; tag: string; club?: string; games: SharedScorecard[]; stats: DayStats }

export default function LivePrint() {
  return <KioskGate><PrintView /></KioskGate>;
}

function PrintView() {
  const params = useParams<{ spielerId: string }>();
  const search = useSearch();
  const [, setLocation] = useLocation();
  const spielerId = Number(params.spielerId);
  const tag = new URLSearchParams(search).get("tag") || todayIso();
  const [format, setFormat] = useState<"a4" | "bon">("a4");

  const q = useGetKioskPrint(spielerId, { tag }, {
    query: { queryKey: getGetKioskPrintQueryKey(spielerId, { tag }), enabled: Number.isFinite(spielerId) && spielerId > 0, retry: 1, refetchInterval: (query) => (query.state.error ? 10_000 : false) },
  });
  useEffect(() => { if (q.error) reportKioskError(q.error); }, [q.error]);
  const data = q.data as unknown as PrintData | undefined;

  const back = () => setLocation("/live");
  useEffect(() => {
    const after = () => setLocation("/live");
    window.addEventListener("afterprint", after);
    return () => window.removeEventListener("afterprint", after);
  }, [setLocation]);
  const idle = useIdleReset();
  useEffect(() => { if (idle > 0) setLocation("/live"); }, [idle, setLocation]);

  const printedAt = new Date().toLocaleString("de-LU", { day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit" });

  return (
    <div className="kiosk-root min-h-[100dvh] bg-background text-foreground print:bg-transparent">
      <nav className="print:hidden sticky top-0 z-10 flex flex-wrap items-center gap-3 px-6 py-4 border-b border-border/50 bg-background/95">
        <button type="button" onClick={back} data-testid="button-print-back" className="kiosk-key h-16 px-6 rounded-xl text-xl font-bold flex items-center gap-2">
          <ArrowLeft size={26} /> Zurück
        </button>
        <div className="flex rounded-xl border border-border overflow-hidden ml-auto">
          {(["a4", "bon"] as const).map((f) => (
            <button key={f} type="button" onClick={() => setFormat(f)} data-testid={`button-format-${f}`}
              className={cn("h-16 px-6 text-xl font-bold", format === f ? "bg-primary/15 text-primary" : "text-muted-foreground")}>
              {f === "a4" ? "A4" : "Bon (80 mm)"}
            </button>
          ))}
        </div>
        <button type="button" disabled={!data} onClick={() => window.print()} data-testid="button-print-now"
          className="kiosk-primary h-16 px-8 rounded-xl text-2xl font-black flex items-center gap-3 disabled:opacity-40">
          <Printer size={28} /> Drucken
        </button>
      </nav>

      <div className="p-6 print:p-0 flex justify-center">
        {q.isLoading ? (
          <div className="w-full max-w-[210mm] h-[70vh] rounded bg-secondary/30 animate-pulse" />
        ) : q.isError || !data ? (
          <div className="text-center py-24 space-y-3" data-testid="text-print-error">
            <p className="text-4xl font-black">Ausdruck nicht verfügbar</p>
            <p className="text-2xl text-muted-foreground">{q.isError ? apiErrorText(q.error) : "Keine Daten."}</p>
            <button type="button" onClick={() => q.refetch()} className="kiosk-key h-16 px-6 rounded-xl text-xl font-bold mt-4">Erneut versuchen</button>
          </div>
        ) : (
          <article className={cn("print-sheet", format === "bon" ? "print-receipt w-[80mm]" : "w-full max-w-[210mm]")} data-testid="sheet-print">
            <header className="flex flex-wrap items-end justify-between gap-2 border-b-2 border-[hsl(var(--slip-ink))] pb-2 mb-4">
              <div>
                <p className="text-xs uppercase tracking-[0.25em] font-bold">{data.club ?? "Rangemaster"}</p>
                <h1 className="text-2xl font-black" data-testid="text-print-name">{data.name}</h1>
              </div>
              <p className="text-lg font-bold font-mono" data-testid="text-print-day">{formatDay(data.tag ?? tag)}</p>
            </header>
            <div className="space-y-4 mb-6">
              {data.games.length === 0 && <p className="italic">An diesem Tag gibt es keine sichtbaren Ergebnisse.</p>}
              {data.games.map((g, i) => (
                <div key={`${g.date}-${i}`} className="print-avoid-break">
                  <Scorecard game={g} />
                </div>
              ))}
            </div>
            {data.stats && <DayStatsBlock stats={data.stats} />}
            <footer className="mt-6 pt-2 border-t border-[hsl(var(--slip-ink)/0.5)] flex justify-between text-xs">
              <span>Gedruckt am {printedAt}</span>
              <span>{data.club ?? "Rangemaster"}</span>
            </footer>
          </article>
        )}
      </div>
    </div>
  );
}
