import { useState } from "react";
import { useParams, Link } from "wouter";
import {
  useGetSpieler, getGetSpielerQueryKey,
  useGetStatistik, useGetStatistikVerlauf,
  getGetStatistikQueryKey, getGetStatistikVerlaufQueryKey,
  useGetSpielerErgebnisse, getGetSpielerErgebnisseQueryKey,
} from "@workspace/api-client-react";
import type { ErgebnisWithSpiel } from "@workspace/api-client-react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Skeleton } from "@/components/ui/skeleton";
import { Badge } from "@/components/ui/badge";
import { ArrowLeft, Activity, Target, Trophy, Crosshair, AlertCircle } from "lucide-react";
import {
  BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer,
  AreaChart, Area, RadarChart, Radar, PolarGrid, PolarAngleAxis, PolarRadiusAxis,
} from "recharts";
import { cn } from "@/lib/utils";

type Tab = "dashboard" | "resultater" | "statistiken";

// ─── Main page ────────────────────────────────────────────────────────────────

export default function AdminSpieler() {
  const params = useParams<{ id: string }>();
  const spielerId = Number(params.id);
  const [tab, setTab] = useState<Tab>("dashboard");

  const { data: spielerResp } = useGetSpieler(spielerId, {
    query: { enabled: !!spielerId, queryKey: getGetSpielerQueryKey(spielerId) },
  });
  const spieler = (spielerResp as any)?.spieler ?? spielerResp;

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      {/* Breadcrumb header */}
      <div className="border-b border-border/50 pb-6">
        <Link href="/admin">
          <button className="flex items-center gap-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors mb-4">
            <ArrowLeft size={16} /> Spillerverwaltung
          </button>
        </Link>
        <div className="flex items-center gap-3 flex-wrap">
          <h1 className="text-3xl font-bold tracking-tight">
            {spieler?.name ?? `Spiller #${spielerId}`}
          </h1>
          {spieler?.mitgliedNr && (
            <span className="text-xs font-mono font-black text-muted-foreground bg-secondary/50 border border-border/50 px-2.5 py-1 rounded-lg tracking-widest">
              {spieler.mitgliedNr}
            </span>
          )}
          {spieler?.isAdmin && (
            <Badge variant="secondary" className="text-primary border-primary/30 bg-primary/10 font-bold text-xs">
              Admin
            </Badge>
          )}
        </div>
        {spieler?.email && (
          <p className="text-sm text-muted-foreground mt-1 font-mono">{spieler.email}</p>
        )}
      </div>

      {/* Tab bar */}
      <div className="flex gap-1 bg-secondary/30 p-1 rounded-xl w-fit border border-border/30">
        {(["dashboard", "resultater", "statistiken"] as Tab[]).map((t) => (
          <button
            key={t}
            onClick={() => setTab(t)}
            className={cn(
              "px-5 py-2 rounded-lg text-sm font-bold uppercase tracking-widest transition-all",
              tab === t
                ? "bg-card text-foreground shadow-sm border border-border/50"
                : "text-muted-foreground hover:text-foreground",
            )}
          >
            {t === "dashboard" ? "Dashboard" : t === "resultater" ? "Resultater" : "Statistiken"}
          </button>
        ))}
      </div>

      {tab === "dashboard" && <DashboardTab spielerId={spielerId} />}
      {tab === "resultater" && <ResultaterTab spielerId={spielerId} />}
      {tab === "statistiken" && <StatistikenTab spielerId={spielerId} />}
    </div>
  );
}

// ─── Dashboard Tab ────────────────────────────────────────────────────────────

