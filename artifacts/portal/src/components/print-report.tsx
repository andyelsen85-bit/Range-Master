import { useEffect, useLayoutEffect, useRef } from "react";
import type { SharedScorecard, SpielerStatistik } from "@workspace/api-client-react";
import { Scorecard } from "@/components/scorecard";
import { DayStatsBlock } from "@/components/kiosk/day-stats";
import { MonthTable } from "@/components/kiosk/month-table";
import { formatDay } from "@/lib/kiosk";
import { cn } from "@/lib/utils";

export interface ReportData {
  name: string; tag: string; club?: string; games: SharedScorecard[];
  stats: Omit<SpielerStatistik, "spielerId">; zeitraum?: "tag" | "jahr"; jahr?: number;
}

/** Current calendar year in Europe/Luxembourg. */
export function luxYear(): number {
  return Number(new Intl.DateTimeFormat("en", { timeZone: "Europe/Luxembourg", year: "numeric" }).format(new Date()));
}

const BASE_PT = 9;
const MIN_PT = 7;

/**
 * Print fit. Synchronously (no React state) puts the sheet into a measurement
 * mode that mirrors the print table rules at 9 pt with the sheet at the
 * printable width (190 mm portrait), reads table.scrollWidth vs. sheet.clientWidth,
 * then removes the mode before paint. Result: --print-scale (CSS turns it into
 * max(7pt, 9pt * scale), no transform/zoom so nothing shrinks twice) and
 * data-print-fit="landscape" only when even 7 pt cannot fit portrait.
 * Runs in useLayoutEffect, on beforeprint/afterprint and right before window.print().
 */
export function usePrintFit(dep: unknown) {
  const ref = useRef<HTMLElement | null>(null);
  const measure = () => {
    const el = ref.current;
    if (!el || !el.classList.contains("print-a4")) return;
    const fitAt = (mm: number) => {
      el.style.setProperty("--measure-width", `${mm}mm`);
      el.classList.add("print-measure");
      const avail = el.clientWidth;
      let scale=1;
      el.querySelectorAll<HTMLTableElement>("table.slip-table").forEach((table) => {
        // These widths do not shrink with the font. Reserve them before
        // fitting shot text, otherwise the global ratio hides cell overflow.
        let fixed=0;
        for(const cell of Array.from(table.tHead?.rows[0]?.cells??[])) {
          if(cell.matches(".slip-sticky,.slip-run-total,.slip-hit-total,.slip-zero-total,.slip-total"))
            fixed+=cell.getBoundingClientRect().width;
          else {
            const style=getComputedStyle(cell);
            fixed+=parseFloat(style.paddingLeft)+parseFloat(style.paddingRight)+parseFloat(style.borderLeftWidth);
          }
        }
        scale=Math.min(scale,Math.max(0,(avail-fixed)/Math.max(1,table.scrollWidth-fixed)));
      });
      el.classList.remove("print-measure");
      return scale;
    };
    let scale = fitAt(190);
    let landscape = false;
    if (BASE_PT * scale < MIN_PT) { landscape = true; scale = fitAt(277); }
    el.style.setProperty("--print-scale", scale.toFixed(4));
    el.dataset.printFit = landscape ? "landscape" : "portrait";
    el.dataset.printFont = `${Math.max(MIN_PT, BASE_PT * scale).toFixed(2)}pt`;
  };
  const measureRef = useRef(measure);
  measureRef.current = measure;
  useLayoutEffect(() => { measureRef.current(); }, [dep]);
  useEffect(() => {
    const h = () => measureRef.current();
    window.addEventListener("beforeprint", h);
    window.addEventListener("afterprint", h);
    window.addEventListener("resize", h);
    return () => { window.removeEventListener("beforeprint", h); window.removeEventListener("afterprint", h); window.removeEventListener("resize", h); };
  }, []);
  const print = () => { measureRef.current(); window.print(); };
  return { ref, print };
}

