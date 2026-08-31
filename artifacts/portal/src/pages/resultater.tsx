import { useAuthStore } from "@/store/use-auth-store";
import { useGetSpielerErgebnisse, getGetSpielerErgebnisseQueryKey, ErgebnisWithSpiel } from "@workspace/api-client-react";
import { Skeleton } from "@/components/ui/skeleton";
import { Badge } from "@/components/ui/badge";
import { AlertCircle } from "lucide-react";

interface ResultaterProps {
  spielerId?: number;
}

export default function Resultater({ spielerId }: ResultaterProps = {}) {
  const user = useAuthStore((s) => s.user);
  const effectiveId = spielerId ?? user?.id ?? 0;

  const { data, isLoading } = useGetSpielerErgebnisse(effectiveId, {
    query: { enabled: !!effectiveId, queryKey: getGetSpielerErgebnisseQueryKey(effectiveId) }
  });

  // Group by spielId only — both Läufe belong to one game card
  const grouped: Record<number, ErgebnisWithSpiel[]> = {};
  if (data?.ergebnisse) {
    data.ergebnisse.forEach(e => {
      if (!grouped[e.spielId]) grouped[e.spielId] = [];
      grouped[e.spielId].push(e);
    });
  }

  const sortedGroups = Object.values(grouped).sort((a, b) => {
    const dateA = new Date(a[0].spiel.datum).getTime();
    const dateB = new Date(b[0].spiel.datum).getTime();
    return dateB - dateA;
  });

  return (
    <div className="space-y-8 animate-in fade-in duration-500">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight">Meine Ergebnisse</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">Detaillierte Analyse jeder Runde.</p>
      </header>

      {isLoading ? (
        <div className="space-y-6">
          {[1,2,3].map(i => <Skeleton key={i} className="h-48 w-full rounded-xl bg-card border border-border/50" />)}
        </div>
      ) : sortedGroups.length > 0 ? (
        <div className="space-y-8">
          {sortedGroups.map((group, idx) => (
            <ResultGroup key={idx} ergebnisse={group} />
          ))}
        </div>
      ) : (
        <div className="p-12 text-center bg-card rounded-xl border border-border/50">
          <AlertCircle className="mx-auto h-12 w-12 text-muted-foreground mb-4 opacity-50" />
          <h3 className="text-lg font-bold mb-1">Noch keine Ergebnisse</h3>
          <p className="text-muted-foreground font-medium">Sie haben in dieser Saison noch keine Spiele abgeschlossen.</p>
        </div>
      )}
    </div>
  );
}

