import { useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Skeleton } from "@/components/ui/skeleton";
import { Coins, Target } from "lucide-react";
import { cn } from "@/lib/utils";

// ── Types ──────────────────────────────────────────────────────────────────────

interface KreditRow {
  spielerId: number;
  name: string;
  mitgliedNr: string | null;
  gewaehrt: number;
  verbraucht: number;
}

interface JoerData {
  year: number;
  totalGewaehrt: number;
  totalVerbraucht: number;
  anzahlDagen: number;
  anzahlSpiller: number;
  byMonth: { monat: number; gewaehrt: number; verbraucht: number; dagen: number }[];
}

interface TaubenData {
  year: number;
  byMaschine: Record<string, number>;
  total: number;
}

// ── Helpers ────────────────────────────────────────────────────────────────────

function todayStr(): string {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

const MONTH_NAMES = ["Jan", "Feb", "Mär", "Apr", "Mee", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"];
const MASCHINEN = ["A", "B", "C", "D", "E", "F", "G", "H"] as const;

function StatCard({ label, value, sub }: { label: string; value: number | string; sub?: string }) {
  return (
    <div className="bg-card border border-border/50 rounded-xl p-5 shadow-sm">
      <p className="text-xs uppercase tracking-widest font-bold text-muted-foreground">{label}</p>
      <p className="text-3xl font-bold mt-1 font-mono">{value}</p>
      {sub && <p className="text-xs text-muted-foreground mt-1">{sub}</p>}
    </div>
  );
}

// ── Tab: Dag ──────────────────────────────────────────────────────────────────

function TabDag({ token }: { token: string | null }) {
  const [datum, setDatum] = useState(todayStr());

  const { data, isLoading } = useQuery<{ datum: string; kredite: KreditRow[] }>({
    queryKey: ["admin-kredite-dag", datum],
    queryFn: async () => {
      const res = await fetch(`/api/admin/kredite?datum=${datum}`, {
        headers: { Authorization: `Bearer ${token}` },
      });
      const json = await res.json();
      if (!res.ok) throw new Error(json.error || `HTTP ${res.status}`);
      return json;
    },
  });

  const rows = data?.kredite ?? [];
  const totalGewaehrt = rows.reduce((s, r) => s + r.gewaehrt, 0);
  const totalVerbraucht = rows.reduce((s, r) => s + r.verbraucht, 0);
  const totalRest = totalGewaehrt - totalVerbraucht;
  const isToday = datum === todayStr();

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between flex-wrap gap-4">
        <p className="text-sm text-muted-foreground font-medium">
          Virbezuelte Spiller pro Dag — um Terminal verwalt, hei nëmmen liesbar.
        </p>
        <input
          type="date"
          value={datum}
          onChange={(e) => e.target.value && setDatum(e.target.value)}
          className="bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 transition-colors"
        />
      </div>

      <div className="grid grid-cols-3 gap-4">
        <StatCard label="Kaaft" value={totalGewaehrt} />
        <StatCard label="Gespillt" value={totalVerbraucht} />
        <StatCard label={isToday ? "Nach oppen" : "Net benotzt"} value={totalRest} />
      </div>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-3">{[1, 2, 3].map(i => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}</div>
        ) : rows.length === 0 ? (
          <div className="p-12 text-center text-muted-foreground">
            <Coins size={32} className="mx-auto mb-3 opacity-30" />
            <p className="text-sm font-medium">Keng Kreditten fir den {datum}.</p>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Numm</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Membernummer</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Kaaft</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Gespillt</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Rescht</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {rows.map((r) => {
                  const rest = r.gewaehrt - r.verbraucht;
                  return (
                    <TableRow key={r.spielerId} className="border-border/30 hover:bg-secondary/20 transition-colors">
                      <TableCell className="font-bold text-foreground">{r.name}</TableCell>
                      <TableCell className="text-muted-foreground text-sm font-mono">{r.mitgliedNr || <span className="opacity-30">–</span>}</TableCell>
                      <TableCell className="text-right font-mono">{r.gewaehrt}</TableCell>
                      <TableCell className="text-right font-mono text-muted-foreground">{r.verbraucht}</TableCell>
                      <TableCell className="text-right font-mono font-bold">
                        {rest > 0 ? <span className="text-primary">{rest}</span> : <span className="text-muted-foreground/40">0</span>}
                      </TableCell>
                    </TableRow>
                  );
                })}
              </TableBody>
            </Table>
          </div>
        )}
      </div>
    </div>
  );
}

