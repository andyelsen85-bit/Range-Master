import { useState } from "react";
import { useParams, useLocation } from "wouter";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { useToast } from "@/hooks/use-toast";
import { Tabs, TabsList, TabsTrigger, TabsContent } from "@/components/ui/tabs";
import { Skeleton } from "@/components/ui/skeleton";
import { ChevronLeft, Activity, Target, Trophy, Pencil, Check, X } from "lucide-react";
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
  const { toast } = useToast();
  const qc = useQueryClient();
  const spielerId = parseInt(id ?? "0", 10);

  const [editing, setEditing] = useState(false);
  const [editEmail, setEditEmail] = useState("");
  const [editAktiv, setEditAktiv] = useState(false);

  const apiFetch = async (path: string, options?: RequestInit) => {
    const res = await fetch(path, {
      ...options,
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${token}`,
        ...options?.headers,
      },
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
    return data;
  };

  const { data, isLoading } = useQuery<{ spieler: AdminPlayer[] }>({
    queryKey: ["admin-spieler"],
    queryFn: () => apiFetch("/api/admin/spieler"),
  });

  const player = data?.spieler.find((p) => p.id === spielerId);

  const updateMut = useMutation({
    mutationFn: (body: { email: string; portalAktiv: boolean }) =>
      apiFetch(`/api/admin/spieler/${spielerId}`, {
        method: "PUT",
        body: JSON.stringify({
          name: player?.name ?? "",
          email: body.email,
          mitgliedNr: player?.mitgliedNr ?? "",
          portalAktiv: body.portalAktiv,
          isAdmin: player?.isAdmin ?? false,
        }),
      }),
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["admin-spieler"] });
      setEditing(false);
      toast({ title: "Spieler aktualisiert" });
    },
    onError: (e: Error) =>
      toast({ title: "Fehler", description: e.message, variant: "destructive" }),
  });

  const openEdit = () => {
    setEditEmail(player?.email ?? "");
    setEditAktiv(player?.portalAktiv ?? false);
    setEditing(true);
  };

  const cancelEdit = () => setEditing(false);

  const saveEdit = () => {
    updateMut.mutate({ email: editEmail, portalAktiv: editAktiv });
  };

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      {/* Breadcrumb */}
      <div className="flex items-center gap-2">
        <button
          onClick={() => setLocation("/admin")}
          className="flex items-center gap-1.5 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors group"
        >
          <ChevronLeft size={16} className="group-hover:-translate-x-0.5 transition-transform" />
          Zurück zur Spielerverwaltung
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
          <div className="flex flex-col md:flex-row md:items-start justify-between gap-6">
            <div className="flex-1 space-y-4">
              <div>
                <h1 className="text-3xl font-bold tracking-tight">{player.name}</h1>
                {player.mitgliedNr && (
                  <p className="text-xs font-mono font-bold text-muted-foreground uppercase tracking-widest mt-1">
                    ID: {player.mitgliedNr}
                  </p>
                )}
              </div>

              {/* Inline edit form */}
              {editing ? (
                <div className="space-y-3 bg-secondary/20 border border-border/50 rounded-xl p-4">
                  <div className="space-y-1.5">
                    <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">
                      Email
                    </label>
                    <input
                      type="email"
                      value={editEmail}
                      onChange={(e) => setEditEmail(e.target.value)}

                      placeholder="max@beispill.lu"
                      className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
                    />
                  </div>

                  <div className="flex items-center justify-between">
                    <button
                      type="button"
                      onClick={() => setEditAktiv(!editAktiv)}
                      className="flex items-center gap-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors"
                    >
                      <div
                        className={`w-9 h-5 rounded-full transition-colors relative ${
                          editAktiv ? "bg-primary" : "bg-secondary border border-border/60"
                        }`}
                      >
                        <span
                          className={`absolute top-0.5 w-4 h-4 rounded-full bg-white shadow transition-all ${
                            editAktiv ? "left-4.5" : "left-0.5"
                          }`}
                        />
                      </div>
                      Portal aktiv
                    </button>

                    <div className="flex gap-2">
                      <button
                        onClick={cancelEdit}
                        className="flex items-center gap-1.5 px-3 py-1.5 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors"
                      >
                        <X size={14} /> Abbrechen
                      </button>
                      <button
                        onClick={saveEdit}
                        disabled={updateMut.isPending}
                        className="flex items-center gap-1.5 px-4 py-1.5 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50"
                      >
                        <Check size={14} />
                        {updateMut.isPending ? "Wird gespeichert…" : "Speichern"}
                      </button>
                    </div>
                  </div>
                </div>
              ) : (
                <div className="flex items-center gap-4">
                  <div className="flex flex-wrap items-center gap-3 text-sm text-muted-foreground">
                    {player.email ? (
                      <span className="font-mono">{player.email}</span>
                    ) : (
                       <span className="opacity-40 italic">Keine E-Mail-Adresse</span>
                    )}
                    <span
                      className={`px-2 py-0.5 rounded-full text-xs font-bold uppercase tracking-widest ${
                        player.portalAktiv
                          ? "bg-green-500/15 text-green-500"
                          : "bg-secondary text-muted-foreground"
                      }`}
                    >
                      {player.portalAktiv ? "Aktiv" : "Inaktiv"}
                    </span>
                  </div>
                  <button
                    onClick={openEdit}
                    className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-bold text-muted-foreground hover:text-foreground hover:bg-secondary/60 rounded-lg transition-colors"
                  >
                    <Pencil size={13} /> Bearbeiten
                  </button>
                </div>
              )}
            </div>

            {/* Stats */}
            <div className="flex gap-6 shrink-0">
              <StatPill label="Spiele" value={player.anzahlSpiele} icon={<Activity size={14} />} />
              <StatPill label="Ø / 36" value={player.durchschnitt.toFixed(1)} icon={<Target size={14} />} />
              <StatPill
                 label="Bestes Ergebnis"
                value={player.bestPunkte > 0 ? player.bestPunkte : "–"}
                icon={<Trophy size={14} />}
              />
            </div>
          </div>
        </header>
      ) : (
        <div className="border-b border-border/50 pb-6">
          <p className="text-muted-foreground font-medium">Spieler nicht gefunden.</p>
        </div>
      )}

      {/* Tabs */}
      {spielerId > 0 && (
        <Tabs defaultValue="dashboard">
          <TabsList className="bg-secondary/30 border border-border/50 h-10">
            <TabsTrigger value="dashboard" className="text-xs font-bold uppercase tracking-widest">
              Dashboard
            </TabsTrigger>
            <TabsTrigger value="resultater" className="text-xs font-bold uppercase tracking-widest">
              Resultater
            </TabsTrigger>
            <TabsTrigger value="statistiken" className="text-xs font-bold uppercase tracking-widest">
              Statistiken
            </TabsTrigger>
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

function StatPill({
  label,
  value,
  icon,
}: {
  label: string;
  value: string | number;
  icon: React.ReactNode;
}) {
  return (
    <div className="flex flex-col items-center gap-1">
      <div className="flex items-center gap-1 text-muted-foreground">
        {icon}
        <span className="text-[10px] font-bold uppercase tracking-widest">{label}</span>
      </div>
      <div className="text-xl font-black tracking-tight text-foreground">{value}</div>
    </div>
  );
}