export function PrintReport({ data, format, fallbackTag, sheetRef }: {
  data: ReportData; format: "a4" | "bon"; fallbackTag: string;
  sheetRef: React.MutableRefObject<HTMLElement | null>;
}) {
  const printedAt = new Date().toLocaleString("de-LU", { day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit" });
  const yearly = data.zeitraum === "jahr";
  const club = data.club ?? "Rangemaster";
  const hasData = yearly ? (data.stats?.gesamtSpiele ?? 0) > 0 : true;
  return (
    <article ref={(n) => { sheetRef.current = n; }}
      className={cn("print-sheet", format === "bon" ? "print-receipt w-[80mm]" : "print-a4 w-full max-w-[210mm]", yearly && "print-annual")}
      data-testid="sheet-print" data-zeitraum={yearly ? "jahr" : "tag"}>
      <header className="flex flex-wrap items-end justify-between gap-2 border-b-2 border-[hsl(var(--slip-ink))] pb-2 mb-4">
        <div>
          <p className="text-xs uppercase tracking-[0.25em] font-bold" data-testid="text-print-club">{club}</p>
          <h1 className="text-2xl font-black" data-testid="text-print-name">{data.name}</h1>
        </div>
        <p className="text-lg font-bold font-mono" data-testid="text-print-day">
          {yearly ? `Gesamt Jahr ${data.jahr ?? ""}` : formatDay(data.tag ?? fallbackTag)}
        </p>
      </header>
      {!yearly && (
        <div className="space-y-4 mb-6">
          {data.games.length === 0 && <p className="italic">An diesem Tag gibt es keine sichtbaren Ergebnisse.</p>}
          {data.games.map((g, i) => (
            <div key={`${g.date}-${i}`} className="print-avoid-break print-game">
              <Scorecard game={g} />
            </div>
          ))}
        </div>
      )}
      {yearly && !hasData ? (
        <p className="py-10 text-center text-lg font-bold" data-testid="text-print-empty-year">Keine Spiele in diesem Jahr</p>
      ) : data.stats && (
        <>
          <DayStatsBlock stats={data.stats} title={yearly ? `Statistik ${data.jahr ?? ""}` : "Statistik des Tages"} annual={yearly} />
          {yearly && data.stats.monate && <MonthTable months={data.stats.monate} />}
        </>
      )}
      <footer className="mt-6 pt-2 border-t border-[hsl(var(--slip-ink)/0.5)] flex justify-between text-xs">
        <span>Gedruckt am {printedAt}</span>
        <span>{club}</span>
      </footer>
    </article>
  );
}

/** Day / year range selector shared by private and kiosk print screens. */
export function RangeSelect({ days, years, zeitraum, tag, jahr, onChange, large }: {
  days: string[]; years: number[]; zeitraum: "tag" | "jahr"; tag: string; jahr: number;
  onChange: (v: { zeitraum: "tag" | "jahr"; tag: string; jahr: number }) => void; large?: boolean;
}) {
  const h = large ? "h-16 text-xl px-5" : "h-10 text-sm px-3";
  return (
    <div className="flex flex-wrap items-center gap-2" data-testid="range-select">
      <div className="flex rounded-xl border border-border overflow-hidden">
        {(["tag", "jahr"] as const).map((z) => (
          <button key={z} type="button" data-testid={`button-range-${z}`} onClick={() => onChange({ zeitraum: z, tag, jahr })}
            className={cn(h, "font-bold", zeitraum === z ? "bg-primary/15 text-primary" : "text-muted-foreground")}>
            {z === "tag" ? "Tag" : "Gesamt Jahr"}
          </button>
        ))}
      </div>
      {zeitraum === "tag" ? (
        <select value={tag} onChange={(e) => onChange({ zeitraum, tag: e.target.value, jahr })} data-testid="select-print-day"
          className={cn(h, "rounded-xl border border-border bg-background font-mono font-bold")} disabled={days.length === 0}>
          {days.length === 0 && <option value={tag}>Keine Spieltage</option>}
          {days.map((d) => <option key={d} value={d}>{formatDay(d)}</option>)}
        </select>
      ) : (
        <select value={jahr} onChange={(e) => onChange({ zeitraum, tag, jahr: Number(e.target.value) })} data-testid="select-print-year"
          className={cn(h, "rounded-xl border border-border bg-background font-mono font-bold")}>
          {!years.includes(jahr) && <option value={jahr}>{jahr}</option>}
          {years.map((y) => <option key={y} value={y}>{y}</option>)}
        </select>
      )}
    </div>
  );
}
