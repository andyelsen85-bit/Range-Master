import { useState } from "react";
import { useAuthStore } from "@/store/use-auth-store";
import { useToast } from "@/hooks/use-toast";
import { User, Lock, Mail, Hash } from "lucide-react";

export default function Profil() {
  const user = useAuthStore((s) => s.user);
  const token = useAuthStore((s) => s.token);
  const { toast } = useToast();

  const [form, setForm] = useState({ altPasswort: "", neuesPasswort: "", confirm: "" });
  const [loading, setLoading] = useState(false);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (form.neuesPasswort !== form.confirm) {
      toast({ title: "Fehler", description: "Die neuen Passwörter stimmen nicht überein.", variant: "destructive" });
      return;
    }
    if (form.neuesPasswort.length < 6) {
      toast({ title: "Fehler", description: "Das neue Passwort muss mindestens 6 Zeichen enthalten.", variant: "destructive" });
      return;
    }
    setLoading(true);
    try {
      const res = await fetch("/api/auth/passwort", {
        method: "PUT",
        headers: { "Content-Type": "application/json", Authorization: `Bearer ${token}` },
        body: JSON.stringify({ altPasswort: form.altPasswort, neuesPasswort: form.neuesPasswort }),
      });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || "Fehler");
      toast({ title: "Erfolg", description: "Ihr Passwort wurde geändert." });
      setForm({ altPasswort: "", neuesPasswort: "", confirm: "" });
    } catch (err: any) {
      toast({ title: "Fehler", description: err.message, variant: "destructive" });
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="space-y-8 animate-in fade-in duration-500 max-w-lg">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight">Mein Profil</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">Kontoinformationen und Passwort ändern.</p>
      </header>

      {/* Profile info */}
      <div className="bg-card border border-border/50 rounded-xl overflow-hidden">
        <div className="px-6 py-4 bg-secondary/20 border-b border-border/50">
          <h2 className="text-xs font-black uppercase tracking-widest text-muted-foreground">Profilinformationen</h2>
        </div>
        <div className="p-6 space-y-4">
           <InfoRow icon={<User size={16} />} label="Name" value={user?.name ?? "–"} />
          <InfoRow icon={<Mail size={16} />} label="Email" value={user?.email ?? "–"} />
           <InfoRow icon={<Hash size={16} />} label="Mitgliedsnummer" value={user?.mitgliedNr ?? "–"} />
        </div>
      </div>

      {/* Password change */}
      <div className="bg-card border border-border/50 rounded-xl overflow-hidden">
        <div className="px-6 py-4 bg-secondary/20 border-b border-border/50">
          <h2 className="text-xs font-black uppercase tracking-widest text-muted-foreground">Passwort ändern</h2>
        </div>
        <form onSubmit={handleSubmit} className="p-6 space-y-5">
          <Field label="Altes Passwort" id="alt">
            <input
              id="alt" type="password" required autoComplete="current-password"
              value={form.altPasswort}
              onChange={(e) => setForm((f) => ({ ...f, altPasswort: e.target.value }))}
              className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
              placeholder="••••••••"
            />
          </Field>
          <Field label="Neues Passwort" id="new">
            <input
              id="new" type="password" required autoComplete="new-password"
              value={form.neuesPasswort}
              onChange={(e) => setForm((f) => ({ ...f, neuesPasswort: e.target.value }))}
              className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
              placeholder="••••••••"
            />
          </Field>
          <Field label="Neues Passwort (bestätigen)" id="confirm">
            <input
              id="confirm" type="password" required autoComplete="new-password"
              value={form.confirm}
              onChange={(e) => setForm((f) => ({ ...f, confirm: e.target.value }))}
              className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors"
              placeholder="••••••••"
            />
          </Field>
          <button
            type="submit" disabled={loading}
            className="w-full flex items-center justify-center gap-2 bg-primary hover:bg-primary/90 text-primary-foreground font-bold py-2.5 rounded-lg transition-colors disabled:opacity-50"
          >
            <Lock size={16} />
            {loading ? "Wird geändert…" : "Passwort ändern"}
          </button>
        </form>
      </div>
    </div>
  );
}

function InfoRow({ icon, label, value }: { icon: React.ReactNode; label: string; value: string }) {
  return (
    <div className="flex items-center gap-3">
      <div className="w-8 h-8 rounded-lg bg-secondary/50 flex items-center justify-center text-muted-foreground shrink-0">{icon}</div>
      <div className="min-w-0">
        <div className="text-[10px] uppercase tracking-widest text-muted-foreground font-bold mb-0.5">{label}</div>
        <div className="text-sm font-semibold text-foreground truncate">{value}</div>
      </div>
    </div>
  );
}

function Field({ label, id, children }: { label: string; id: string; children: React.ReactNode }) {
  return (
    <div className="space-y-1.5">
      <label htmlFor={id} className="text-xs font-bold uppercase tracking-widest text-muted-foreground">{label}</label>
      {children}
    </div>
  );
}
