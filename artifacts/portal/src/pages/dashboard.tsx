import { useAuthStore } from "@/store/use-auth-store";
import { useGetStatistik, useGetStatistikVerlauf, getGetStatistikQueryKey, getGetStatistikVerlaufQueryKey } from "@workspace/api-client-react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Activity, Target, Trophy, Crosshair, AlertCircle } from "lucide-react";
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, AreaChart, Area } from "recharts";
import { Skeleton } from "@/components/ui/skeleton";

interface DashboardProps {
  spielerId?: number;
  playerName?: string;
}

export default function Dashboard({ spielerId, playerName }: DashboardProps = {}) {
  const user = useAuthStore((s) => s.user);
  const effectiveId = spielerId ?? user?.id ?? 0;
  const effectiveName = playerName ?? user?.name;

  const { data: stats, isLoading: statsLoading } = useGetStatistik(effectiveId, {
    query: { enabled: !!effectiveId, queryKey: getGetStatistikQueryKey(effectiveId) }
  });

  const { data: verlaufData, isLoading: verlaufLoading } = useGetStatistikVerlauf(effectiveId, { limit: 10 }, {
    query: { enabled: !!effectiveId, queryKey: getGetStatistikVerlaufQueryKey(effectiveId, { limit: 10 }) }
  });

  const machineData = stats?.maschinen 
    ? Object.entries(stats.maschinen).map(([key, val]) => ({
        name: key,
        quote: Math.round(val.quote)
      }))
    : [];

  const trendData = verlaufData?.verlauf?.slice().reverse().map(v => ({
    ...v,
    // Short label for the X-axis ticks (DD.MM keeps the axis readable)
    date: new Date(v.datum).toLocaleDateString('lb-LU', { day: '2-digit', month: '2-digit' }),
    // Full label used by the tooltip so hovering shows the complete date+time
    fullDate: new Date(v.datum).toLocaleString('lb-LU', {
      year: 'numeric', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit',
    }),
  })) || [];

  const trendYMax = trendData.length > 0
    ? Math.max(...trendData.map(v => v.maxPunkte ?? 36))
    : 36;

  return (
    <div className="space-y-8 animate-in fade-in slide-in-from-bottom-4 duration-500">
      <header className="flex flex-col md:flex-row md:items-end justify-between gap-4 border-b border-border/50 pb-6">
        <div>
          <h1 className="text-4xl font-bold tracking-tight text-foreground">Moien, {effectiveName}</h1>
          <div className="flex items-center gap-3 mt-2">
            <span className="flex h-2 w-2 rounded-full bg-primary animate-pulse" />
            <p className="text-muted-foreground text-xs font-mono uppercase tracking-widest font-bold">
              ID: {user?.mitgliedNr || "N/A"} • PORTAL AKTIV
            </p>
          </div>
        </div>
      </header>

      {statsLoading ? (
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          {[1,2,3,4].map(i => <Skeleton key={i} className="h-32 rounded-xl bg-card border border-border/50" />)}
        </div>
      ) : stats ? (
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          <StatCard title="Saison Spiller" value={stats.gesamtSpiele} icon={<Activity className="text-chart-2" />} />
          <StatCard title="Duerchschnëtt" value={stats.durchschnitt.toFixed(1)} icon={<Crosshair className="text-chart-3" />} />
          <StatCard title="Trefferquote" value={`${Math.round(stats.trefferquote)}%`} icon={<Target className="text-primary" />} />
          <StatCard title="Bescht Resultat" value={stats.bestPunkte} icon={<Trophy className="text-chart-4" />} />
        </div>
      ) : (
        <div className="p-12 text-center bg-card rounded-xl border border-border/50">
          <AlertCircle className="mx-auto h-10 w-10 text-muted-foreground mb-4 opacity-50" />
          <p className="text-muted-foreground font-medium">Keng Statistike fonnt. Dir hutt nach keng Resultater.</p>
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card className="bg-card border-border/50 shadow-sm overflow-hidden">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-4">
            <CardTitle className="text-xs uppercase tracking-[0.15em] font-bold text-muted-foreground">Trefferquote pro Maschinn</CardTitle>
          </CardHeader>
          <CardContent className="h-80 pt-6">
            {statsLoading ? <Skeleton className="w-full h-full rounded-md bg-secondary/20" /> : 
             machineData.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={machineData} margin={{ top: 10, right: 10, left: -25, bottom: 0 }}>
                  <XAxis dataKey="name" stroke="hsl(var(--muted-foreground))" fontSize={12} tickLine={false} axisLine={false} fontFamily="var(--font-mono)" />
                  <YAxis stroke="hsl(var(--muted-foreground))" fontSize={12} tickLine={false} axisLine={false} tickFormatter={(v) => `${v}%`} fontFamily="var(--font-mono)" />
                  <Tooltip 
                    cursor={{ fill: 'hsl(var(--secondary))' }}
                    contentStyle={{ backgroundColor: 'hsl(var(--card))', borderColor: 'hsl(var(--border))', borderRadius: '8px', fontFamily: 'var(--font-sans)', fontWeight: 600 }}
                    itemStyle={{ color: 'hsl(var(--primary))' }}
                  />
                  <Bar dataKey="quote" fill="hsl(var(--primary))" radius={[4, 4, 0, 0]} maxBarSize={40} />
                </BarChart>
              </ResponsiveContainer>
            ) : <EmptyChart />}
          </CardContent>
        </Card>

        <Card className="bg-card border-border/50 shadow-sm overflow-hidden">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-4">
            <CardTitle className="text-xs uppercase tracking-[0.15em] font-bold text-muted-foreground">Punkten Trend (Lëscht 10)</CardTitle>
          </CardHeader>
          <CardContent className="h-80 pt-6">
            {verlaufLoading ? <Skeleton className="w-full h-full rounded-md bg-secondary/20" /> :
             trendData.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <AreaChart data={trendData} margin={{ top: 10, right: 10, left: -25, bottom: 0 }}>
                  <defs>
                    <linearGradient id="colorPunkte" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="hsl(var(--primary))" stopOpacity={0.4}/>
                      <stop offset="95%" stopColor="hsl(var(--primary))" stopOpacity={0}/>
                    </linearGradient>
                  </defs>
                  <XAxis dataKey="date" stroke="hsl(var(--muted-foreground))" fontSize={12} tickLine={false} axisLine={false} fontFamily="var(--font-mono)" />
                  <YAxis domain={[0, trendYMax]} stroke="hsl(var(--muted-foreground))" fontSize={12} tickLine={false} axisLine={false} fontFamily="var(--font-mono)" />
                  <Tooltip
                    labelFormatter={(_label, payload) =>
                      (payload?.[0]?.payload as { fullDate?: string })?.fullDate ?? _label
                    }
                    contentStyle={{ backgroundColor: 'hsl(var(--card))', borderColor: 'hsl(var(--border))', borderRadius: '8px', fontFamily: 'var(--font-sans)', fontWeight: 600 }}
                  />
                  <Area type="monotone" dataKey="punkte" stroke="hsl(var(--primary))" strokeWidth={3} fillOpacity={1} fill="url(#colorPunkte)" />
                </AreaChart>
              </ResponsiveContainer>
             ) : <EmptyChart />}
          </CardContent>
        </Card>
      </div>
    </div>
  );
}

function StatCard({ title, value, icon, suffix = "" }: { title: string, value: number | string, icon: React.ReactNode, suffix?: string }) {
  return (
    <Card className="bg-card border-border/50 shadow-sm relative overflow-hidden group">
      <div className="absolute top-0 right-0 p-6 opacity-20 transform translate-x-4 -translate-y-4 transition-transform group-hover:translate-x-2 group-hover:-translate-y-2 duration-300 pointer-events-none">
        {icon}
      </div>
      <CardContent className="p-6 relative z-10">
        <h3 className="text-xs font-bold text-muted-foreground uppercase tracking-widest mb-4">{title}</h3>
        <div className="flex items-baseline gap-2">
          <div className="text-4xl font-black tracking-tight text-foreground">{value}</div>
          {suffix && <div className="text-muted-foreground font-mono text-sm font-bold">{suffix}</div>}
        </div>
      </CardContent>
    </Card>
  );
}

function EmptyChart() {
  return (
    <div className="w-full h-full flex items-center justify-center text-muted-foreground font-medium text-sm">
      Keng Daten disponibel
    </div>
  );
}
