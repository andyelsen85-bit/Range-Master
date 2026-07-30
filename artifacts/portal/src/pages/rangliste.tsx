import { useState } from "react";
import { useGetRangliste, getGetRanglisteQueryKey, Modus } from "@workspace/api-client-react";
import { Skeleton } from "@/components/ui/skeleton";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Trophy, Medal, Award } from "lucide-react";

export default function Rangliste() {
  const [modus, setModus] = useState<string>("ALL");
  const [jahr, setJahr] = useState<string>(new Date().getFullYear().toString());

  const apiModus = modus === "ALL" ? undefined : (modus as Modus);
  const apiJahr = parseInt(jahr, 10);

  const { data, isLoading } = useGetRangliste(
    { modus: apiModus, jahr: apiJahr },
    { query: { queryKey: getGetRanglisteQueryKey({ modus: apiModus, jahr: apiJahr }) } }
  );

  return (
    <div className="space-y-8 animate-in fade-in duration-500">
      <header className="flex flex-col md:flex-row md:items-end justify-between gap-6 border-b border-border/50 pb-6">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Saison Ranglischt</h1>
          <p className="text-muted-foreground mt-2 text-sm font-medium">Vergläicht Är Leeschtung mat anere Memberen.</p>
        </div>
        
        <div className="flex items-center gap-3">
          <Select value={jahr} onValueChange={setJahr}>
            <SelectTrigger className="w-32 bg-card border-border/80 h-10 font-medium">
              <SelectValue placeholder="Joer" />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="2025">2025</SelectItem>
              <SelectItem value="2024">2024</SelectItem>
            </SelectContent>
          </Select>

          <Select value={modus} onValueChange={setModus}>
            <SelectTrigger className="w-40 bg-card border-border/80 h-10 font-medium">
              <SelectValue placeholder="Modus" />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="ALL">All Modi</SelectItem>
              <SelectItem value="NORMAL">Normal</SelectItem>
              <SelectItem value="HARAKIRI">Harakiri</SelectItem>
            </SelectContent>
          </Select>
        </div>
      </header>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-4">
            {[1,2,3,4,5].map(i => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}
          </div>
        ) : data?.rangliste && data.rangliste.length > 0 ? (
          <div className="overflow-x-auto">
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                  <TableHead className="w-20 text-center font-mono text-xs uppercase tracking-widest font-bold">Rang</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Spiller</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Spiller</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Gesamt Punkten</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Bescht</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold text-primary">Ø Duerchschnëtt</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {data.rangliste.map((row) => (
                  <TableRow key={row.spielerId} className="border-border/30 hover:bg-secondary/30 transition-colors">
                    <TableCell className="text-center font-mono font-bold text-base h-16">
                      <RankIcon rank={row.rang} />
                    </TableCell>
                    <TableCell className="font-bold text-foreground text-base tracking-tight">{row.name}</TableCell>
                    <TableCell className="text-right font-mono font-medium text-muted-foreground">{row.anzahlSpiele}</TableCell>
                    <TableCell className="text-right font-mono font-medium text-muted-foreground">{row.gesamtPunkte}</TableCell>
                    <TableCell className="text-right font-mono font-bold text-foreground">{row.bestPunkte}</TableCell>
                    <TableCell className="text-right font-mono font-black text-primary text-lg">{row.durchschnitt.toFixed(2)}</TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </div>
        ) : (
          <div className="p-16 text-center">
            <p className="text-muted-foreground font-medium">Keng Ranglëscht Date fir dës Selektioun.</p>
          </div>
        )}
      </div>
    </div>
  );
}

function RankIcon({ rank }: { rank: number }) {
  if (rank === 1) return <div className="flex justify-center"><Trophy className="text-yellow-500 w-6 h-6 drop-shadow-[0_0_8px_rgba(234,179,8,0.5)]" /></div>;
  if (rank === 2) return <div className="flex justify-center"><Medal className="text-zinc-300 w-6 h-6 drop-shadow-[0_0_8px_rgba(212,212,216,0.3)]" /></div>;
  if (rank === 3) return <div className="flex justify-center"><Award className="text-amber-600 w-6 h-6 drop-shadow-[0_0_8px_rgba(217,119,6,0.4)]" /></div>;
  return <span className="text-muted-foreground">{rank}</span>;
}
