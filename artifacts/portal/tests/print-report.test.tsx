import { describe, expect, it } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";
import { readFileSync } from "node:fs";
import { PrintReport, type ReportData } from "../src/components/print-report";

const stats = {
  gesamtSpiele: 1, gesamtSchuesse: 18, gesamtTreffer: 13,
  trefferquote: 72.2, bestPunkte: 26, durchschnitt: 26,
  anzahlSpieltage: 1, besterTag: "2026-10-13", besterTagPunkte: 26,
  maschinen: {}, tauben: {}, breakdown: [],
} as ReportData["stats"];
const columns = Array.from({ length: 18 }, (_, i) => ({
  run: Math.floor(i / 9) + 1, clay: i % 9 + 1, machine: "ABCDEFGHH"[i % 9],
}));
const game = {
  date: "2026-10-13T14:35:00Z", mode: "NORMAL", runs: 2, claysPerRun: 9,
  columns, players: [{ name: "Testspieler", cells: columns.map(() => 2), runTotals: [18, 18], total: 36 }],
} as ReportData["games"][number];
const data: ReportData = { name: "Testspieler", tag: "2026-10-13", games: [game], stats };
const render = (annual = false, format: "a4" | "bon" = "a4") =>
  renderToStaticMarkup(<PrintReport data={{ ...data, ...(annual ? { zeitraum: "jahr", jahr: 2026 } as const : {}) }}
    format={format} fallbackTag={data.tag} sheetRef={{ current: null }} />);

describe("print report formatting", () => {
  it("keeps both runs and every summary column in the daily report", () => {
    const doc = new DOMParser().parseFromString(render(), "text/html");
    const row = doc.querySelector('[data-testid="row-scorecard-player-0"]')!;
    expect(row.children).toHaveLength(25); // name + 18 shots + 2 runs + 4 summaries
    expect(row.lastElementChild?.textContent).toBe("36");
    expect(doc.querySelectorAll("thead th")).toHaveLength(25);
  });
  it("prints the best day as an unbroken date and a separate labelled total", () => {
    const doc = new DOMParser().parseFromString(render(true), "text/html");
    expect(doc.querySelector(".print-best-day-date")?.textContent).toBe("13.10.2026");
    expect(doc.querySelector(".print-best-day-points")?.textContent).toBe("26 Punkte");
    expect(doc.querySelectorAll(".print-stats-grid > div")).toHaveLength(8);
    expect(doc.querySelector(".slip-table")).toBeNull();
  });
  it("applies fitting before print media, without changing ordinary scorecards or receipts", () => {
    const css = readFileSync("src/index.css", "utf8");
    const preview = css.slice(0, css.indexOf("@media print"));
    expect(preview).toMatch(/\.print-a4 \.slip-table \{[^}]*table-layout: fixed/);
    expect(preview).toMatch(/\.print-a4 \.scorecard-paper \.overflow-x-auto \{ overflow: visible/);
    expect(preview).toMatch(/\.print-a4\.print-annual \.print-stats-grid \{ grid-template-columns: repeat\(4/);
    expect(render(false, "bon")).toContain("print-receipt");
    expect(render(false, "bon")).not.toContain("print-a4");
  });
});
