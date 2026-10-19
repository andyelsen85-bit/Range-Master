import type { SpielerStatistikMonateItem } from "@workspace/api-client-react";

const MONTHS = ["Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"];
const fmt = (n: number, d = 1) => (Number.isFinite(n) ? n.toLocaleString("de-LU", { maximumFractionDigits: d, minimumFractionDigits: d }) : "–");

/** Compact per-month table for the annual report, six numeric fields. */
export function MonthTable({ months }: { months: SpielerStatistikMonateItem[] }) {
  const rows = Array.from({ length: 12 }, (_, i) => months.find((m) => m.month === i + 1) ?? { month: i + 1, spiele: 0, schuesse: 0, treffer: 0, quote: 0, schnitt: 0 });
  return (
    <section className="mt-5 print-avoid-break" data-testid="section-month-table">
      <h3 className="font-bold text-sm uppercase tracking-widest mb-1">Pro Monat</h3>
      <table className="w-full text-sm border-collapse print-month-table">
        <thead>
          <tr className="border-b border-[hsl(var(--slip-ink))]">
            <th className="text-left py-1">Monat</th><th className="text-right">Spiele</th><th className="text-right">Schüsse</th>
            <th className="text-right">Treffer</th><th className="text-right">Quote</th><th className="text-right">Schnitt</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((r) => (
            <tr key={r.month} className="border-b border-[hsl(var(--slip-ink)/0.3)]" data-testid={`row-month-${r.month}`}>
              <td className="py-0.5">{MONTHS[r.month - 1]}</td>
              <td className="text-right font-mono tabular-nums">{r.spiele}</td>
              <td className="text-right font-mono tabular-nums">{r.schuesse}</td>
              <td className="text-right font-mono tabular-nums">{r.treffer}</td>
              <td className="text-right font-mono tabular-nums">{r.spiele ? `${fmt(r.quote)} %` : "–"}</td>
              <td className="text-right font-mono tabular-nums">{r.spiele ? fmt(r.schnitt) : "–"}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  );
}