function DashboardTab({ spielerId }: { spielerId: number }) {
  const { data: stats, isLoading: statsLoading } = useGetStatistik(spielerId, {
    query: { enabled: !!spielerId, queryKey: getGetStatistikQueryKey(spielerId) },
  });
  const { data: verlaufData, isLoading: verlaufLoading } = useGetStatistikVerlauf(
    spielerId, { limit: 10 },
    { query: { enabled: !!spielerId, queryKey: getGetStatistikVerlaufQueryKey(spielerId, { limit: 10 }) } },
  );

  const machineData = stats?.maschinen
    ? Object.entries(stats.maschinen).map(([key, val]: [string, any]) => ({
        name: key,
        quote: Math.round(val.quote),
      }))
    : [];

  const trendData = verlaufData?.verlauf
    ?.slice()
    .reverse()
    .map((v: any) => ({
      ...v,
      date: new Date(v.datum).toLocaleDateString("lb-LU", { day: "2-digit", month: "2-digit" }),
    })) ?? [];

  if (statsLoading) {
    return (
      <div className="space-y-6">
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          {[1,2,3,4].map(i => <Skeleton key={i} className="h-32 rounded-xl bg-card border border-border/50" />)}
        </div>
        <Skeleton className="h-72 rounded-xl bg-card border border-border/50" />
      </div>
    );
  }

  if (!stats) {
    return <EmptyState text="Keng Statistike fonnt. Dëse Spiller huet nach keng Resultater." />;
  }

  return (
    <div className="space-y-6">
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        <StatCard title="Saison Spiller" value={stats.gesamtSpiele} icon={<Activity className="text-chart-2" />} />
        <StatCard title="Duerchschnëtt" value={stats.durchschnitt.toFixed(1)} icon={<Crosshair className="text-chart-3" />} />
        <StatCard title="Trefferquote" value={`${Math.round(stats.trefferquote)}%`} icon={<Target className="text-primary" />} />
        <StatCard title="Bescht Resultat" value={stats.bestPunkte} icon={<Trophy className="text-chart-4" />} />
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card className="bg-card border-border/50">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-4">
            <CardTitle className="text-xs uppercase tracking-[0.15em] font-bold text-muted-foreground">Trefferquote pro Maschinn</CardTitle>
          </CardHeader>
          <CardContent className="h-72 pt-6">
            {machineData.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={machineData} barSize={28}>
                  <XAxis dataKey="name" stroke="hsl(var(--muted-foreground))" fontSize={13} fontWeight={700} />
                  <YAxis domain={[0, 100]} stroke="hsl(var(--muted-foreground))" fontSize={11} tickFormatter={(v) => `${v}%`} />
                  <Tooltip formatter={(v) => [`${v}%`, "Quote"]} contentStyle={{ backgroundColor: "hsl(var(--card))", borderColor: "hsl(var(--border))", borderRadius: "8px", fontWeight: 600 }} />
                  <Bar dataKey="quote" fill="hsl(var(--primary))" radius={[6, 6, 0, 0]} />
                </BarChart>
              </ResponsiveContainer>
            ) : <EmptyState text="Keng Daten" />}
          </CardContent>
        </Card>

        <Card className="bg-card border-border/50">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-4">
            <CardTitle className="text-xs uppercase tracking-[0.15em] font-bold text-muted-foreground">Saison Trend</CardTitle>
          </CardHeader>
          <CardContent className="h-72 pt-6">
            {verlaufLoading ? <Skeleton className="w-full h-full bg-secondary/20 rounded-md" /> :
             trendData.length > 0 ? (
              <ResponsiveContainer width="100%" height="100%">
                <AreaChart data={trendData}>
                  <defs>
                    <linearGradient id="adminTrend" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="hsl(var(--primary))" stopOpacity={0.3} />
                      <stop offset="95%" stopColor="hsl(var(--primary))" stopOpacity={0} />
                    </linearGradient>
                  </defs>
                  <XAxis dataKey="date" stroke="hsl(var(--muted-foreground))" fontSize={11} fontWeight={700} />
                  <YAxis domain={[0, trendData.length > 0 ? Math.max(...trendData.map(v => v.maxPunkte ?? 36)) : 36]} stroke="hsl(var(--muted-foreground))" fontSize={11} />
                  <Tooltip formatter={(v) => [`${v} Punkte`, "Resultat"]} contentStyle={{ backgroundColor: "hsl(var(--card))", borderColor: "hsl(var(--border))", borderRadius: "8px", fontWeight: 600 }} />
                  <Area type="monotone" dataKey="punkte" stroke="hsl(var(--primary))" strokeWidth={3} fill="url(#adminTrend)" dot={{ fill: "hsl(var(--primary))", r: 4 }} />
                </AreaChart>
              </ResponsiveContainer>
            ) : <EmptyState text="Keng Daten" />}
          </CardContent>
        </Card>
      </div>
    </div>
  );
}

// ─── Resultater Tab ───────────────────────────────────────────────────────────

