import { useState } from "react";
import { useGetRangliste, getGetRanglisteQueryKey, Modus } from "@workspace/api-client-react";
import { Skeleton } from "@/components/ui/skeleton";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Trophy, Medal, Award } from "lucide-react";
import { Badge } from "@/components/ui/badge";

export default function Rangliste() {
  const [modus, setModus] = useState<string>("ALL");
  const [jahr, setJahr] = useState<string>(new Date().getFullYear().toString());

  const apiModus = modus === "ALL" ? undefined : (modus as Modus);
  const apiJahr = parseInt(jahr, 10);

  const { data, isLoading } = useGetRangliste(
    { modus: apiModus, jahr: apiJahr },
    { query: { queryKey: getGetRanglisteQueryKey({ modus: apiModus, jahr: apiJahr }) } }
  );

  // When "ALL" is selected, formats differ in max score — show the normalized % column prominently.
  // When filtered to a single modus, all games share the same max, so raw durchschnitt is fine.
  const showPercent = modus === "ALL";

  return (
    <div className="space-y-8 animate-in fade-in duration-500">
      <header className="flex flex-col md:flex-row md:items-end justify-between gap-6 border-b border-border/50 pb-6">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Saisonrangliste</h1>
          <p className="text-muted-foreground mt-2 text-sm font-medium">Vergleichen Sie Ihre Leistung mit anderen Mitgliedern.</p>
          {showPercent && (
            <p className="text-muted-foreground mt-1 text-xs">
              Sortiert nach normalisiertem Durchschnitt (% der maximal möglichen Punktzahl) – für einen fairen Vergleich über verschiedene Formate hinweg.
            </p>
          )}
        </div>

        <div className="flex items-center gap-3">
          <Select value={jahr} onValueChange={setJahr}>
            <SelectTrigger className="w-32 bg-card border-border/80 h-10 font-medium">
              <SelectValue placeholder="Jahr" />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="2026">2026</SelectItem>
              <SelectItem value="2025">2025</SelectItem>
              <SelectItem value="2024">2024</SelectItem>
            </SelectContent>
          </Select>

          <Select value={modus} onValueChange={setModus}>
            <SelectTrigger className="w-48 bg-card border-border/80 h-10 font-medium">
              <SelectValue placeholder="Modus" />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="ALL">All Modi</SelectItem>
              <SelectItem value="NORMAL">Normal</SelectItem>
              <SelectItem value="HARAKIRI">Harakiri</SelectItem>
              <SelectItem value="HARAKIRI_DELAYED">Harakiri Delayed</SelectItem>
              <SelectItem value="HARAKIRI_FULL">Harakiri Full</SelectItem>
              <SelectItem value="CUSTOM_1">Custom 1</SelectItem>
              <SelectItem value="CUSTOM_2">Custom 2</SelectItem>
              <SelectItem value="CUSTOM_3">Custom 3</SelectItem>
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
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Spieler</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Spiele</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Gesamtpunkte</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Bestes Ergebnis</TableHead>
                  {showPercent ? (
                    <TableHead className="text-right text-xs uppercase tracking-widest font-bold text-primary">
                      Ø % Score
                      <Badge variant="outline" className="ml-1 text-[9px] px-1 py-0 font-normal border-primary/40 text-primary/80">normalisiert</Badge>
                    </TableHead>
                  ) : (
                    <TableHead className="text-right text-xs uppercase tracking-widest font-bold text-primary">Ø Durchschnitt</TableHead>
                  )}
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
                    {showPercent ? (
                      <TableCell className="text-right font-mono font-black text-primary text-lg">
                        {row.durchschnittProzent.toFixed(1)}%
                      </TableCell>
                    ) : (
                      <TableCell className="text-right font-mono font-black text-primary text-lg">
                        {row.durchschnitt.toFixed(1)}
                      </TableCell>
                    )}
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </div>
        ) : (
          <div className="p-16 text-center">
            <p className="text-muted-foreground font-medium">Keine Ranglistendaten für diese Auswahl.</p>
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
