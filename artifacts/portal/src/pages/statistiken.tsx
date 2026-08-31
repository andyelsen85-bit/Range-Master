import { useAuthStore } from "@/store/use-auth-store";
import {
  useGetStatistik,
  getGetStatistikQueryKey,
  useGetStatistikModusBreakdown,
  getGetStatistikModusBreakdownQueryKey,
} from "@workspace/api-client-react";
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from "@/components/ui/card";
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, RadarChart, PolarGrid, PolarAngleAxis, PolarRadiusAxis, Radar } from "recharts";
import { Skeleton } from "@/components/ui/skeleton";

const MODUS_LABEL: Record<string, string> = {
  NORMAL: "Normal",
  HARAKIRI: "Harakiri",
  HARAKIRI_DELAYED: "Harakiri Delayed",
  HARAKIRI_FULL: "Harakiri Full",
  CUSTOM_1: "Custom 1",
  CUSTOM_2: "Custom 2",
  CUSTOM_3: "Custom 3",
};

interface StatistikenProps {
  spielerId?: number;
}

export default function Statistiken({ spielerId }: StatistikenProps = {}) {
  const user = useAuthStore((s) => s.user);
  const effectiveId = spielerId ?? user?.id ?? 0;

  const { data: stats, isLoading } = useGetStatistik(effectiveId, {
    query: { enabled: !!effectiveId, queryKey: getGetStatistikQueryKey(effectiveId) }
  });

  const { data: breakdownData, isLoading: isLoadingBreakdown } = useGetStatistikModusBreakdown(effectiveId, {
    query: { enabled: !!effectiveId, queryKey: getGetStatistikModusBreakdownQueryKey(effectiveId) }
  });

  const machineData = stats?.maschinen 
    ? Object.entries(stats.maschinen).map(([key, val]) => ({
        name: `Maschine ${key}`,
        short: key,
        versuche: val.versuche,
        treffer: val.treffer,
        quote: Math.round(val.quote)
      }))
    : [];

  return (
    <div className="space-y-8 animate-in fade-in duration-500">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight">Detaillierte Statistiken</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">Überprüfen Sie Ihre Stärken und Schwächen.</p>
      </header>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card className="bg-card border-border/50 shadow-sm overflow-hidden">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-5">
            <CardTitle className="text-sm uppercase tracking-widest font-bold">Trefferquote pro Maschine</CardTitle>
            <CardDescription className="font-medium mt-1">Zeigt, wie gut Sie verschiedene Flugbahnen treffen.</CardDescription>
          </CardHeader>
          <CardContent className="h-[340px] pt-6">
            {isLoading ? <Skeleton className="w-full h-full bg-secondary/20 rounded-md" /> : 
             machineData.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={machineData} layout="vertical" margin={{ top: 5, right: 30, left: 10, bottom: 5 }}>
                  <XAxis type="number" domain={[0, 100]} stroke="hsl(var(--muted-foreground))" fontSize={12} tickFormatter={(v) => `${v}%`} fontFamily="var(--font-mono)" tickLine={false} axisLine={false} />
                  <YAxis dataKey="short" type="category" stroke="hsl(var(--foreground))" fontSize={16} fontWeight="900" fontFamily="var(--font-mono)" tickLine={false} axisLine={false} />
                  <Tooltip 
                    cursor={{ fill: 'hsl(var(--secondary))' }}
                    contentStyle={{ backgroundColor: 'hsl(var(--card))', borderColor: 'hsl(var(--border))', borderRadius: '8px', fontFamily: 'var(--font-sans)', fontWeight: 600 }}
                  />
                  <Bar dataKey="quote" name="Quote (%)" fill="hsl(var(--primary))" radius={[0, 4, 4, 0]} maxBarSize={30} />
                </BarChart>
              </ResponsiveContainer>
             ) : <EmptyState />}
          </CardContent>
        </Card>

        <Card className="bg-card border-border/50 shadow-sm overflow-hidden">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-5">
            <CardTitle className="text-sm uppercase tracking-widest font-bold">Profil Radar</CardTitle>
            <CardDescription className="font-medium mt-1">Visuelle Darstellung Ihrer Leistung bei den Tauben.</CardDescription>
          </CardHeader>
          <CardContent className="h-[340px] pt-6">
            {isLoading ? <Skeleton className="w-full h-full bg-secondary/20 rounded-md" /> : 
             machineData.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <RadarChart cx="50%" cy="50%" outerRadius="65%" data={machineData}>
                  <PolarGrid stroke="hsl(var(--border))" strokeDasharray="3 3" />
                  <PolarAngleAxis dataKey="short" stroke="hsl(var(--muted-foreground))" fontSize={14} fontWeight="900" fontFamily="var(--font-mono)" />
                  <PolarRadiusAxis angle={90} domain={[0, 100]} stroke="hsl(var(--muted-foreground))" tick={false} axisLine={false} />
                  <Radar name="Quote" dataKey="quote" stroke="hsl(var(--primary))" strokeWidth={3} fill="hsl(var(--primary))" fillOpacity={0.25} />
                  <Tooltip 
                    contentStyle={{ backgroundColor: 'hsl(var(--card))', borderColor: 'hsl(var(--border))', borderRadius: '8px', fontFamily: 'var(--font-sans)', fontWeight: 600 }}
                  />
                </RadarChart>
              </ResponsiveContainer>
             ) : <EmptyState />}
          </CardContent>
        </Card>
      </div>

      {/* Per-format leaderboard score breakdown */}
      <Card className="bg-card border-border/50 shadow-sm overflow-hidden">
        <CardHeader className="bg-secondary/20 border-b border-border/50 pb-5">
          <CardTitle className="text-sm uppercase tracking-widest font-bold">Score pro Spillformat</CardTitle>
          <CardDescription className="font-medium mt-1">
            Wie sich Ihr normalisierter Ranglistenwert je Spielmodus zusammensetzt.
          </CardDescription>
        </CardHeader>
        <CardContent className="pt-0">
          {isLoadingBreakdown ? (
            <div className="divide-y divide-border/40">
              {[1, 2, 3].map((i) => (
                <div key={i} className="flex items-center gap-4 py-4 px-6">
                  <Skeleton className="h-5 w-28 bg-secondary/30 rounded" />
                  <div className="flex-1 flex justify-end gap-8">
                    <Skeleton className="h-4 w-14 bg-secondary/20 rounded" />
                    <Skeleton className="h-4 w-14 bg-secondary/20 rounded" />
                    <Skeleton className="h-4 w-14 bg-secondary/20 rounded" />
                  </div>
                </div>
              ))}
            </div>
          ) : breakdownData?.breakdown?.length ? (
            <>
              {/* Header row */}
              <div className="flex items-center gap-4 px-6 py-3 border-b border-border/40 text-[10px] font-black uppercase tracking-widest text-muted-foreground">
                <span className="flex-1">Format</span>
                 <span className="w-20 text-right">Spiele</span>
                <span className="w-24 text-right">Ø Punkte</span>
                 <span className="w-24 text-right">Normalisiert</span>
              </div>
              <div className="divide-y divide-border/40">
                {breakdownData.breakdown
                  .slice()
                  .sort((a, b) => b.durchschnittProzent - a.durchschnittProzent)
                  .map((entry) => (
                    <div
                      key={entry.modus}
                      className="flex items-center gap-4 px-6 py-4 hover:bg-secondary/10 transition-colors group"
                    >
                      <span className="flex-1 text-sm font-bold text-foreground">
                        {MODUS_LABEL[entry.modus] ?? entry.modus}
                      </span>
                      <span className="w-20 text-right text-sm font-mono font-bold text-muted-foreground">
                        {entry.anzahlSpiele}
                      </span>
                      <span className="w-24 text-right text-sm font-mono font-bold text-muted-foreground">
                        {entry.durchschnitt.toFixed(1)}
                      </span>
                      <span className="w-24 text-right">
                        <span className="text-base font-black tracking-tight text-primary">
                          {entry.durchschnittProzent.toFixed(1)}%
                        </span>
                      </span>
                    </div>
                  ))}
              </div>
            </>
          ) : (
            <div className="py-12 flex items-center justify-center text-muted-foreground font-medium text-sm">
               Keine Spieldaten verfügbar
            </div>
          )}
        </CardContent>
      </Card>

      {machineData.length > 0 && (
        <Card className="bg-card border-border/50 shadow-sm overflow-hidden mt-6">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-4">
            <CardTitle className="text-xs uppercase tracking-widest font-bold text-muted-foreground">Rohdaten im Detail</CardTitle>
          </CardHeader>
          <CardContent className="pt-6">
            <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
              {machineData.map((d) => (
                <div key={d.short} className="p-4 bg-background rounded-xl border border-border/50 flex justify-between items-center group hover:border-primary/50 transition-colors">
                  <div className="w-12 h-12 rounded-full bg-secondary/50 border border-border/50 flex items-center justify-center font-black text-xl text-primary group-hover:bg-primary/10 transition-colors">
                    {d.short}
                  </div>
                  <div className="text-right">
                    <div className="text-xl font-black tracking-tight">{d.quote}%</div>
                    <div className="text-xs font-mono text-muted-foreground font-bold mt-1 tracking-wider">{d.treffer} <span className="opacity-50">/</span> {d.versuche}</div>
                  </div>
                </div>
              ))}
            </div>
          </CardContent>
        </Card>
      )}
    </div>
  );
}

function EmptyState() {
  return <div className="w-full h-full flex items-center justify-center text-muted-foreground font-medium text-sm">Keine Daten verfügbar</div>;
}