function ResultaterTab({ spielerId }: { spielerId: number }) {
  const { data, isLoading } = useGetSpielerErgebnisse(spielerId, {
    query: { enabled: !!spielerId, queryKey: getGetSpielerErgebnisseQueryKey(spielerId) },
  });

  const grouped: Record<number, ErgebnisWithSpiel[]> = {};
  if (data?.ergebnisse) {
    data.ergebnisse.forEach((e) => {
      if (!grouped[e.spielId]) grouped[e.spielId] = [];
      grouped[e.spielId].push(e);
    });
  }

  const sortedGroups = Object.values(grouped).sort((a, b) =>
    new Date(b[0].spiel.datum).getTime() - new Date(a[0].spiel.datum).getTime(),
  );

  if (isLoading) {
    return <div className="space-y-4">{[1,2,3].map(i => <Skeleton key={i} className="h-48 rounded-xl bg-card border border-border/50" />)}</div>;
  }

  if (sortedGroups.length === 0) {
    return <EmptyState text="Keng Resultater fonnt." />;
  }

  return (
    <div className="space-y-4">
      {sortedGroups.map((group) => {
        const spiel = group[0].spiel;
        const lauf1 = group.filter((e) => e.lauf === 1);
        const lauf2 = group.filter((e) => e.lauf === 2);
        const totalPunkte = group.reduce((s, e) => s + e.punkte, 0);
        const maxPunkte = group.length * 2; // each taube = max 2 pts
        const pct = maxPunkte > 0 ? Math.round((totalPunkte / maxPunkte) * 100) : 0;

        return (
          <div key={group[0].spielId} className="bg-card border border-border/50 rounded-xl overflow-hidden">
            <div className="px-5 py-3 bg-secondary/20 border-b border-border/50 flex items-center justify-between flex-wrap gap-2">
              <div className="flex items-center gap-3">
                <span className="text-sm font-bold text-foreground">
                  {new Date(spiel.datum).toLocaleDateString("lb-LU", { day: "2-digit", month: "long", year: "numeric" })}
                </span>
                <Badge variant="outline" className="text-xs font-bold tracking-widest">{spiel.modus}</Badge>
              </div>
              <div className="flex items-center gap-2">
                <div className={cn(
                  "text-lg font-black font-mono tracking-tight",
                  pct >= 80 ? "text-green-400" : pct >= 60 ? "text-primary" : "text-muted-foreground"
                )}>
                  {totalPunkte} <span className="text-muted-foreground/50 text-sm font-bold">/ {maxPunkte}</span>
                </div>
              </div>
            </div>
            <div className="p-5 grid md:grid-cols-2 gap-6">
              {[{ label: "Lauf 1", ergebnisse: lauf1 }, { label: "Lauf 2", ergebnisse: lauf2 }].map(({ label, ergebnisse }) => (
                ergebnisse.length > 0 && (
                  <div key={label}>
                    <p className="text-[10px] font-black uppercase tracking-[0.2em] text-muted-foreground mb-3">{label} — {ergebnisse.reduce((s, e) => s + e.punkte, 0)} Punkte</p>
                    <div className="grid grid-cols-9 gap-1">
                      {ergebnisse
                        .slice()
                        .sort((a, b) => a.taube - b.taube)
                        .map((e) => (
                          <div key={`${e.lauf}-${e.taube}`}
                            className={cn(
                              "flex flex-col items-center justify-center rounded-lg p-2 border text-center",
                              e.punkte === 2 ? "bg-primary/10 border-primary/30 text-primary" :
                              e.punkte === 1 ? "bg-amber-500/10 border-amber-500/30 text-amber-400" :
                              "bg-secondary/30 border-border/30 text-muted-foreground/50"
                            )}>
                            <span className="text-[10px] font-black">{e.maschine}</span>
                            <span className="text-xs font-black mt-0.5">{e.punkte}</span>
                          </div>
                        ))}
                    </div>
                  </div>
                )
              ))}
            </div>
          </div>
        );
      })}
    </div>
  );
}

// ─── Statistiken Tab ──────────────────────────────────────────────────────────

