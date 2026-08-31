import { useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Skeleton } from "@/components/ui/skeleton";
import { Coins, Target, Plus, Minus, RotateCcw, Loader2 } from "lucide-react";
import { cn } from "@/lib/utils";
import { useToast } from "@/hooks/use-toast";
import { useListAdminProducts, useGetAdminDaySales, getGetAdminDaySalesQueryKey } from "@workspace/api-client-react";

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

const MONTH_NAMES = ["Jan", "Feb", "Mär", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"];
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

type OpKey = string;
type OpState = { externalId: string; status: "pending" | "failed" };

function TabDag({ token }: { token: string | null }) {
  const [datum, setDatum] = useState(todayStr());
  const qc = useQueryClient();
  const { toast } = useToast();

  const [ops, setOps] = useState<Record<OpKey, OpState>>({});

  const { data: creditsData, isLoading: creditsLoading } = useQuery<{ datum: string; kredite: KreditRow[] }>({
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

  const { data: salesData, isLoading: salesLoading } = useGetAdminDaySales(
    { datum },
    { query: { enabled: !!datum, queryKey: getGetAdminDaySalesQueryKey({ datum }) } }
  );

  const { data: productsData, isLoading: productsLoading } = useListAdminProducts();

  const adjustCredit = useMutation({
    mutationFn: async ({ spielerId, delta, externalId }: { spielerId: number; delta: 1 | -1; externalId: string; opKey: string }) => {
      const res = await fetch(`/api/admin/kredite/adjust`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${token}`
        },
        body: JSON.stringify({
          spielerId,
          datum,
          delta,
          externalId
        })
      });
      const json = await res.json();
      if (!res.ok) {
        const err = new Error(json.error || `HTTP ${res.status}`);
        (err as any).status = res.status;
        throw err;
      }
      return json;
    },
    onSuccess: (data, variables) => {
      qc.invalidateQueries({ queryKey: ["admin-kredite-dag", datum] });
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
      toast({ title: "Erfolgreich gespeichert", description: "Die Guthaben wurden angepasst." });
      setOps(prev => { const next = { ...prev }; delete next[variables.opKey]; return next; });
    },
    onError: (err: any, variables) => {
      toast({ title: "Fehler", description: err.message, variant: "destructive" });
      const status = err.status;
      if (status && status >= 400 && status < 500 && status !== 408 && status !== 429) {
        setOps(prev => { const next = { ...prev }; delete next[variables.opKey]; return next; });
      } else {
        setOps(prev => ({ ...prev, [variables.opKey]: { ...prev[variables.opKey], status: "failed" } }));
      }
    }
  });

  const adjustAmmo = useMutation({
    mutationFn: async ({ spielerId, productId, delta, externalId }: { spielerId: number; productId: number; delta: 1 | -1; externalId: string; opKey: string }) => {
      const res = await fetch(`/api/admin/ammo/adjust`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${token}`
        },
        body: JSON.stringify({
          spielerId,
          datum,
          productId,
          delta,
          externalId
        })
      });
      const json = await res.json();
      if (!res.ok) {
        const err = new Error(json.error || `HTTP ${res.status}`);
        (err as any).status = res.status;
        throw err;
      }
      return json;
    },
    onSuccess: (data, variables) => {
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
      toast({ title: "Erfolgreich gespeichert", description: "Die Munition wurde angepasst." });
      setOps(prev => { const next = { ...prev }; delete next[variables.opKey]; return next; });
    },
    onError: (err: any, variables) => {
      toast({ title: "Fehler", description: err.message, variant: "destructive" });
      const status = err.status;
      if (status && status >= 400 && status < 500 && status !== 408 && status !== 429) {
        setOps(prev => { const next = { ...prev }; delete next[variables.opKey]; return next; });
      } else {
        setOps(prev => ({ ...prev, [variables.opKey]: { ...prev[variables.opKey], status: "failed" } }));
      }
    }
  });

  const handleAdjustCredit = (spielerId: number, delta: 1 | -1) => {
    const opKey = `${spielerId}-credit-${delta}`;
    let externalId = ops[opKey]?.externalId;
    if (!externalId || ops[opKey]?.status !== "failed") {
      externalId = `portal-${Date.now()}-${Math.random().toString(36).substring(2, 9)}`;
    }
    setOps(prev => ({ ...prev, [opKey]: { externalId, status: "pending" } }));
    adjustCredit.mutate({ spielerId, delta, externalId, opKey });
  };

  const handleAdjustAmmo = (spielerId: number, productId: number, delta: 1 | -1, ammoType: '12' | '20') => {
    const opKey = `${spielerId}-ammo${ammoType}-${delta}`;
    let externalId = ops[opKey]?.externalId;
    if (!externalId || ops[opKey]?.status !== "failed") {
      externalId = `portal-${Date.now()}-${Math.random().toString(36).substring(2, 9)}`;
    }
    setOps(prev => ({ ...prev, [opKey]: { externalId, status: "pending" } }));
    adjustAmmo.mutate({ spielerId, productId, delta, externalId, opKey });
  };

  const renderIcon = (opKey: string, DefaultIcon: any) => {
    const state = ops[opKey];
    if (state?.status === "pending") return <Loader2 size={14} strokeWidth={3} className="animate-spin" />;
    if (state?.status === "failed") return <RotateCcw size={14} strokeWidth={3} />;
    return <DefaultIcon size={14} strokeWidth={3} />;
  };

  const getBtnClass = (opKey: string) => {
    const state = ops[opKey];
    if (state?.status === "failed") return "border-destructive text-destructive bg-destructive/10 hover:bg-destructive/20";
    return "bg-secondary text-secondary-foreground hover:bg-secondary/80 border-border/50";
  };

  const isLoading = creditsLoading || salesLoading || productsLoading;
  const isToday = datum === todayStr();
  const products = productsData?.products ?? [];
  const ammo12Prod = products.find(p => p.category === "AMMO_CAL12");
  const ammo20Prod = products.find(p => p.category === "AMMO_CAL20");

  const combinedMap = new Map<number, any>();

  const rows = creditsData?.kredite ?? [];
  for (const r of rows) {
    combinedMap.set(r.spielerId, {
      ...r,
      rest: r.gewaehrt - r.verbraucht,
      ammo12: 0,
      ammo20: 0,
    });
  }

  const sales = salesData?.sales ?? [];
  for (const s of sales) {
    let row = combinedMap.get(s.spielerId);
    if (!row) {
      row = {
        spielerId: s.spielerId,
        name: s.spielerName,
        mitgliedNr: null,
        gewaehrt: 0,
        verbraucht: 0,
        rest: 0,
        ammo12: 0,
        ammo20: 0,
      };
      combinedMap.set(s.spielerId, row);
    }
    if (ammo12Prod && s.productId === ammo12Prod.id) row.ammo12 += s.quantity;
    if (ammo20Prod && s.productId === ammo20Prod.id) row.ammo20 += s.quantity;
  }

  const combinedRows = Array.from(combinedMap.values()).sort((a, b) => a.name.localeCompare(b.name));

  const totalGewaehrt = combinedRows.reduce((s, r) => s + r.gewaehrt, 0);
  const totalVerbraucht = combinedRows.reduce((s, r) => s + r.verbraucht, 0);
  const totalRest = totalGewaehrt - totalVerbraucht;

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between flex-wrap gap-4">
        <p className="text-sm text-muted-foreground font-medium">
          Im Voraus bezahlte Spiele pro Tag, einschließlich Munition.{isToday && " Diese können für heute hier angepasst werden."}
        </p>
        <input
          type="date"
          value={datum}
          onChange={(e) => e.target.value && setDatum(e.target.value)}
          className="bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 transition-colors"
        />
      </div>

      <div className="grid grid-cols-3 gap-4">
        <StatCard label="Gekauft" value={totalGewaehrt} />
        <StatCard label="Gespielt" value={totalVerbraucht} />
        <StatCard label={isToday ? "Noch offen" : "Nicht genutzt"} value={totalRest} />
      </div>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-3">{[1, 2, 3].map(i => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}</div>
        ) : combinedRows.length === 0 ? (
          <div className="p-12 text-center text-muted-foreground">
            <Coins size={32} className="mx-auto mb-3 opacity-30" />
            <p className="text-sm font-medium">Keine Guthaben oder Munition für den {datum}.</p>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Spieler</TableHead>
                   <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Guthaben (Rest)</TableHead>
                   <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Gespielt</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Cal. 12</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Cal. 20</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {combinedRows.map((r) => {
                  const creditMinusKey = `${r.spielerId}-credit--1`;
                  const creditPlusKey = `${r.spielerId}-credit-1`;
                  const ammo12MinusKey = `${r.spielerId}-ammo12--1`;
                  const ammo12PlusKey = `${r.spielerId}-ammo12-1`;
                  const ammo20MinusKey = `${r.spielerId}-ammo20--1`;
                  const ammo20PlusKey = `${r.spielerId}-ammo20-1`;

                  return (
                    <TableRow key={r.spielerId} className="border-border/30 hover:bg-secondary/20 transition-colors">
                      <TableCell>
                        <div className="font-bold text-foreground truncate max-w-[180px]" title={r.name}>{r.name}</div>
                        {r.mitgliedNr && <div className="text-muted-foreground text-[10px] font-mono">{r.mitgliedNr}</div>}
                      </TableCell>
                      <TableCell className="text-center font-mono">
                        {isToday ? (
                          <div className="flex items-center justify-center gap-1.5">
                            <button
                              disabled={r.rest <= 0 || ops[creditMinusKey]?.status === "pending"}
                              onClick={() => handleAdjustCredit(r.spielerId, -1)}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(creditMinusKey))}
                              aria-label="Kredit -1"
                              title="Kredit -1"
                              data-testid={`credit-minus-${r.spielerId}`}
                            >{renderIcon(creditMinusKey, Minus)}</button>
                            <span className={cn("w-6 text-center font-bold text-base", r.rest > 0 ? "text-primary" : "text-muted-foreground/40")}>{r.rest}</span>
                            <button
                              disabled={ops[creditPlusKey]?.status === "pending"}
                              onClick={() => handleAdjustCredit(r.spielerId, 1)}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(creditPlusKey))}
                              aria-label="Kredit +1"
                              title="Kredit +1"
                              data-testid={`credit-plus-${r.spielerId}`}
                            >{renderIcon(creditPlusKey, Plus)}</button>
                          </div>
                        ) : (
                          <span className="font-bold">{r.rest > 0 ? <span className="text-primary">{r.rest}</span> : <span className="text-muted-foreground/40">0</span>}</span>
                        )}
                      </TableCell>
                      <TableCell className="text-center font-mono text-muted-foreground">
                        {r.verbraucht}
                      </TableCell>
                      <TableCell className="text-center font-mono">
                        {isToday ? (
                          <div className="flex items-center justify-center gap-1.5">
                            <button
                              disabled={r.ammo12 <= 0 || !ammo12Prod || ops[ammo12MinusKey]?.status === "pending"}
                              onClick={() => ammo12Prod && handleAdjustAmmo(r.spielerId, ammo12Prod.id, -1, '12')}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(ammo12MinusKey))}
                              aria-label="Cal.12 -1"
                              title="Cal.12 -1"
                              data-testid={`ammo12-minus-${r.spielerId}`}
                            >{renderIcon(ammo12MinusKey, Minus)}</button>
                            <span className={cn("w-6 text-center font-bold text-base", r.ammo12 > 0 ? "text-amber-500" : "text-muted-foreground/40")}>{r.ammo12}</span>
                            <button
                              disabled={!ammo12Prod || ops[ammo12PlusKey]?.status === "pending"}
                              onClick={() => ammo12Prod && handleAdjustAmmo(r.spielerId, ammo12Prod.id, 1, '12')}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(ammo12PlusKey))}
                              aria-label="Cal.12 +1"
                              title="Cal.12 +1"
                              data-testid={`ammo12-plus-${r.spielerId}`}
                            >{renderIcon(ammo12PlusKey, Plus)}</button>
                          </div>
                        ) : (
                          <span className="font-bold">{r.ammo12 > 0 ? <span className="text-amber-500">{r.ammo12}</span> : <span className="text-muted-foreground/40">0</span>}</span>
                        )}
                      </TableCell>
                      <TableCell className="text-center font-mono">
                        {isToday ? (
                          <div className="flex items-center justify-center gap-1.5">
                            <button
                              disabled={r.ammo20 <= 0 || !ammo20Prod || ops[ammo20MinusKey]?.status === "pending"}
                              onClick={() => ammo20Prod && handleAdjustAmmo(r.spielerId, ammo20Prod.id, -1, '20')}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(ammo20MinusKey))}
                              aria-label="Cal.20 -1"
                              title="Cal.20 -1"
                              data-testid={`ammo20-minus-${r.spielerId}`}
                            >{renderIcon(ammo20MinusKey, Minus)}</button>
                            <span className={cn("w-6 text-center font-bold text-base", r.ammo20 > 0 ? "text-amber-500" : "text-muted-foreground/40")}>{r.ammo20}</span>
                            <button
                              disabled={!ammo20Prod || ops[ammo20PlusKey]?.status === "pending"}
                              onClick={() => ammo20Prod && handleAdjustAmmo(r.spielerId, ammo20Prod.id, 1, '20')}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(ammo20PlusKey))}
                              aria-label="Cal.20 +1"
                              title="Cal.20 +1"
                              data-testid={`ammo20-plus-${r.spielerId}`}
                            >{renderIcon(ammo20PlusKey, Plus)}</button>
                          </div>
                        ) : (
                          <span className="font-bold">{r.ammo20 > 0 ? <span className="text-amber-500">{r.ammo20}</span> : <span className="text-muted-foreground/40">0</span>}</span>
                        )}
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
          Jahresübersicht aller gekauften Guthaben – zusammengefasst über alle Schießtage.
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
            <StatCard label="Insgesamt gekauft" value={data?.totalGewaehrt ?? 0} sub="Guthaben" />
            <StatCard label="Insgesamt gespielt" value={data?.totalVerbraucht ?? 0} sub="Guthaben" />
            <StatCard label="Schießtage" value={data?.anzahlDagen ?? 0} sub="Tage" />
            <StatCard label="Verschiedene Schützen" value={data?.anzahlSpiller ?? 0} sub="Spieler" />
          </div>

          <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
            <div className="px-6 py-4 border-b border-border/50 bg-secondary/10">
               <h3 className="font-bold text-sm uppercase tracking-widest text-muted-foreground">Pro Monat {year}</h3>
            </div>
            {!data || data.byMonth.length === 0 ? (
              <div className="p-12 text-center text-muted-foreground">
                <Coins size={28} className="mx-auto mb-3 opacity-30" />
                <p className="text-sm font-medium">Keine Daten für {year}.</p>
              </div>
            ) : (
              <div className="overflow-x-auto">
                <Table>
                  <TableHeader className="bg-secondary/20">
                    <TableRow className="border-border/50 hover:bg-transparent">
                       <TableHead className="text-xs uppercase tracking-widest font-bold">Monat</TableHead>
                       <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Gekauft</TableHead>
                       <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Gespielt</TableHead>
                       <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Schießtage</TableHead>
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
          Ausgelöste Tauben basierend auf den Spielergebnissen – A–G = 1 Taube, H = 2 Tauben (H1+H2).
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
            <StatCard label="Tauben gesamt" value={data?.total ?? 0} sub="einzelne Tauben ausgelöst" />
            <StatCard label="Maschinen A–G" value={aGTotal} sub="einzelne Tauben" />
            <StatCard label="Dublette H" value={`${hTotal} (${hDoubletten}×)`} sub="einzelne Tauben (Dublettenzyklen)" />
          </div>

          <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
            <div className="px-6 py-4 border-b border-border/50 bg-secondary/10">
               <h3 className="font-bold text-sm uppercase tracking-widest text-muted-foreground">Pro Maschine {year}</h3>
            </div>
            {!data || data.total === 0 ? (
              <div className="p-12 text-center text-muted-foreground">
                <Target size={28} className="mx-auto mb-3 opacity-30" />
                <p className="text-sm font-medium">Keine Ergebnisse für {year} gefunden.</p>
              </div>
            ) : (
              <div className="overflow-x-auto">
                <Table>
                  <TableHeader className="bg-secondary/20">
                    <TableRow className="border-border/50 hover:bg-transparent">
                       <TableHead className="text-xs uppercase tracking-widest font-bold">Maschine</TableHead>
                      <TableHead className="text-xs uppercase tracking-widest font-bold">Typ</TableHead>
                       <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Tauben ausgelöst</TableHead>
                       <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Anteil</TableHead>
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
                             {m === "H" ? "Dublette (H1 + H2)" : "Einzelne Taube"}
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
   { id: "dag",    label: "Tag" },
   { id: "joer",   label: "Jahr" },
   { id: "tauben", label: "Ausgelöste Tauben" },
];

export default function AdminKredite() {
  const token = useAuthStore((s) => s.token);
  const [activeTab, setActiveTab] = useState<TabId>("dag");

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight">Guthaben</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">
          Tagesübersicht, Jahreszusammenfassung und ausgelöste Tauben.
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
