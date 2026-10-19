import { modeLabel } from "@/components/scorecard";
import { formatDay } from "@/lib/kiosk";
import type { DayStats, TaubeStat } from "./types";

const fmt = (n: number, d = 1) => (Number.isFinite(n) ? n.toLocaleString("de-LU", { maximumFractionDigits: d, minimumFractionDigits: d }) : "–");

/** Day statistics for the printout. Bars use a hatch pattern + labels so they print in black and white. */
export function DayStatsBlock({ stats, title = "Statistik des Tages", annual }: { stats: DayStats; title?: string; annual?: boolean }) {
  const zero: TaubeStat = { versuche: 0, treffer: 0, quote: 0 };
  const maschinen: [string, TaubeStat][] = annual
    ? "ABCDEFGH".split("").map((k) => [k, stats.maschinen?.[k] ?? zero])
    : Object.entries(stats.maschinen ?? {}).sort(([a], [b]) => a.localeCompare(b));
  const tauben: [string, TaubeStat][] = annual
    ? Array.from({ length: 9 }, (_, i) => [String(i + 1), stats.tauben?.[String(i + 1)] ?? zero])
    : Object.entries(stats.tauben ?? {}).sort(([a], [b]) => Number(a) - Number(b));
  const breakdown = stats.breakdown ?? [];
  return (
    <section className="space-y-5 print-avoid-break" data-testid="section-day-stats">
      <h2 className="text-xl font-black border-b-2 border-[hsl(var(--slip-ink))] pb-1" data-testid="text-stats-title">{title}</h2>
      <dl className="grid grid-cols-3 sm:grid-cols-6 gap-3 print-stats-grid">
        <Kpi label="Spiele" value={String(stats.gesamtSpiele)} />
        <Kpi label="Schüsse" value={String(stats.gesamtSchuesse)} />
        <Kpi label="Treffer" value={String(stats.gesamtTreffer)} />
        <Kpi label="Trefferquote" value={`${fmt(stats.trefferquote)} %`} />
        <Kpi label="Bestes Spiel" value={String(stats.bestPunkte)} />
        <Kpi label="Schnitt / Spiel" value={fmt(stats.durchschnitt)} />
        {annual && <Kpi label="Spieltage" value={String(stats.anzahlSpieltage ?? 0)} />}
        {annual && <Kpi label="Bester Tag" value={stats.besterTag ? `${formatDay(stats.besterTag)} · ${stats.besterTagPunkte ?? 0}` : "–"} />}
      </dl>
      <div className="grid sm:grid-cols-2 gap-6 print-two-col">
        <Bars title="Trefferquote pro Maschine" rows={maschinen.map(([k, v]) => [`Maschine ${k}`, v])} testid="bars-maschinen" />
        <Bars title="Trefferquote pro Taube" rows={tauben.map(([k, v]) => [`Taube ${k}`, v])} testid="bars-tauben" />
      </div>
      {breakdown.length > 1 && (
        <div>
          <h3 className="font-bold text-sm uppercase tracking-widest mb-1">Pro Modus</h3>
          <table className="w-full text-sm border-collapse">
            <thead><tr className="border-b border-[hsl(var(--slip-ink))]"><th className="text-left py-1">Modus</th><th className="text-right">Spiele</th><th className="text-right">Schnitt</th></tr></thead>
            <tbody>
              {breakdown.map((b) => (
                <tr key={b.modus} className="border-b border-[hsl(var(--slip-ink)/0.3)]">
                  <td className="py-1">{modeLabel(b.modus)}</td>
                  <td className="text-right font-mono">{b.anzahlSpiele}</td>
                  <td className="text-right font-mono">{fmt(b.durchschnitt)}{b.durchschnittProzent != null ? ` (${fmt(b.durchschnittProzent)} %)` : ""}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </section>
  );
}

function Kpi({ label, value }: { label: string; value: string }) {
  return (
    <div className="border border-[hsl(var(--slip-ink)/0.6)] rounded px-2 py-1.5">
      <dt className="text-[10px] uppercase tracking-widest font-bold text-[hsl(var(--slip-muted))]">{label}</dt>
      <dd className="text-xl font-black font-mono tabular-nums">{value}</dd>
    </div>
  );
}

function Bars({ title, rows, testid }: { title: string; rows: [string, TaubeStat][]; testid: string }) {
  return (
    <div data-testid={testid}>
      <h3 className="font-bold text-sm uppercase tracking-widest mb-2">{title}</h3>
      {rows.length === 0 ? <p className="text-sm italic">Keine Daten</p> : (
        <div className="space-y-1">
          {rows.map(([label, v]) => (
            <div key={label} className="grid grid-cols-[6.5rem_1fr_6.5rem] items-center gap-2 text-sm">
              <span className="font-semibold">{label}</span>
              <span className="h-4 border border-[hsl(var(--slip-ink))] relative">
                <span className="print-bar absolute inset-y-0 left-0" style={{ width: `${Math.max(0, Math.min(100, v.quote))}%` }} />
              </span>
              <span className="font-mono text-right tabular-nums">{fmt(v.quote, 0)} % <span className="opacity-70">{v.treffer}/{v.versuche}</span></span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