function StatistikenTab({ spielerId }: { spielerId: number }) {
  const { data: stats, isLoading } = useGetStatistik(spielerId, {
    query: { enabled: !!spielerId, queryKey: getGetStatistikQueryKey(spielerId) },
  });

  const machineData = stats?.maschinen
    ? Object.entries(stats.maschinen).map(([key, val]: [string, any]) => ({
        name: key,
        short: key,
        quote: Math.round(val.quote),
        treffer: val.treffer,
        versuche: val.versuche,
      }))
    : [];

  if (isLoading) {
    return <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
      <Skeleton className="h-80 rounded-xl bg-card border border-border/50" />
      <Skeleton className="h-80 rounded-xl bg-card border border-border/50" />
    </div>;
  }

  if (!stats || machineData.length === 0) {
    return <EmptyState text="Keng Statistike fonnt." />;
  }

  return (
    <div className="space-y-6">
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card className="bg-card border-border/50">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-4">
            <CardTitle className="text-xs uppercase tracking-widest font-bold text-muted-foreground">Trefferquote pro Maschinn</CardTitle>
          </CardHeader>
          <CardContent className="h-80 pt-6">
            <ResponsiveContainer width="100%" height="100%">
              <BarChart data={machineData} barSize={28}>
                <XAxis dataKey="name" stroke="hsl(var(--muted-foreground))" fontSize={13} fontWeight={700} />
                <YAxis domain={[0, 100]} stroke="hsl(var(--muted-foreground))" fontSize={11} tickFormatter={(v) => `${v}%`} />
                <Tooltip formatter={(v) => [`${v}%`, "Quote"]} contentStyle={{ backgroundColor: "hsl(var(--card))", borderColor: "hsl(var(--border))", borderRadius: "8px", fontWeight: 600 }} />
                <Bar dataKey="quote" fill="hsl(var(--primary))" radius={[6, 6, 0, 0]} />
              </BarChart>
            </ResponsiveContainer>
          </CardContent>
        </Card>

        <Card className="bg-card border-border/50">
          <CardHeader className="bg-secondary/20 border-b border-border/50 pb-4">
            <CardTitle className="text-xs uppercase tracking-widest font-bold text-muted-foreground">Radar Analyse</CardTitle>
          </CardHeader>
          <CardContent className="h-80 pt-6">
            <ResponsiveContainer width="100%" height="100%">
              <RadarChart cx="50%" cy="50%" outerRadius="65%" data={machineData}>
                <PolarGrid stroke="hsl(var(--border))" strokeDasharray="3 3" />
                <PolarAngleAxis dataKey="short" stroke="hsl(var(--muted-foreground))" fontSize={14} fontWeight={900} />
                <PolarRadiusAxis angle={90} domain={[0, 100]} tick={false} axisLine={false} />
                <Radar name="Quote" dataKey="quote" stroke="hsl(var(--primary))" strokeWidth={3} fill="hsl(var(--primary))" fillOpacity={0.25} />
                <Tooltip contentStyle={{ backgroundColor: "hsl(var(--card))", borderColor: "hsl(var(--border))", borderRadius: "8px", fontWeight: 600 }} />
              </RadarChart>
            </ResponsiveContainer>
          </CardContent>
        </Card>
      </div>

      <Card className="bg-card border-border/50">
        <CardHeader className="bg-secondary/20 border-b border-border/50 pb-4">
          <CardTitle className="text-xs uppercase tracking-widest font-bold text-muted-foreground">Detailer pro Maschinn</CardTitle>
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
                  <div className="text-xs font-mono text-muted-foreground font-bold mt-1">{d.treffer} / {d.versuche}</div>
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}

// ─── Shared helpers ───────────────────────────────────────────────────────────

function StatCard({ title, value, icon, suffix }: { title: string; value: any; icon: React.ReactNode; suffix?: string }) {
  return (
    <Card className="bg-card border-border/50 shadow-sm overflow-hidden">
      <CardContent className="p-6">
        <div className="flex justify-between items-start">
          <div>
            <p className="text-xs uppercase tracking-[0.15em] font-bold text-muted-foreground mb-2">{title}</p>
            <div className="flex items-baseline gap-1.5">
              <p className="text-3xl font-black tracking-tight">{value}</p>
              {suffix && <span className="text-sm font-bold text-muted-foreground">{suffix}</span>}
            </div>
          </div>
          <div className="w-10 h-10 rounded-xl bg-secondary/50 border border-border/50 flex items-center justify-center">{icon}</div>
        </div>
      </CardContent>
    </Card>
  );
}

function EmptyState({ text }: { text: string }) {
  return (
    <div className="p-12 text-center bg-card rounded-xl border border-border/50">
      <AlertCircle className="mx-auto h-10 w-10 text-muted-foreground mb-4 opacity-50" />
      <p className="text-muted-foreground font-medium">{text}</p>
    </div>
  );
}
