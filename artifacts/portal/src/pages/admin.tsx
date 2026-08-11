import { useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { useToast } from "@/hooks/use-toast";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Badge } from "@/components/ui/badge";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter, DialogDescription } from "@/components/ui/dialog";
import { Skeleton } from "@/components/ui/skeleton";
import { useLocation } from "wouter";
import { Plus, Pencil, Trash2, KeyRound, Shield, CheckCircle2, XCircle, Eye, Search } from "lucide-react";

// ─── Types ────────────────────────────────────────────────────────────────────

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
    onSuccess: () => { invalidate(); setAddOpen(false); setAddForm(emptyForm); toast({ title: "Spiller erstallt" }); },
    onError: (e: Error) => toast({ title: "Feeler", description: e.message, variant: "destructive" }),
  });

  const updateMut = useMutation({
    mutationFn: ({ id, body }: { id: number; body: Omit<PlayerForm, "passwort"> }) =>
      apiFetch(`/api/admin/spieler/${id}`, { method: "PUT", body: JSON.stringify(body) }),
    onSuccess: () => { invalidate(); setEditPlayer(null); toast({ title: "Spiller aktualiséiert" }); },
    onError: (e: Error) => toast({ title: "Feeler", description: e.message, variant: "destructive" }),
  });

  const deleteMut = useMutation({
    mutationFn: (id: number) => apiFetch(`/api/admin/spieler/${id}`, { method: "DELETE" }),
    onSuccess: () => { invalidate(); setDeletePlayer(null); toast({ title: "Spiller geläscht" }); },
    onError: (e: Error) => toast({ title: "Feeler", description: e.message, variant: "destructive" }),
  });

  const pwdMut = useMutation({
    mutationFn: ({ id, pwd }: { id: number; pwd: string }) =>
      apiFetch(`/api/admin/spieler/${id}/passwort`, { method: "PUT", body: JSON.stringify({ neuesPasswort: pwd }) }),
    onSuccess: () => { setPwdPlayer(null); setNewPwd(""); toast({ title: "Passwuert geännert" }); },
    onError: (e: Error) => toast({ title: "Feeler", description: e.message, variant: "destructive" }),
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
          <h1 className="text-3xl font-bold tracking-tight">Spillerverwaltung</h1>
          <p className="text-muted-foreground mt-2 text-sm font-medium">Spiller erstellen, änneren, läschen a Passwierder setzen.</p>
        </div>
        <button
          onClick={() => setAddOpen(true)}
          className="flex items-center gap-2 px-4 py-2.5 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors shrink-0"
        >
          <Plus size={16} /> Neit Spiller
        </button>
      </header>

      {/* ── Search ───────────────────────────────────────────────────────── */}
      <div className="relative">
        <Search size={15} className="absolute left-3.5 top-1/2 -translate-y-1/2 text-muted-foreground pointer-events-none" />
        <input
          value={search}
          onChange={(e) => setSearch(e.target.value)}

          placeholder="Spiller sichen… (Numm, Email, Mitglied Nr)"
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
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Numm</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Email</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Mitglied Nr</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Portal</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Admin</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Spiller</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Ø / 36</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Bescht</TableHead>
                  <TableHead className="w-28"></TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {filtered.length === 0 && (
                  <TableRow>
                    <TableCell colSpan={9} className="text-center py-10 text-muted-foreground font-medium">
                      {search ? `Keng Resultater fir "${search}"` : "Keng Spiller fonnt"}
                    </TableCell>
                  </TableRow>
                )}
                {filtered.map((p) => (
                  <TableRow key={p.id} className="border-border/30 hover:bg-secondary/20 transition-colors">
                    <TableCell className="font-bold text-foreground">{p.name}</TableCell>
                    <TableCell className="text-muted-foreground text-sm font-mono">{p.email || <span className="opacity-30">–</span>}</TableCell>
                    <TableCell className="text-muted-foreground text-sm font-mono">{p.mitgliedNr || <span className="opacity-30">–</span>}</TableCell>
                    <TableCell className="text-center">
                      {p.portalAktiv
                        ? <CheckCircle2 size={16} className="text-green-500 mx-auto" />
                        : <XCircle size={16} className="text-muted-foreground/40 mx-auto" />}
                    </TableCell>
                    <TableCell className="text-center">
                      {p.isAdmin ? <Shield size={16} className="text-primary mx-auto" /> : <span className="text-muted-foreground/30">–</span>}
                    </TableCell>
                    <TableCell className="text-right font-mono text-muted-foreground">{p.anzahlSpiele}</TableCell>
                    <TableCell className="text-right font-mono font-bold">{p.durchschnitt.toFixed(1)}</TableCell>
                    <TableCell className="text-right font-mono font-bold text-primary">{p.bestPunkte > 0 ? p.bestPunkte : <span className="text-muted-foreground/40">–</span>}</TableCell>
                    <TableCell>
                      <div className="flex items-center justify-end gap-1">
                        <IconBtn title="Profil gesinn" onClick={() => navigate(`/admin/spieler/${p.id}`)}><Eye size={14} /></IconBtn>
                        <IconBtn title="Änneren" onClick={() => openEdit(p)}><Pencil size={14} /></IconBtn>
                        <IconBtn title="Passwuert" onClick={() => { setPwdPlayer(p); setNewPwd(""); }}><KeyRound size={14} /></IconBtn>
                        <IconBtn title="Läschen" danger onClick={() => setDeletePlayer(p)}><Trash2 size={14} /></IconBtn>
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
            <DialogTitle>Neit Spiller erstellen</DialogTitle>
            <DialogDescription>Fëllt d'Felder aus. Passwuert ass nëmme néideg fir Portal-Zougang.</DialogDescription>
          </DialogHeader>
          <PlayerFormFields form={addForm} onChange={setAddForm} showPassword isEdit={false} />
          <DialogFooter>
            <button onClick={() => setAddOpen(false)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Ofbriechen</button>
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
            <DialogTitle>Spiller änneren</DialogTitle>
            <DialogDescription>{editPlayer?.name}</DialogDescription>
          </DialogHeader>
          <PlayerFormFields form={editForm} onChange={setEditForm} showPassword={false} isEdit />
          <DialogFooter>
            <button onClick={() => setEditPlayer(null)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Ofbriechen</button>
            <button
              onClick={() => editPlayer && updateMut.mutate({ id: editPlayer.id, body: editForm })}
              disabled={!editForm.name || updateMut.isPending}
              className="px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50"
            >
              {updateMut.isPending ? "Späicheren…" : "Späicheren"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* ── Delete Dialog ─────────────────────────────────────────────────── */}
      <Dialog open={!!deletePlayer} onOpenChange={(o) => !o && setDeletePlayer(null)}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Spiller läschen</DialogTitle>
            <DialogDescription>
              Wëllt dir <strong>{deletePlayer?.name}</strong> wierklech läschen? All Statistiken a Resultater ginn onwidderrufflech geläscht.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <button onClick={() => setDeletePlayer(null)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Ofbriechen</button>
            <button
              onClick={() => deletePlayer && deleteMut.mutate(deletePlayer.id)}
              disabled={deleteMut.isPending}
              className="px-4 py-2 bg-destructive hover:bg-destructive/90 text-destructive-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50"
            >
              {deleteMut.isPending ? "Läschen…" : "Definitiv läschen"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* ── Password Dialog ───────────────────────────────────────────────── */}
      <Dialog open={!!pwdPlayer} onOpenChange={(o) => !o && setPwdPlayer(null)}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Passwuert setzen</DialogTitle>
            <DialogDescription>Neit Passwuert fir <strong>{pwdPlayer?.name}</strong>.</DialogDescription>
          </DialogHeader>
          <PwdField value={newPwd} onChange={setNewPwd} />
          <DialogFooter>
            <button onClick={() => setPwdPlayer(null)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Ofbriechen</button>
            <button
              onClick={() => pwdPlayer && newPwd.length >= 6 && pwdMut.mutate({ id: pwdPlayer.id, pwd: newPwd })}
              disabled={newPwd.length < 6 || pwdMut.isPending}
              className="px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50"
            >
              {pwdMut.isPending ? "Späicheren…" : "Späicheren"}
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
      <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Neit Passwuert</label>
      <input
        type="password" autoComplete="new-password"
        value={value} onChange={(e) => onChange(e.target.value)}
        className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
        placeholder="Min. 6 Zeechen"
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
      <FormField label="Numm *" id="name">
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
          placeholder="max@beispill.lu"
        />
      </FormField>
      {isEdit ? (
        <FormField label="Mitglied Nr" id="nr">
          <div className="w-full bg-secondary/30 border border-border/40 rounded-lg px-4 py-2.5 text-sm font-mono font-medium text-muted-foreground select-all">
            {form.mitgliedNr || "–"}
          </div>
        </FormField>
      ) : (
        <div className="flex items-center gap-2 text-xs text-muted-foreground bg-secondary/20 border border-border/30 rounded-lg px-3 py-2">
          <span className="font-mono font-bold text-primary">WLZ###</span>
          <span>Mitglied Nr gëtt automatesch zougewisen</span>
        </div>
      )}
      {showPassword && (
        <FormField label="Passwuert (fir Portal-Zougang)" id="pwd">
          <input
            id="pwd" type="password" autoComplete="new-password" value={form.passwort}
            onChange={(e) => set("passwort", e.target.value)}
            className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
            placeholder="Min. 6 Zeechen"
          />
        </FormField>
      )}
      <div className="flex gap-6 pt-1">
        <Toggle label="Portal Aktiv" checked={form.portalAktiv} onToggle={(v) => set("portalAktiv", v)} />
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
