import { useParams, useLocation } from "wouter";
import { useQuery } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { Tabs, TabsList, TabsTrigger, TabsContent } from "@/components/ui/tabs";
import { Skeleton } from "@/components/ui/skeleton";
import { ChevronLeft, Activity, Target, Trophy } from "lucide-react";
import Dashboard from "./dashboard";
import Resultater from "./resultater";
import Statistiken from "./statistiken";

interface AdminPlayer {
  id: number;
  name: string;
  email: string | null;
  mitgliedNr: string | null;
  portalAktiv: boolean;
  isAdmin: boolean;
  createdAt: string;
  anzahlSpiele: number;
  durchschnitt: number;
  bestPunkte: number;
}

export default function AdminSpielrProfil() {
  const { id } = useParams<{ id: string }>();
  const [, setLocation] = useLocation();
  const token = useAuthStore((s) => s.token);
  const spielerId = parseInt(id ?? "0", 10);

  const { data, isLoading } = useQuery<{ spieler: AdminPlayer[] }>({
    queryKey: ["admin-spieler"],
    queryFn: async () => {
      const res = await fetch("/api/admin/spieler", {
        headers: { Authorization: `Bearer ${token}` },
      });
      const json = await res.json();
      if (!res.ok) throw new Error(json.error || `HTTP ${res.status}`);
      return json;
    },
  });

  const player = data?.spieler.find((p) => p.id === spielerId);

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      {/* Breadcrumb */}
      <div className="flex items-center gap-2">
        <button
          onClick={() => setLocation("/admin")}
          className="flex items-center gap-1.5 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors group"
        >
          <ChevronLeft size={16} className="group-hover:-translate-x-0.5 transition-transform" />
          Zréck zur Spillerverwaltung
        </button>
      </div>

      {/* Player header */}
      {isLoading ? (
        <div className="border-b border-border/50 pb-6 space-y-3">
          <Skeleton className="h-9 w-48 rounded-md bg-secondary/30" />
          <Skeleton className="h-5 w-72 rounded-md bg-secondary/20" />
        </div>
      ) : player ? (
        <header className="border-b border-border/50 pb-6">
          <div className="flex flex-col md:flex-row md:items-end justify-between gap-4">
            <div>
              <h1 className="text-3xl font-bold tracking-tight">{player.name}</h1>
              <div className="flex flex-wrap items-center gap-4 mt-2 text-xs font-mono font-bold text-muted-foreground uppercase tracking-widest">
                {player.mitgliedNr && <span>ID: {player.mitgliedNr}</span>}
                {player.email && <span>{player.email}</span>}
              </div>
            </div>
            <div className="flex gap-6 shrink-0">
              <StatPill label="Spiller" value={player.anzahlSpiele} icon={<Activity size={14} />} />
              <StatPill label="Ø / 36" value={player.durchschnitt.toFixed(1)} icon={<Target size={14} />} />
              <StatPill label="Bescht" value={player.bestPunkte > 0 ? player.bestPunkte : "–"} icon={<Trophy size={14} />} />
            </div>
          </div>
        </header>
      ) : (
        <div className="border-b border-border/50 pb-6">
          <p className="text-muted-foreground font-medium">Spiller net fonnt.</p>
        </div>
      )}

      {/* Tabs */}
      {spielerId > 0 && (
        <Tabs defaultValue="dashboard">
          <TabsList className="bg-secondary/30 border border-border/50 h-10">
            <TabsTrigger value="dashboard" className="text-xs font-bold uppercase tracking-widest">Dashboard</TabsTrigger>
            <TabsTrigger value="resultater" className="text-xs font-bold uppercase tracking-widest">Resultater</TabsTrigger>
            <TabsTrigger value="statistiken" className="text-xs font-bold uppercase tracking-widest">Statistiken</TabsTrigger>
          </TabsList>

          <TabsContent value="dashboard" className="mt-6">
            <Dashboard spielerId={spielerId} playerName={player?.name} />
          </TabsContent>

          <TabsContent value="resultater" className="mt-6">
            <Resultater spielerId={spielerId} />
          </TabsContent>

          <TabsContent value="statistiken" className="mt-6">
            <Statistiken spielerId={spielerId} />
          </TabsContent>
        </Tabs>
      )}
    </div>
  );
}

function StatPill({ label, value, icon }: { label: string; value: string | number; icon: React.ReactNode }) {
  return (
    <div className="flex flex-col items-center gap-1">
      <div className="flex items-center gap-1 text-muted-foreground">{icon}
        <span className="text-[10px] font-bold uppercase tracking-widest">{label}</span>
      </div>
      <div className="text-xl font-black tracking-tight text-foreground">{value}</div>
    </div>
  );
}
