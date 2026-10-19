import { useMemo, useState } from "react";
import { Link, useSearch } from "wouter";
import { keepPreviousData } from "@tanstack/react-query";
import {
  useGetOwnerPrint, getGetOwnerPrintQueryKey, useGetOwnerPrintDays, getGetOwnerPrintDaysQueryKey,
} from "@workspace/api-client-react";
import { useAuthStore } from "@/store/use-auth-store";
import { PrintReport, RangeSelect, luxYear, usePrintFit, type ReportData } from "@/components/print-report";
import { apiErrorText } from "@/lib/kiosk";
import { ArrowLeft, Printer, RotateCcw } from "lucide-react";
import { cn } from "@/lib/utils";

/** Private, authenticated print of the logged-in player's own results (owner-scoped API only). */
export default function MeineDruck() {
  const user = useAuthStore((s) => s.user);
  const sp = new URLSearchParams(useSearch());
  const forId = Number(sp.get("spieler"));
  const id = forId > 0 ? forId : user?.id ?? 0;
  const [zeitraum, setZeitraum] = useState<"tag" | "jahr">(sp.get("zeitraum") === "jahr" ? "jahr" : "tag");
  const [tagSel, setTag] = useState<string | null>(sp.get("tag"));
  const [jahr, setJahr] = useState(Number(sp.get("jahr")) || luxYear());
  const [format, setFormat] = useState<"a4" | "bon">("a4");

  const daysQ = useGetOwnerPrintDays(id, { query: { queryKey: getGetOwnerPrintDaysQueryKey(id), enabled: id > 0, placeholderData: keepPreviousData } });
  const days = useMemo(() => daysQ.data?.days ?? [], [daysQ.data]);
  const years = daysQ.data?.years ?? [];
  const tag = tagSel ?? days[0] ?? "";
  const p = zeitraum === "jahr" ? { zeitraum, jahr } : { tag };
  const enabled = id > 0 && (zeitraum === "jahr" || !!tag);
  const q = useGetOwnerPrint(id, p, { query: { queryKey: getGetOwnerPrintQueryKey(id, p), enabled, retry: 1 } });
  const data = q.data as unknown as ReportData | undefined;
  const fit = usePrintFit([data, format]);
  const backHref = forId > 0 ? `/admin/spieler/${forId}` : "/resultater";

  return (
    <div className="min-h-[100dvh] bg-background text-foreground print:bg-transparent">
      <nav className="print:hidden sticky top-0 z-10 flex flex-wrap items-center gap-3 px-4 sm:px-6 py-3 border-b border-border/50 bg-background/95">
        <Link href={backHref} data-testid="button-owner-print-back" className="h-10 px-3 rounded-xl border border-border text-sm font-bold flex items-center gap-2">
          <ArrowLeft size={16} /> Zurück
        </Link>
        <RangeSelect days={days} years={years} zeitraum={zeitraum} tag={tag} jahr={jahr}
          onChange={(v) => { setZeitraum(v.zeitraum); setTag(v.tag); setJahr(v.jahr); }} />
        <div className="flex rounded-xl border border-border overflow-hidden ml-auto">
          {(["a4", "bon"] as const).map((f) => (
            <button key={f} type="button" onClick={() => setFormat(f)} data-testid={`button-format-${f}`}
              className={cn("h-10 px-3 text-sm font-bold", format === f ? "bg-primary/15 text-primary" : "text-muted-foreground")}>
              {f === "a4" ? "A4" : "Bon (80 mm)"}
            </button>
          ))}
        </div>
        <button type="button" disabled={!data} onClick={fit.print} data-testid="button-print-now"
          className="h-10 px-4 rounded-xl bg-primary text-primary-foreground text-sm font-black flex items-center gap-2 disabled:opacity-40">
          <Printer size={16} /> Drucken
        </button>
      </nav>
      <div className="p-4 sm:p-6 print:p-0 flex justify-center print-ancestor">
        {!enabled && zeitraum === "tag" && daysQ.isSuccess ? (
          <p className="py-24 text-center text-lg font-bold" data-testid="text-owner-print-empty">Noch keine Spieltage vorhanden.</p>
        ) : q.isLoading || daysQ.isLoading ? (
          <div className="w-full max-w-[210mm] h-[70vh] rounded bg-secondary/30 animate-pulse" />
        ) : q.isError || !data ? (
          <div className="text-center py-24 space-y-3" data-testid="text-print-error">
            <p className="text-2xl font-black">Ausdruck nicht verfügbar</p>
            <p className="text-muted-foreground">{q.isError ? apiErrorText(q.error) : "Keine Daten."}</p>
            <button type="button" onClick={() => q.refetch()} className="h-10 px-4 rounded-xl border border-border text-sm font-bold inline-flex items-center gap-2">
              <RotateCcw size={14} /> Erneut versuchen
            </button>
          </div>
        ) : (
          <PrintReport data={data} format={format} fallbackTag={tag} sheetRef={fit.ref} />
        )}
      </div>
    </div>
  );
}
