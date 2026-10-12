export interface SavedResult { spielerId: number; lauf: number; taube: number; maschine: string; posten: number; punkte: number; wiederholt: boolean }
export interface Scorecard {
  date: string; completedAt: string | null; mode: string; runs: number; claysPerRun: number;
  columns: { run: number; clay: number; machine: string; pair: "start" | "end" | null }[];
  players: { name: string; total: number; runTotals: number[]; cells: (number | null)[] }[];
}
export function buildScorecard(game: { datum: Date | string; completedAt?: Date | string | null; modus: string; lauf: number; taubenProLauf: number },
  players: { id: number; name: string; total?: number }[], results: SavedResult[]): Scorecard {
  const ledger = results.slice(0, 200);
  const clays = Math.min(50, Math.max(0, game.taubenProLauf, ...ledger.map(r => r.taube)));
  const runs = Math.min(clays ? Math.floor(200 / clays) : 1, Math.max(1, game.lauf, ...ledger.map(r => r.lauf)));
  const found = new Map<string, SavedResult>();
  for (const r of ledger) if (!r.wiederholt) found.set(`${r.spielerId}:${r.lauf}:${r.taube}`, r);
  const at = (id: number, run: number, clay: number) => found.get(`${id}:${run}:${clay}`);
  const pairAt = (run: number, clay: number) => clay >= 1 && clay < 50 && ledger.some(r => {
    if (r.lauf !== run || r.taube !== clay) return false;
    const a = at(r.spielerId, run, clay), b = at(r.spielerId, run, clay + 1);
    return !!a && !!b && a.posten >= 1 && a.posten <= 6 && a.posten === b.posten;
  });
  const columns: Scorecard["columns"] = [];
  for (let run = 1; run <= runs; run++) for (let clay = 1; clay <= clays; clay++) {
    const heading = ledger.find(r => r.lauf === run && r.taube === clay && /^[A-H]$/.test(r.maschine));
    columns.push({ run, clay, machine: heading?.maschine ?? "-", pair: pairAt(run, clay) ? "start" : pairAt(run, clay - 1) ? "end" : null });
  }
  return {
    date: new Date(game.datum).toISOString(), completedAt: game.completedAt ? new Date(game.completedAt).toISOString() : null,
    mode: game.modus, runs, claysPerRun: clays, columns,
    players: players.map(p => {
      const cells = columns.map(c => at(p.id, c.run, c.clay)?.punkte ?? null);
      const runTotals = Array.from({ length: runs }, (_, i) => cells.slice(i * clays, (i + 1) * clays).reduce<number>((n, p) => n + (p ?? 0), 0));
      return { name: p.name, total: p.total ?? runTotals.reduce((n, p) => n + p, 0), runTotals, cells };
    }),
  };
}
export function publicName(name: string, mode: "FULL" | "INITIAL") {
  if (mode === "FULL") return name;
  const parts = name.trim().split(/\s+/); return parts.length > 1 ? `${parts[0]} ${parts.at(-1)![0]}.` : parts[0] ?? "";
}
