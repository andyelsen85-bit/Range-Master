import { useState } from "react";
import { keepPreviousData } from "@tanstack/react-query";
import { useGetRangliste, getGetRanglisteQueryKey, Modus } from "@workspace/api-client-react";
import type { RanglisteEntry } from "@workspace/api-client-react";
import { Skeleton } from "@/components/ui/skeleton";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Trophy, Medal, Award } from "lucide-react";
import { modeLabel } from "@/components/scorecard";
import { useAuthStore } from "@/store/use-auth-store";
import { cn } from "@/lib/utils";

const LIMIT = 15;

export default function Rangliste() {
  const currentYear = new Date().getFullYear();
  const [modus, setModus] = useState<string>("ALL");
  const [jahr, setJahr] = useState<string>(String(currentYear));
  const userId = useAuthStore((s) => s.user?.id);

  const apiModus = modus === "ALL" ? undefined : (modus as Modus);
  const params = { modus: apiModus, jahr: parseInt(jahr, 10), limit: LIMIT };
  const { data, isLoading, isError, refetch } = useGetRangliste(params, {
    query: { queryKey: getGetRanglisteQueryKey(params), placeholderData: keepPreviousData },
  });

  // Year list from the data (first game year .. current year), never hard-coded.
  const years = (() => {
    const ys = data?.years?.length ? [...data.years] : [];
    const min = ys.length ? Math.min(...ys) : currentYear;
    const all = new Set<number>([...ys, currentYear, parseInt(jahr, 10)]);
    for (let y = min; y <= currentYear; y++) all.add(y);
    return [...all].sort((a, b) => b - a);
  })();

  const rows = (data?.rangliste ?? []).slice(0, LIMIT);
  const own = data?.ownRow && !rows.some((r) => r.spielerId === data.ownRow?.spielerId) ? data.ownRow : null;

  return (
    <div className="space-y-8 animate-in fade-in duration-500">
      <header className="flex flex-col md:flex-row md:items-end justify-between gap-6 border-b border-border/50 pb-6">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Saisonrangliste</h1>
          <p className="text-muted-foreground mt-2 text-sm font-medium">Top {LIMIT} · Bestes Einzelergebnis</p>
        </div>
        <div className="flex items-center gap-3">
          <Select value={jahr} onValueChange={setJahr}>
            <SelectTrigger className="w-32 bg-card border-border/80 h-10 font-medium" data-testid="select-jahr"><SelectValue placeholder="Jahr" /></SelectTrigger>
            <SelectContent>
              {years.map((y) => <SelectItem key={y} value={String(y)}>{y}</SelectItem>)}
            </SelectContent>
          </Select>
          <Select value={modus} onValueChange={setModus}>
            <SelectTrigger className="w-48 bg-card border-border/80 h-10 font-medium" data-testid="select-modus"><SelectValue placeholder="Modus" /></SelectTrigger>
            <SelectContent>
              <SelectItem value="ALL">Alle Modi</SelectItem>
              {Object.values(Modus).map((m) => <SelectItem key={m} value={m}>{modeLabel(m)}</SelectItem>)}
            </SelectContent>
          </Select>
        </div>
      </header>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-4">{[1, 2, 3, 4, 5].map((i) => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}</div>
        ) : isError ? (
          <div className="p-16 text-center space-y-3">
            <p className="text-destructive font-medium">Rangliste konnte nicht geladen werden.</p>
            <button onClick={() => refetch()} className="px-3 py-1.5 rounded-lg border border-border/60 text-xs font-bold">Erneut versuchen</button>
          </div>
        ) : rows.length > 0 ? (
          <Table>
            <TableHeader className="bg-secondary/30">
              <TableRow className="border-border/50 hover:bg-transparent">
                <TableHead className="w-20 text-center font-bold uppercase tracking-widest text-[10px]">Rang</TableHead>
                <TableHead className="font-bold uppercase tracking-widest text-[10px]">Name</TableHead>
                <TableHead className="text-right font-bold uppercase tracking-widest text-[10px] pr-6">Bestes Ergebnis</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {rows.map((row) => <RankRow key={row.spielerId} row={row} own={row.spielerId === userId} />)}
              {own && (
                <>
                  <TableRow className="hover:bg-transparent border-0"><TableCell colSpan={3} className="text-center text-muted-foreground py-1">…</TableCell></TableRow>
                  <RankRow row={own} own />
                </>
              )}
            </TableBody>
          </Table>
        ) : (
          <div className="p-16 text-center"><p className="text-muted-foreground font-medium">Keine Ranglistendaten für diese Auswahl.</p></div>
        )}
      </div>
    </div>
  );
}

function RankRow({ row, own }: { row: RanglisteEntry; own: boolean }) {
  return (
    <TableRow className={cn("border-border/50 transition-colors", own ? "bg-primary/10 hover:bg-primary/15" : "hover:bg-secondary/40")} data-testid={`row-rang-${row.spielerId}`}>
      <TableCell className="text-center font-bold font-mono text-lg"><RankIcon rank={row.rang} /></TableCell>
      <TableCell className={cn("font-bold text-base", own && "text-primary")}>{row.name}{own && <span className="ml-2 text-[10px] uppercase tracking-widest">du</span>}</TableCell>
      <TableCell className="text-right pr-6 font-mono font-black text-lg tabular-nums" data-testid={`text-best-${row.spielerId}`}>
        {row.bestPunkte}{row.bestMaximum ? <span className="text-muted-foreground font-bold text-sm"> / {row.bestMaximum}</span> : null}
      </TableCell>
    </TableRow>
  );
}

function RankIcon({ rank }: { rank: number }) {
  if (rank === 1) return <div className="flex justify-center"><Trophy className="text-yellow-500 w-6 h-6" /></div>;
  if (rank === 2) return <div className="flex justify-center"><Medal className="text-zinc-300 w-6 h-6" /></div>;
  if (rank === 3) return <div className="flex justify-center"><Award className="text-amber-600 w-6 h-6" /></div>;
  return <span className="text-muted-foreground">{rank}</span>;
}