// ── Tab: Joer ─────────────────────────────────────────────────────────────────

function TabJoer({ token }: { token: string | null }) {
  const currentYear = new Date().getFullYear();
  const [year, setYear] = useState(currentYear);
  const years = Array.from({ length: 5 }, (_, i) => currentYear - i);

  const { data, isLoading } = useQuery<JoerData>({
    queryKey: ["admin-kredite-joer", year],
    queryFn: async () => {
      const res = await fetch(`/api/admin/kredite/joer?year=${year}`, {
        headers: { Authorization: `Bearer ${token}` },
      });
      const json = await res.json();
      if (!res.ok) throw new Error(json.error || `HTTP ${res.status}`);
      return json;
    },
  });

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between flex-wrap gap-4">
        <p className="text-sm text-muted-foreground font-medium">
          Joeregesamt vun alle Kreditten déi kaaft goufen — aggregéiert iwwer all Schéissdeeg.
        </p>
        <div className="flex gap-2">
          {years.map(y => (
            <button
              key={y}
              onClick={() => setYear(y)}
              className={cn(
                "px-4 py-2 rounded-lg text-sm font-bold border transition-colors",
                y === year
                  ? "bg-primary text-primary-foreground border-primary"
                  : "bg-background border-border/60 text-muted-foreground hover:border-primary/40 hover:text-foreground"
              )}
            >
              {y}
            </button>
          ))}
        </div>
      </div>

      {isLoading ? (
        <div className="grid grid-cols-4 gap-4">{[1,2,3,4].map(i => <Skeleton key={i} className="h-24 rounded-xl bg-secondary/30" />)}</div>
      ) : (
        <>
          <div className="grid grid-cols-4 gap-4">
            <StatCard label="Total Kaaft" value={data?.totalGewaehrt ?? 0} sub="Kreditten" />
            <StatCard label="Total Gespillt" value={data?.totalVerbraucht ?? 0} sub="Kreditten" />
            <StatCard label="Schéissdeeg" value={data?.anzahlDagen ?? 0} sub="Deeg" />
            <StatCard label="Verschidde Schützen" value={data?.anzahlSpiller ?? 0} sub="Spiller" />
          </div>

          <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
            <div className="px-6 py-4 border-b border-border/50 bg-secondary/10">
              <h3 className="font-bold text-sm uppercase tracking-widest text-muted-foreground">Pro Mounts {year}</h3>
            </div>
            {!data || data.byMonth.length === 0 ? (
              <div className="p-12 text-center text-muted-foreground">
                <Coins size={28} className="mx-auto mb-3 opacity-30" />
                <p className="text-sm font-medium">Keng Donnéeën fir {year}.</p>
              </div>
            ) : (
              <div className="overflow-x-auto">
                <Table>
                  <TableHeader className="bg-secondary/20">
                    <TableRow className="border-border/50 hover:bg-transparent">
                      <TableHead className="text-xs uppercase tracking-widest font-bold">Mount</TableHead>
                      <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Kaaft</TableHead>
                      <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Gespillt</TableHead>
                      <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Schéissdeeg</TableHead>
                    </TableRow>
                  </TableHeader>
                  <TableBody>
                    {data.byMonth.map(m => (
                      <TableRow key={m.monat} className="border-border/30 hover:bg-secondary/20 transition-colors">
                        <TableCell className="font-bold text-foreground">{MONTH_NAMES[m.monat - 1]}</TableCell>
                        <TableCell className="text-right font-mono">{m.gewaehrt}</TableCell>
                        <TableCell className="text-right font-mono text-muted-foreground">{m.verbraucht}</TableCell>
                        <TableCell className="text-right font-mono text-muted-foreground">{m.dagen}</TableCell>
                      </TableRow>
                    ))}
                    <TableRow className="border-border/50 bg-secondary/20 font-bold">
                      <TableCell className="font-black">Total</TableCell>
                      <TableCell className="text-right font-black font-mono text-primary">{data.totalGewaehrt}</TableCell>
                      <TableCell className="text-right font-black font-mono">{data.totalVerbraucht}</TableCell>
                      <TableCell className="text-right font-black font-mono">{data.anzahlDagen}</TableCell>
                    </TableRow>
                  </TableBody>
                </Table>
              </div>
            )}
          </div>
        </>
      )}
    </div>
  );
}

