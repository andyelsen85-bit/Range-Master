import { useEffect, useState } from "react";
import { useLocation, useParams, useSearch } from "wouter";
import { keepPreviousData } from "@tanstack/react-query";
import {
  useGetKioskPrint, getGetKioskPrintQueryKey, useGetKioskPrintDays, getGetKioskPrintDaysQueryKey,
} from "@workspace/api-client-react";
import { apiErrorText, reportKioskError, todayIso } from "@/lib/kiosk";
import { useIdleReset } from "@/pages/live";
import { KioskGate } from "@/components/kiosk/kiosk-gate";
import { PrintReport, RangeSelect, luxYear, usePrintFit, type ReportData } from "@/components/print-report";
import { ArrowLeft, Printer } from "lucide-react";
import { cn } from "@/lib/utils";

export default function LivePrint() {
  return <KioskGate><PrintView /></KioskGate>;
}

function PrintView() {
  const params = useParams<{ spielerId: string }>();
  const search = useSearch();
  const [, setLocation] = useLocation();
  const spielerId = Number(params.spielerId);
  const sp = new URLSearchParams(search);
  const [zeitraum, setZeitraum] = useState<"tag" | "jahr">(sp.get("zeitraum") === "jahr" ? "jahr" : "tag");
  const [tag, setTag] = useState(sp.get("tag") || todayIso());
  const [jahr, setJahr] = useState(Number(sp.get("jahr")) || luxYear());
  const [format, setFormat] = useState<"a4" | "bon">("a4");
  const ok = Number.isFinite(spielerId) && spielerId > 0;

  const daysQ = useGetKioskPrintDays(spielerId, { query: { queryKey: getGetKioskPrintDaysQueryKey(spielerId), enabled: ok, placeholderData: keepPreviousData } });
  const days = daysQ.data?.days ?? [];
  const years = daysQ.data?.years ?? [];
  const p = zeitraum === "jahr" ? { zeitraum, jahr } : { tag };
  const q = useGetKioskPrint(spielerId, p, {
    query: { queryKey: getGetKioskPrintQueryKey(spielerId, p), enabled: ok, retry: 1, refetchInterval: (query) => (query.state.error ? 10_000 : false) },
  });
  useEffect(() => { if (q.error) reportKioskError(q.error); }, [q.error]);
  const data = q.data as unknown as ReportData | undefined;
  const fit = usePrintFit([data, format]);

  const back = () => setLocation("/live");
  useEffect(() => {
    const after = () => setLocation("/live");
    window.addEventListener("afterprint", after);
    return () => window.removeEventListener("afterprint", after);
  }, [setLocation]);
  const idle = useIdleReset();
  useEffect(() => { if (idle > 0) setLocation("/live"); }, [idle, setLocation]);

  return (
    <div className="kiosk-root min-h-[100dvh] bg-background text-foreground print:bg-transparent">
      <nav className="print:hidden sticky top-0 z-10 flex flex-wrap items-center gap-3 px-6 py-4 border-b border-border/50 bg-background/95">
        <button type="button" onClick={back} data-testid="button-print-back" className="kiosk-key h-16 px-6 rounded-xl text-xl font-bold flex items-center gap-2">
          <ArrowLeft size={26} /> Zurück
        </button>
        <RangeSelect large days={days} years={years} zeitraum={zeitraum} tag={tag} jahr={jahr}
          onChange={(v) => { setZeitraum(v.zeitraum); setTag(v.tag); setJahr(v.jahr); }} />
        <div className="flex rounded-xl border border-border overflow-hidden ml-auto">
          {(["a4", "bon"] as const).map((f) => (
            <button key={f} type="button" onClick={() => setFormat(f)} data-testid={`button-format-${f}`}
              className={cn("h-16 px-6 text-xl font-bold", format === f ? "bg-primary/15 text-primary" : "text-muted-foreground")}>
              {f === "a4" ? "A4" : "Bon (80 mm)"}
            </button>
          ))}
        </div>
        <button type="button" disabled={!data} onClick={fit.print} data-testid="button-print-now"
          className="kiosk-primary h-16 px-8 rounded-xl text-2xl font-black flex items-center gap-3 disabled:opacity-40">
          <Printer size={28} /> Drucken
        </button>
      </nav>

      <div className="p-6 print:p-0 flex justify-center print-ancestor">
        {q.isLoading ? (
          <div className="w-full max-w-[210mm] h-[70vh] rounded bg-secondary/30 animate-pulse" />
        ) : q.isError || !data ? (
          <div className="text-center py-24 space-y-3" data-testid="text-print-error">
            <p className="text-4xl font-black">Ausdruck nicht verfügbar</p>
            <p className="text-2xl text-muted-foreground">{q.isError ? apiErrorText(q.error) : "Keine Daten."}</p>
            <button type="button" onClick={() => q.refetch()} className="kiosk-key h-16 px-6 rounded-xl text-xl font-bold mt-4">Erneut versuchen</button>
          </div>
        ) : (
          <PrintReport data={data} format={format} fallbackTag={tag} sheetRef={fit.ref} />
        )}
      </div>
    </div>
  );
}
