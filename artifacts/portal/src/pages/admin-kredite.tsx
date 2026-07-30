import { useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Skeleton } from "@/components/ui/skeleton";
import { Coins } from "lucide-react";

interface KreditRow {
  spielerId: number;
  name: string;
  mitgliedNr: string | null;
  gewaehrt: number;
  verbraucht: number;
}

function todayStr(): string {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

export default function AdminKredite() {
  const token = useAuthStore((s) => s.token);
  const [datum, setDatum] = useState(todayStr());

  const { data, isLoading } = useQuery<{ datum: string; kredite: KreditRow[] }>({
    queryKey: ["admin-kredite", datum],
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
    <div className="space-y-6 animate-in fade-in duration-500">
      <header className="flex items-start justify-between border-b border-border/50 pb-6 gap-4 flex-wrap">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Dageskreditter</h1>
          <p className="text-muted-foreground mt-2 text-sm font-medium">
            Virbezuelte Spiller pro Dag — um Terminal verwalt, hei nëmme liesbar.
          </p>
        </div>
        <input
          type="date"
          value={datum}
          onChange={(e) => e.target.value && setDatum(e.target.value)}
          className="bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
        />
      </header>

      {/* Summary cards */}
      <div className="grid grid-cols-3 gap-4">
        {[
          { label: "Kaaft", value: totalGewaehrt },
          { label: "Gespillt", value: totalVerbraucht },
          { label: isToday ? "Nach oppen" : "Net benotzt (zréckbezuelt)", value: totalRest },
        ].map((c) => (
          <div key={c.label} className="bg-card border border-border/50 rounded-xl p-5 shadow-sm">
            <p className="text-xs uppercase tracking-widest font-bold text-muted-foreground">{c.label}</p>
            <p className="text-3xl font-bold mt-1 font-mono">{c.value}</p>
          </div>
        ))}
      </div>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-3">
            {[1, 2, 3].map((i) => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}
          </div>
        ) : rows.length === 0 ? (
          <div className="p-12 text-center text-muted-foreground">
            <Coins size={32} className="mx-auto mb-3 opacity-30" />
            <p className="text-sm font-medium">Keng Kreditter fir den {datum}.</p>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Numm</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Mitglied Nr</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Kaaft</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Gespillt</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Rest</TableHead>
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
                        {rest > 0
                          ? <span className="text-primary">{rest}</span>
                          : <span className="text-muted-foreground/40">0</span>}
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
