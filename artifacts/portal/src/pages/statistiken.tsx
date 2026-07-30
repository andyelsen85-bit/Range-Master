import { useAuthStore } from "@/store/use-auth-store";
import { useGetStatistik, getGetStatistikQueryKey } from "@workspace/api-client-react";
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from "@/components/ui/card";
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, RadarChart, PolarGrid, PolarAngleAxis, PolarRadiusAxis, Radar } from "recharts";
import { Skeleton } from "@/components/ui/skeleton";

interface StatistikenProps {
  spielerId?: number;
}

export default function Statistiken({ spielerId }: StatistikenProps = {}) {
  const user = useAuthStore((s) => s.user);
  const effectiveId = spielerId ?? user?.id ?? 0;

  const { data: stats, isLoading } = useGetStatistik(effectiveId, {
    query: { enabled: !!effectiveId, queryKey: getGetStatistikQueryKey(effectiveId) }
  });

  const machineData = stats?.maschinen 
    ? Object.entries(stats.maschinen).map(([key, val]) => ({
        name: `Maschinn ${key}`,
        short: key,
        versuche: val.versuche,
        treffer: val.treffer,
        quote: Math.round(val.quote)
      }))
    : [];

  return (
    <div className="space-y-8 animate-in fade-in duration-500">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight">Detailléiert Statistiken</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">Iwwerpréift Är Schwächten a Stäerkten.</p>
      </header>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card className="bg-card border-border/50 shadow-sm overflow-hidden">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-5">
            <CardTitle className="text-sm uppercase tracking-widest font-bold">Trefferquote pro Maschinn</CardTitle>
            <CardDescription className="font-medium mt-1">Weist wéi gutt Dir verschidde Fluchbunne trefft.</CardDescription>
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
            <CardDescription className="font-medium mt-1">Visuell Duerstellung vun Ärer Tauben-Performance.</CardDescription>
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

      {machineData.length > 0 && (
        <Card className="bg-card border-border/50 shadow-sm overflow-hidden mt-6">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-4">
            <CardTitle className="text-xs uppercase tracking-widest font-bold text-muted-foreground">Rohdaten Detailer</CardTitle>
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
  return <div className="w-full h-full flex items-center justify-center text-muted-foreground font-medium text-sm">Keng Daten disponibel</div>;
}
