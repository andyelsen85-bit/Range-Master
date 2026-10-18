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
  const harakiri = game.modus === "HARAKIRI" || game.modus === "HARAKIRI_CUSTOM";
  const signature = (r: SavedResult) => {
    const next = at(r.spielerId, r.lauf, r.taube + 1), prev = at(r.spielerId, r.lauf, r.taube - 1);
    const role = r.posten >= 1 && r.posten <= 6
      ? next?.posten === r.posten ? 1 : prev?.posten === r.posten ? 2 : 0 : 0;
    return role * 8 + r.maschine.charCodeAt(0) - 65;
  };
  const machineKeys = new Map<number, number[]>();
  if (harakiri) for (let run = 1; run <= runs; run++)
    machineKeys.set(run, [...new Set(ledger.filter(r => r.lauf === run && !r.wiederholt && /^[A-H]$/.test(r.maschine)).map(signature))].sort((a,b) => a-b));
  const pairAt = (run: number, clay: number) => clay >= 1 && clay < 50 && ledger.some(r => {
    if (r.lauf !== run || r.taube !== clay) return false;
    const a = at(r.spielerId, run, clay), b = at(r.spielerId, run, clay + 1);
    return !!a && !!b && a.posten >= 1 && a.posten <= 6 && a.posten === b.posten;
  });
  const columns: Scorecard["columns"] = [];
  for (let run = 1; run <= runs; run++) for (let clay = 1; clay <= clays; clay++) {
    if (harakiri) {
      const key = machineKeys.get(run)?.[clay-1];
      const role = key === undefined ? 0 : Math.floor(key/8);
      const letter = key === undefined ? "-" : String.fromCharCode(65+key%8);
      columns.push({run, clay, machine: letter === "H" && role ? `H${role}` : letter,
        pair: role === 1 ? "start" : role === 2 ? "end" : null});
      continue;
    }
    const heading = ledger.find(r => r.lauf === run && r.taube === clay && /^[A-H]$/.test(r.maschine));
    columns.push({ run, clay, machine: heading?.maschine ?? "-", pair: pairAt(run, clay) ? "start" : pairAt(run, clay - 1) ? "end" : null });
  }
  return {
    date: new Date(game.datum).toISOString(), completedAt: game.completedAt ? new Date(game.completedAt).toISOString() : null,
    mode: game.modus, runs, claysPerRun: clays, columns,
    players: players.map(p => {
      const cells = columns.map(c => {
        if (!harakiri) return at(p.id, c.run, c.clay)?.punkte ?? null;
        const key = machineKeys.get(c.run)?.[c.clay-1];
        return ledger.filter(r => !r.wiederholt && r.spielerId === p.id && r.lauf === c.run && signature(r) === key).at(-1)?.punkte ?? null;
      });
      const runTotals = Array.from({ length: runs }, (_, i) => cells.slice(i * clays, (i + 1) * clays).reduce<number>((n, p) => n + (p ?? 0), 0));
      return { name: p.name, total: p.total ?? runTotals.reduce((n, p) => n + p, 0), runTotals, cells };
    }),
  };
}
export function publicName(name: string, mode: "FULL" | "INITIAL") {
  if (mode === "FULL") return name;
  const parts = name.trim().split(/\s+/); return parts.length > 1 ? `${parts[0]} ${parts.at(-1)![0]}.` : parts[0] ?? "";
}