// ── Tab: Tauben ───────────────────────────────────────────────────────────────

function TabTauben({ token }: { token: string | null }) {
  const currentYear = new Date().getFullYear();
  const [year, setYear] = useState(currentYear);
  const years = Array.from({ length: 5 }, (_, i) => currentYear - i);

  const { data, isLoading } = useQuery<TaubenData>({
    queryKey: ["admin-kredite-tauben", year],
    queryFn: async () => {
      const res = await fetch(`/api/admin/kredite/tauben?year=${year}`, {
        headers: { Authorization: `Bearer ${token}` },
      });
      const json = await res.json();
      if (!res.ok) throw new Error(json.error || `HTTP ${res.status}`);
      return json;
    },
  });

  const byMaschine = data?.byMaschine ?? {};

  // A-G = 1 clay per row, H = stored as 2 rows per doublet (each row = 1 individual clay)
  const aGTotal = MASCHINEN.filter(m => m !== "H").reduce((s, m) => s + (byMaschine[m] ?? 0), 0);
  const hTotal = byMaschine["H"] ?? 0;
  const hDoubletten = Math.floor(hTotal / 2); // doublet cycles (H1+H2 pairs)

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between flex-wrap gap-4">
        <p className="text-sm text-muted-foreground font-medium">
          Ausgeléiste Tauben baséiert op den Spillresultater — A–G = 1 Taube, H = 2 Tauben (H1+H2).
        </p>
        <div className="flex gap-2">
          {years.map(y => (
            <button
              key={y}
              onClick={() => setYear(y)}
              className={cn(
                "px-4 py-2 rounded-lg text-sm font-bold border transition-colors",
                y === year
                  ? "bg-primary text-primary-foreground border-primary"
                  : "bg-background border-border/60 text-muted-foreground hover:border-primary/40 hover:text-foreground"
              )}
            >
              {y}
            </button>
          ))}
        </div>
      </div>

      {isLoading ? (
        <div className="grid grid-cols-4 gap-4">{[1,2,3,4].map(i => <Skeleton key={i} className="h-24 rounded-xl bg-secondary/30" />)}</div>
      ) : (
        <>
          <div className="grid grid-cols-3 gap-4">
            <StatCard label="Total Tauben" value={data?.total ?? 0} sub="eenzel Tauben ausgeléist" />
            <StatCard label="Maschinnen A–G" value={aGTotal} sub="eenzel Tauben" />
            <StatCard label="Doubletten H" value={`${hTotal} (${hDoubletten}×)`} sub="eenzel Tauben (Doublette-Zyklen)" />
          </div>

          <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
            <div className="px-6 py-4 border-b border-border/50 bg-secondary/10">
              <h3 className="font-bold text-sm uppercase tracking-widest text-muted-foreground">Pro Maschinn {year}</h3>
            </div>
            {!data || data.total === 0 ? (
              <div className="p-12 text-center text-muted-foreground">
                <Target size={28} className="mx-auto mb-3 opacity-30" />
                <p className="text-sm font-medium">Keng Resultater fir {year} fonnt.</p>
              </div>
            ) : (
              <div className="overflow-x-auto">
                <Table>
                  <TableHeader className="bg-secondary/20">
                    <TableRow className="border-border/50 hover:bg-transparent">
                      <TableHead className="text-xs uppercase tracking-widest font-bold">Maschinn</TableHead>
                      <TableHead className="text-xs uppercase tracking-widest font-bold">Typ</TableHead>
                      <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Tauben ausgeléist</TableHead>
                      <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Anteel</TableHead>
                    </TableRow>
                  </TableHeader>
                  <TableBody>
                    {MASCHINEN.map(m => {
                      const n = byMaschine[m] ?? 0;
                      const total = data?.total ?? 1;
                      const pct = total > 0 ? Math.round((n / total) * 100) : 0;
                      return (
                        <TableRow key={m} className="border-border/30 hover:bg-secondary/20 transition-colors">
                          <TableCell>
                            <span className={cn(
                              "inline-flex items-center justify-center w-8 h-8 rounded-lg font-black text-sm",
                              m === "H"
                                ? "bg-amber-500/20 text-amber-400 border border-amber-500/40"
                                : "bg-primary/10 text-primary border border-primary/30"
                            )}>
                              {m}
                            </span>
                          </TableCell>
                          <TableCell className="text-muted-foreground text-sm">
                            {m === "H" ? "Doublette (H1 + H2)" : "Eenzel Taube"}
                          </TableCell>
                          <TableCell className="text-right font-mono font-bold text-foreground">{n}</TableCell>
                          <TableCell className="text-right">
                            <div className="flex items-center justify-end gap-2">
                              <div className="w-20 h-1.5 bg-secondary/40 rounded-full overflow-hidden">
                                <div
                                  className={cn("h-full rounded-full", m === "H" ? "bg-amber-500" : "bg-primary")}
                                  style={{ width: `${pct}%` }}
                                />
                              </div>
                              <span className="text-xs font-mono text-muted-foreground w-8 text-right">{pct}%</span>
                            </div>
                          </TableCell>
                        </TableRow>
                      );
                    })}
                    <TableRow className="border-border/50 bg-secondary/20">
                      <TableCell colSpan={2} className="font-black">Total</TableCell>
                      <TableCell className="text-right font-black font-mono text-primary">{data?.total ?? 0}</TableCell>
                      <TableCell className="text-right text-xs font-mono text-muted-foreground">100%</TableCell>
                    </TableRow>
                  </TableBody>
                </Table>
              </div>
            )}
          </div>
        </>
      )}
    </div>
  );
}