function ResultGroup({ ergebnisse }: { ergebnisse: ErgebnisWithSpiel[] }) {
  const first = ergebnisse[0];
  const date = new Date(first.spiel.datum).toLocaleString('lb-LU', { 
    year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute:'2-digit' 
  });

  const totalPoints = ergebnisse.reduce((sum, e) => sum + e.punkte, 0);

  // Split into Lauf 1 and Lauf 2, sorted by taube within each
  const lauf1 = ergebnisse.filter(e => e.lauf === 1).sort((a, b) => a.taube - b.taube);
  const lauf2 = ergebnisse.filter(e => e.lauf === 2).sort((a, b) => a.taube - b.taube);
  const lauf1Pts = lauf1.reduce((s, e) => s + e.punkte, 0);
  const lauf2Pts = lauf2.reduce((s, e) => s + e.punkte, 0);

  // Dynamic max: each taube is worth max 2 pts
  const maxPerLauf = Math.max(lauf1.length, lauf2.length, 1) * 2;
  const maxTotal = (lauf1.length + lauf2.length) * 2;

  return (
    <div className="bg-card rounded-xl border border-border/50 overflow-hidden shadow-sm">
      {/* Game header */}
      <div className="p-5 md:p-6 flex flex-col md:flex-row md:items-center justify-between gap-4 border-b border-border/50 bg-secondary/10">
        <div>
          <div className="flex flex-wrap items-center gap-3 mb-2">
            <h3 className="text-lg font-bold tracking-tight">{date}</h3>
            <Badge variant="outline" className="font-mono text-xs tracking-wider bg-background text-primary border-primary/30 uppercase font-bold">
              {first.spiel.modus}
            </Badge>
          </div>
          <p className="text-xs text-muted-foreground font-mono uppercase tracking-widest font-bold">
             2 Durchgänge • {Math.max(lauf1.length, lauf2.length)} Tauben / Durchgang • max. {maxTotal} Pkt.
          </p>
        </div>
        <div className="flex items-end gap-6">
          {/* Per-lauf subtotals */}
          <div className="flex gap-4 text-sm font-mono text-muted-foreground">
            <div className="text-center">
              <div className="text-[10px] uppercase tracking-widest mb-1 font-bold">Durchgang 1</div>
              <div className="font-black text-foreground">{lauf1Pts}<span className="text-muted-foreground font-bold">/{maxPerLauf}</span></div>
            </div>
            <div className="text-muted-foreground/30 self-center text-lg">+</div>
            <div className="text-center">
              <div className="text-[10px] uppercase tracking-widest mb-1 font-bold">Durchgang 2</div>
              <div className="font-black text-foreground">{lauf2Pts}<span className="text-muted-foreground font-bold">/{maxPerLauf}</span></div>
            </div>
          </div>
          {/* Total */}
          <div className="flex flex-col items-end">
            <div className="text-xs text-muted-foreground uppercase tracking-[0.2em] font-bold mb-1">Total</div>
            <div className="text-3xl font-black tracking-tight text-primary">
              {totalPoints} <span className="text-muted-foreground text-xl font-bold">/ {maxTotal}</span>
            </div>
          </div>
        </div>
      </div>

      {/* Lauf 1 row */}
      {lauf1.length > 0 && (
        <div className="border-b border-border/30">
          <div className="px-5 pt-4 pb-1 flex items-center gap-3">
            <span className="text-[10px] font-black uppercase tracking-[0.2em] text-muted-foreground">Durchgang 1</span>
            <span className="text-xs font-mono font-bold text-primary">{lauf1Pts} Pkt</span>
          </div>
          <div className="px-5 pb-4 overflow-x-auto">
            <div className="flex gap-3 min-w-max">
              {lauf1.map((e) => <ShotBadge key={e.id} ergebnis={e} />)}
            </div>
          </div>
        </div>
      )}

      {/* Lauf 2 row */}
      {lauf2.length > 0 && (
        <div>
          <div className="px-5 pt-4 pb-1 flex items-center gap-3">
            <span className="text-[10px] font-black uppercase tracking-[0.2em] text-muted-foreground">Durchgang 2</span>
            <span className="text-xs font-mono font-bold text-primary">{lauf2Pts} Pkt</span>
          </div>
          <div className="px-5 pb-4 overflow-x-auto">
            <div className="flex gap-3 min-w-max">
              {lauf2.map((e) => <ShotBadge key={e.id} ergebnis={e} />)}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

function ShotBadge({ ergebnis }: { ergebnis: ErgebnisWithSpiel }) {
  const isHit = ergebnis.punkte > 0;
  
  return (
    <div className="flex flex-col items-center p-3 rounded-lg border border-border bg-background min-w-[4rem] transition-colors hover:border-primary/50">
      <span className="text-[10px] text-muted-foreground font-mono font-bold mb-2">#{ergebnis.taube}</span>
      <div className={`w-10 h-10 rounded-full flex items-center justify-center font-black text-lg shadow-inner
        ${isHit ? 'bg-primary/10 text-primary ring-1 ring-primary/30' : 'bg-destructive/10 text-destructive ring-1 ring-destructive/20'}`}>
        {ergebnis.maschine}
      </div>
      <div className="flex gap-1.5 mt-3">
        <div className={`w-2.5 h-2.5 rounded-full ${ergebnis.schuss1 ? (ergebnis.punkte > 0 ? 'bg-primary shadow-[0_0_8px_rgba(232,103,10,0.6)]' : 'bg-destructive') : 'bg-muted border border-border'}`} />
        <div className={`w-2.5 h-2.5 rounded-full ${ergebnis.schuss2 ? (ergebnis.punkte > 0 ? 'bg-primary shadow-[0_0_8px_rgba(232,103,10,0.6)]' : 'bg-destructive') : 'bg-muted border border-border'}`} />
      </div>
    </div>
  );
}
