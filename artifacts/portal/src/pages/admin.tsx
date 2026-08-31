import { useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { useToast } from "@/hooks/use-toast";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Badge } from "@/components/ui/badge";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter, DialogDescription } from "@/components/ui/dialog";
import { Skeleton } from "@/components/ui/skeleton";
import { useLocation } from "wouter";
import { Plus, Pencil, Trash2, KeyRound, Shield, CheckCircle2, XCircle, Eye, Search, RotateCcw, Database } from "lucide-react";

// ─── Types ────────────────────────────────────────────────────────────────────

interface AdminPlayer {
  id: number;
  name: string;
  email: string | null;
  mitgliedNr: string | null;
  aktiv: boolean;
  portalAktiv: boolean;
  isAdmin: boolean;
  createdAt: string;
  anzahlSpiele: number;
  durchschnitt: number;
  bestPunkte: number;
}

// ─── API helper ───────────────────────────────────────────────────────────────

function useAdminFetch() {
  const token = useAuthStore((s) => s.token);
  return async (path: string, options?: RequestInit) => {
    const res = await fetch(path, {
      ...options,
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${token}`,
        ...options?.headers,
      },
    });
    const ct = res.headers.get("content-type") ?? "";
    const data = ct.includes("application/json") ? await res.json() : null;
    if (!res.ok) throw new Error(data?.error ?? `HTTP ${res.status}`);
    return data;
  };
}

// ─── Form defaults ────────────────────────────────────────────────────────────

const emptyForm = { name: "", email: "", mitgliedNr: "", portalAktiv: false, isAdmin: false, passwort: "" };
type PlayerForm = typeof emptyForm;

// ─── Main component ───────────────────────────────────────────────────────────

export default function Admin() {
  const qc = useQueryClient();
  const apiFetch = useAdminFetch();
  const { toast } = useToast();
  const [, navigate] = useLocation();
  const [addOpen, setAddOpen] = useState(false);
  const [editPlayer, setEditPlayer] = useState<AdminPlayer | null>(null);
  const [deletePlayer, setDeletePlayer] = useState<AdminPlayer | null>(null);
  const [pwdPlayer, setPwdPlayer] = useState<AdminPlayer | null>(null);
  const [purgeMode, setPurgeMode] = useState<"day" | "all" | null>(null);
  const [purgeDate, setPurgeDate] = useState(() => new Date().toISOString().slice(0, 10));
  const [purgeConfirm, setPurgeConfirm] = useState("");
  const [addForm, setAddForm] = useState<PlayerForm>(emptyForm);
  const [editForm, setEditForm] = useState<Omit<PlayerForm, "passwort">>({ name: "", email: "", mitgliedNr: "", portalAktiv: false, isAdmin: false });
  const [newPwd, setNewPwd] = useState("");
  const [search, setSearch] = useState("");

  // ── Queries ──────────────────────────────────────────────────────────────

  const { data, isLoading } = useQuery<{ spieler: AdminPlayer[] }>({
    queryKey: ["admin-spieler"],
    queryFn: () => apiFetch("/api/admin/spieler"),
  });

  // ── Filtered list ─────────────────────────────────────────────────────────

  const filtered = (data?.spieler ?? []).filter((p) => {
    if (!search.trim()) return true;
    const q = search.toLowerCase();
    return (
      p.name.toLowerCase().includes(q) ||
      (p.email ?? "").toLowerCase().includes(q) ||
      (p.mitgliedNr ?? "").toLowerCase().includes(q)
    );
  });

  // ── Mutations ─────────────────────────────────────────────────────────────

  const invalidate = () => qc.invalidateQueries({ queryKey: ["admin-spieler"] });

  const createMut = useMutation({
    mutationFn: ({ mitgliedNr: _nr, ...body }: PlayerForm) => apiFetch("/api/admin/spieler", { method: "POST", body: JSON.stringify(body) }),
    onSuccess: () => { invalidate(); setAddOpen(false); setAddForm(emptyForm); toast({ title: "Spieler erstellt" }); },
    onError: (e: Error) => toast({ title: "Fehler", description: e.message, variant: "destructive" }),
  });

  const updateMut = useMutation({
    mutationFn: ({ id, body }: { id: number; body: Omit<PlayerForm, "passwort"> }) =>
      apiFetch(`/api/admin/spieler/${id}`, { method: "PUT", body: JSON.stringify(body) }),
    onSuccess: () => { invalidate(); setEditPlayer(null); toast({ title: "Spieler aktualisiert" }); },
    onError: (e: Error) => toast({ title: "Fehler", description: e.message, variant: "destructive" }),
  });

  const deleteMut = useMutation({
    mutationFn: (id: number) => apiFetch(`/api/admin/spieler/${id}`, { method: "DELETE" }),
    onSuccess: () => { invalidate(); setDeletePlayer(null); toast({ title: "Spieler deaktiviert", description: "Historische Spiele, Käufe und Abrechnungen bleiben erhalten." }); },
    onError: (e: Error) => toast({ title: "Fehler", description: e.message, variant: "destructive" }),
  });

  const reactivateMut = useMutation({
    mutationFn: (id: number) => apiFetch(`/api/admin/spieler/${id}/reactivate`, { method: "POST" }),
    onSuccess: () => { invalidate(); toast({ title: "Spieler reaktiviert" }); },
    onError: (e: Error) => toast({ title: "Fehler", description: e.message, variant: "destructive" }),
  });

  const purgeMut = useMutation({
    mutationFn: ({ mode, datum }: { mode: "day" | "all"; datum?: string }) =>
      apiFetch("/api/admin/purge", {
        method: "POST",
        body: JSON.stringify(mode === "day"
          ? { mode, datum, confirmation: "PURGE_DAY" }
          : { mode, confirmation: "PURGE_ALL" }),
      }),
    onSuccess: (result: any) => {
      qc.invalidateQueries();
      setPurgeMode(null);
      setPurgeConfirm("");
      const count = result.counts;
      toast({
        title: result.mode === "day" ? "Tag bereinigt" : "Globale Daten bereinigt",
        description: `${count.games} Spiele, ${count.credits} Kreditbuchungen, ${count.sales} Verkäufe und ${count.billPayments} Zahlungen entfernt${count.players ? `; ${count.players} Spieler gelöscht` : ""}.`,
      });
    },
    onError: (e: Error) => toast({ title: "Bereinigung fehlgeschlagen", description: e.message, variant: "destructive" }),
  });

  const pwdMut = useMutation({
    mutationFn: ({ id, pwd }: { id: number; pwd: string }) =>
      apiFetch(`/api/admin/spieler/${id}/passwort`, { method: "PUT", body: JSON.stringify({ neuesPasswort: pwd }) }),
    onSuccess: () => { setPwdPlayer(null); setNewPwd(""); toast({ title: "Passwort geändert" }); },
    onError: (e: Error) => toast({ title: "Fehler", description: e.message, variant: "destructive" }),
  });

  // ── Open edit dialog ──────────────────────────────────────────────────────

  const openEdit = (p: AdminPlayer) => {
    setEditForm({ name: p.name, email: p.email ?? "", mitgliedNr: p.mitgliedNr ?? "", portalAktiv: p.portalAktiv, isAdmin: p.isAdmin });
    setEditPlayer(p);
  };

  // ── Render ────────────────────────────────────────────────────────────────

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      <header className="flex items-start justify-between border-b border-border/50 pb-6 gap-4">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Spielerverwaltung</h1>
          <p className="text-muted-foreground mt-2 text-sm font-medium">Spieler erstellen, bearbeiten, deaktivieren und Passwörter festlegen.</p>
        </div>
        <div className="flex items-center gap-2 shrink-0">
          <button
            onClick={() => { setPurgeMode("day"); setPurgeConfirm(""); }}
            className="flex items-center gap-2 px-4 py-2.5 border border-destructive/50 text-destructive hover:bg-destructive/10 text-sm font-bold rounded-lg transition-colors"
          >
            <Database size={16} /> Daten bereinigen
          </button>
          <button
            onClick={() => setAddOpen(true)}
            className="flex items-center gap-2 px-4 py-2.5 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors"
          >
            <Plus size={16} /> Neuer Spieler
          </button>
        </div>
      </header>

      {/* ── Search ───────────────────────────────────────────────────────── */}
      <div className="relative">
        <Search size={15} className="absolute left-3.5 top-1/2 -translate-y-1/2 text-muted-foreground pointer-events-none" />
        <input
          value={search}
          onChange={(e) => setSearch(e.target.value)}

          placeholder="Spieler suchen… (Name, E-Mail, Mitgliedsnummer)"
          className="w-full bg-card border border-border/60 rounded-lg pl-9 pr-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
        />
      </div>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-3">
            {[1,2,3,4,5].map(i => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}
          </div>
        ) : (
          <div className="overflow-x-auto">
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Name</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Email</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Mitgliedsnummer</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Portal</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Status</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Admin</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Spiele</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Ø / 36</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Bestes Ergebnis</TableHead>
                  <TableHead className="w-28"></TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {filtered.length === 0 && (
                  <TableRow>
                    <TableCell colSpan={10} className="text-center py-10 text-muted-foreground font-medium">
                      {search ? `Keine Ergebnisse für "${search}"` : "Keine Spieler gefunden"}
                    </TableCell>
                  </TableRow>
                )}
                {filtered.map((p) => (
                  <TableRow key={p.id} className={`border-border/30 hover:bg-secondary/20 transition-colors ${p.aktiv ? "" : "opacity-60"}`}>
                    <TableCell className="font-bold text-foreground">{p.name}</TableCell>
                    <TableCell className="text-muted-foreground text-sm font-mono">{p.email || <span className="opacity-30">–</span>}</TableCell>
                    <TableCell className="text-muted-foreground text-sm font-mono">{p.mitgliedNr || <span className="opacity-30">–</span>}</TableCell>
                    <TableCell className="text-center">
                      {p.portalAktiv
                        ? <CheckCircle2 size={16} className="text-green-500 mx-auto" />
                        : <XCircle size={16} className="text-muted-foreground/40 mx-auto" />}
                    </TableCell>
                    <TableCell className="text-center">
                      <Badge variant={p.aktiv ? "secondary" : "outline"} className={p.aktiv ? "text-green-500 border-green-500/30 bg-green-500/10" : "text-muted-foreground"}>
                        {p.aktiv ? "Aktiv" : "Inaktiv"}
                      </Badge>
                    </TableCell>
                    <TableCell className="text-center">
                      {p.isAdmin ? <Shield size={16} className="text-primary mx-auto" /> : <span className="text-muted-foreground/30">–</span>}
                    </TableCell>
                    <TableCell className="text-right font-mono text-muted-foreground">{p.anzahlSpiele}</TableCell>
                    <TableCell className="text-right font-mono font-bold">{p.durchschnitt.toFixed(1)}</TableCell>
                    <TableCell className="text-right font-mono font-bold text-primary">{p.bestPunkte > 0 ? p.bestPunkte : <span className="text-muted-foreground/40">–</span>}</TableCell>
                    <TableCell>
                      <div className="flex items-center justify-end gap-1">
                         <IconBtn title="Profil anzeigen" onClick={() => navigate(`/admin/spieler/${p.id}`)}><Eye size={14} /></IconBtn>
                         <IconBtn title="Bearbeiten" onClick={() => openEdit(p)}><Pencil size={14} /></IconBtn>
                         <IconBtn title="Passwort" onClick={() => { setPwdPlayer(p); setNewPwd(""); }}><KeyRound size={14} /></IconBtn>
                         {p.aktiv && !p.isAdmin && (
                           <IconBtn title="Deaktivieren" danger onClick={() => setDeletePlayer(p)}><Trash2 size={14} /></IconBtn>
                         )}
                         {!p.aktiv && (
                           <IconBtn title="Reaktivieren" onClick={() => reactivateMut.mutate(p.id)}><RotateCcw size={14} /></IconBtn>
                         )}
                      </div>
                    </TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </div>
        )}
      </div>

      {/* ── Add Dialog ────────────────────────────────────────────────────── */}
      <Dialog open={addOpen} onOpenChange={setAddOpen}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Neuen Spieler erstellen</DialogTitle>
            <DialogDescription>Füllen Sie die Felder aus. Ein Passwort wird nur für den Portalzugang benötigt.</DialogDescription>
          </DialogHeader>
          <PlayerFormFields form={addForm} onChange={setAddForm} showPassword isEdit={false} />
          <DialogFooter>
            <button onClick={() => setAddOpen(false)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Abbrechen</button>
            <button
              onClick={() => createMut.mutate(addForm)}
              disabled={!addForm.name || createMut.isPending}
              className="px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50"
            >
              {createMut.isPending ? "Erstellen…" : "Erstellen"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* ── Edit Dialog ───────────────────────────────────────────────────── */}
      <Dialog open={!!editPlayer} onOpenChange={(o) => !o && setEditPlayer(null)}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Spieler bearbeiten</DialogTitle>
            <DialogDescription>{editPlayer?.name}</DialogDescription>
          </DialogHeader>
          <PlayerFormFields form={editForm} onChange={setEditForm} showPassword={false} isEdit />
          <DialogFooter>
            <button onClick={() => setEditPlayer(null)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Abbrechen</button>
            <button
              onClick={() => editPlayer && updateMut.mutate({ id: editPlayer.id, body: editForm })}
              disabled={!editForm.name || updateMut.isPending}
              className="px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50"
            >
              {updateMut.isPending ? "Wird gespeichert…" : "Speichern"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* ── Deactivate Dialog ─────────────────────────────────────────────── */}
      <Dialog open={!!deletePlayer} onOpenChange={(o) => !o && setDeletePlayer(null)}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Spieler deaktivieren</DialogTitle>
            <DialogDescription>
              Möchten Sie <strong>{deletePlayer?.name}</strong> deaktivieren? Der Spieler kann nicht mehr ausgewählt oder angemeldet werden. Alle Spiele, Käufe, Abrechnungen und Statistiken bleiben erhalten und der Spieler kann später reaktiviert werden.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <button onClick={() => setDeletePlayer(null)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Abbrechen</button>
            <button
              onClick={() => deletePlayer && deleteMut.mutate(deletePlayer.id)}
              disabled={deleteMut.isPending}
              className="px-4 py-2 bg-destructive hover:bg-destructive/90 text-destructive-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50"
            >
              {deleteMut.isPending ? "Wird deaktiviert…" : "Spieler deaktivieren"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* ── Purge Dialog ──────────────────────────────────────────────────── */}
      <Dialog open={purgeMode !== null} onOpenChange={(o) => { if (!o) { setPurgeMode(null); setPurgeConfirm(""); } }}>
        <DialogContent className="sm:max-w-lg">
          <DialogHeader>
            <DialogTitle>Daten bereinigen</DialogTitle>
            <DialogDescription>
              Nur operative Spieler-, Spiel-, Kauf- und Abrechnungsdaten werden entfernt. Produkte, Preise, API-Schlüssel und Einstellungen bleiben unverändert.
            </DialogDescription>
          </DialogHeader>

          <div className="grid grid-cols-2 gap-3">
            <button
              type="button"
              onClick={() => { setPurgeMode("day"); setPurgeConfirm(""); }}
              className={`rounded-lg border p-3 text-left transition-colors ${purgeMode === "day" ? "border-primary bg-primary/10" : "border-border hover:bg-secondary/30"}`}
            >
              <span className="block text-sm font-bold">Bestimmten Tag löschen</span>
              <span className="block text-xs text-muted-foreground mt-1">Entfernt Aktivität des gewählten Tages, aber keine Spieler.</span>
            </button>
            <button
              type="button"
              onClick={() => { setPurgeMode("all"); setPurgeConfirm(""); }}
              className={`rounded-lg border p-3 text-left transition-colors ${purgeMode === "all" ? "border-destructive bg-destructive/10" : "border-border hover:bg-secondary/30"}`}
            >
              <span className="block text-sm font-bold text-destructive">Alles global löschen</span>
              <span className="block text-xs text-muted-foreground mt-1">Entfernt alle Aktivitäten und alle Nicht-Admin-Spieler.</span>
            </button>
          </div>

          {purgeMode === "day" && (
            <FormField label="Tag" id="purge-date">
              <input
                id="purge-date"
                type="date"
                value={purgeDate}
                onChange={(e) => setPurgeDate(e.target.value)}
                className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40"
              />
            </FormField>
          )}

          <div className="rounded-lg border border-destructive/40 bg-destructive/10 p-3 text-sm">
            <strong className="text-destructive">Unwiderruflich:</strong>{" "}
            {purgeMode === "day"
              ? `Alle Spiele, Käufe, Kreditbuchungen und Zahlungen vom ${purgeDate || "gewählten Tag"} werden endgültig entfernt.`
              : "Alle Spiele, Käufe, Kreditbuchungen, Zahlungen und Nicht-Admin-Spieler werden endgültig entfernt. Administratorkonten bleiben bestehen."}
          </div>

          <FormField label={`Zur Bestätigung „${purgeMode === "day" ? "TAG LÖSCHEN" : "ALLES LÖSCHEN"}“ eingeben`} id="purge-confirm">
            <input
              id="purge-confirm"
              value={purgeConfirm}
              onChange={(e) => setPurgeConfirm(e.target.value)}
              className="w-full bg-background border border-destructive/50 rounded-lg px-4 py-2.5 text-sm font-bold focus:outline-none focus:ring-2 focus:ring-destructive/40"
            />
          </FormField>

          <DialogFooter>
            <button onClick={() => { setPurgeMode(null); setPurgeConfirm(""); }} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Abbrechen</button>
            <button
              onClick={() => purgeMode && purgeMut.mutate({ mode: purgeMode, datum: purgeMode === "day" ? purgeDate : undefined })}
              disabled={purgeMut.isPending || !purgeDate || purgeConfirm !== (purgeMode === "day" ? "TAG LÖSCHEN" : "ALLES LÖSCHEN")}
              className="px-4 py-2 bg-destructive hover:bg-destructive/90 text-destructive-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-40"
            >
              {purgeMut.isPending ? "Wird bereinigt…" : purgeMode === "day" ? "Tag endgültig löschen" : "Alles endgültig löschen"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* ── Password Dialog ───────────────────────────────────────────────── */}
      <Dialog open={!!pwdPlayer} onOpenChange={(o) => !o && setPwdPlayer(null)}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Passwort festlegen</DialogTitle>
            <DialogDescription>Neues Passwort für <strong>{pwdPlayer?.name}</strong>.</DialogDescription>
          </DialogHeader>
          <PwdField value={newPwd} onChange={setNewPwd} />
          <DialogFooter>
            <button onClick={() => setPwdPlayer(null)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Abbrechen</button>
            <button
              onClick={() => pwdPlayer && newPwd.length >= 6 && pwdMut.mutate({ id: pwdPlayer.id, pwd: newPwd })}
              disabled={newPwd.length < 6 || pwdMut.isPending}
              className="px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50"
            >
              {pwdMut.isPending ? "Wird gespeichert…" : "Speichern"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}

// ─── Password field ───────────────────────────────────────────────────────────

function PwdField({ value, onChange }: { value: string; onChange: (v: string) => void }) {
  return (
    <div className="space-y-1.5 py-2">
      <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Neues Passwort</label>
      <input
        type="password" autoComplete="new-password"
        value={value} onChange={(e) => onChange(e.target.value)}
        className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
        placeholder="Mindestens 6 Zeichen"
      />
    </div>
  );
}

// ─── Shared form component ────────────────────────────────────────────────────

function PlayerFormFields({
  form, onChange, showPassword, isEdit = false,
}: {
  form: any;
  onChange: (f: any) => void;
  showPassword: boolean;
  isEdit?: boolean;
}) {
  const set = (k: string, v: any) => onChange((f: any) => ({ ...f, [k]: v }));
  return (
    <div className="space-y-4 py-2">
      <FormField label="Name *" id="name">
        <input
          id="name" value={form.name} onChange={(e) => set("name", e.target.value)} required
          className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
          placeholder="Max Mustermann"
        />
      </FormField>
      <FormField label="Email" id="email">
        <input
          id="email" type="email" value={form.email} onChange={(e) => set("email", e.target.value)}
          className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
          placeholder="max@beispiel.de"
        />
      </FormField>
      {isEdit ? (
        <FormField label="Mitgliedsnummer" id="nr">
          <div className="w-full bg-secondary/30 border border-border/40 rounded-lg px-4 py-2.5 text-sm font-mono font-medium text-muted-foreground select-all">
            {form.mitgliedNr || "–"}
          </div>
        </FormField>
      ) : (
        <div className="flex items-center gap-2 text-xs text-muted-foreground bg-secondary/20 border border-border/30 rounded-lg px-3 py-2">
          <span className="font-mono font-bold text-primary">WLZ###</span>
          <span>Die Mitgliedsnummer wird automatisch vergeben</span>
        </div>
      )}
      {showPassword && (
        <FormField label="Passwort (für den Portalzugang)" id="pwd">
          <input
            id="pwd" type="password" autoComplete="new-password" value={form.passwort}
            onChange={(e) => set("passwort", e.target.value)}
            className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
            placeholder="Mindestens 6 Zeichen"
          />
        </FormField>
      )}
      <div className="flex gap-6 pt-1">
        <Toggle label="Portal aktiv" checked={form.portalAktiv} onToggle={(v) => set("portalAktiv", v)} />
        <Toggle label="Admin" checked={form.isAdmin} onToggle={(v) => set("isAdmin", v)} />
      </div>
    </div>
  );
}

function FormField({ label, id, children }: { label: string; id: string; children: React.ReactNode }) {
  return (
    <div className="space-y-1.5">
      <label htmlFor={id} className="text-xs font-bold uppercase tracking-widest text-muted-foreground">{label}</label>
      {children}
    </div>
  );
}

function Toggle({ label, checked, onToggle }: { label: string; checked: boolean; onToggle: (v: boolean) => void }) {
  return (
    <button type="button" onClick={() => onToggle(!checked)}
      className="flex items-center gap-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">
      <div className={`w-9 h-5 rounded-full transition-colors relative ${checked ? "bg-primary" : "bg-secondary border border-border/60"}`}>
        <span className={`absolute top-0.5 w-4 h-4 rounded-full bg-white shadow transition-all ${checked ? "left-4.5" : "left-0.5"}`} />
      </div>
      {label}
    </button>
  );
}

function IconBtn({ children, onClick, title, danger }: { children: React.ReactNode; onClick: () => void; title: string; danger?: boolean }) {
  return (
    <button
      type="button" title={title} onClick={onClick}
      className={`p-1.5 rounded-lg transition-colors ${danger ? "hover:bg-destructive/20 hover:text-destructive text-muted-foreground/60" : "hover:bg-secondary text-muted-foreground/60 hover:text-foreground"}`}
    >
      {children}
    </button>
  );
}