// ── Page ──────────────────────────────────────────────────────────────────────

type TabId = "dag" | "joer" | "tauben";

const TABS: { id: TabId; label: string }[] = [
  { id: "dag",    label: "Dag" },
  { id: "joer",   label: "Joer" },
  { id: "tauben", label: "Ausgeléist Tauben" },
];

export default function AdminKredite() {
  const token = useAuthStore((s) => s.token);
  const [activeTab, setActiveTab] = useState<TabId>("dag");

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight">Kreditter</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">
          Dagesiwwersicht, Joereszesummefassung an ausgeléist Tauben.
        </p>
      </header>

      {/* Tabs */}
      <div className="flex gap-1 bg-secondary/20 rounded-xl p-1 w-fit border border-border/40">
        {TABS.map(t => (
          <button
            key={t.id}
            onClick={() => setActiveTab(t.id)}
            className={cn(
              "px-5 py-2 rounded-lg text-sm font-bold transition-colors",
              activeTab === t.id
                ? "bg-card text-foreground shadow-sm border border-border/50"
                : "text-muted-foreground hover:text-foreground"
            )}
          >
            {t.label}
          </button>
        ))}
      </div>

      {activeTab === "dag"    && <TabDag    token={token} />}
      {activeTab === "joer"   && <TabJoer   token={token} />}
      {activeTab === "tauben" && <TabTauben token={token} />}
    </div>
  );
}
