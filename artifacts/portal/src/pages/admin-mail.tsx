import { useEffect, useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { useToast } from "@/hooks/use-toast";
import { Skeleton } from "@/components/ui/skeleton";
import { Mail, Send, Save, ShieldAlert } from "lucide-react";

interface SmtpSettings {
  host: string;
  port: number;
  username: string;
  fromAddress: string;
  verschluesselung: "NONE" | "STARTTLS" | "SSL";
  ignoreTlsErrors: boolean;
  portalUrl: string;
  passwortGesat: boolean;
  konfiguréiert: boolean;
}

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
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
    return data;
  };
}

const emptyForm = {
  host: "", port: 587, username: "", passwort: "", fromAddress: "",
  verschluesselung: "STARTTLS" as SmtpSettings["verschluesselung"],
  ignoreTlsErrors: false, portalUrl: "",
};

export default function AdminMail() {
  const qc = useQueryClient();
  const apiFetch = useAdminFetch();
  const { toast } = useToast();
  const user = useAuthStore((s) => s.user);

  const [form, setForm] = useState(emptyForm);
  const [testEmpfaenger, setTestEmpfaenger] = useState("");

  const { data, isLoading } = useQuery<SmtpSettings>({
    queryKey: ["admin-smtp"],
    queryFn: () => apiFetch("/api/admin/smtp"),
  });

  useEffect(() => {
    if (data) setForm({ ...emptyForm, ...data, passwort: "" });
  }, [data]);

  useEffect(() => {
    if (user?.email && !testEmpfaenger) setTestEmpfaenger(user.email);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [user]);

  const saveMut = useMutation({
    mutationFn: () => apiFetch("/api/admin/smtp", { method: "PUT", body: JSON.stringify(form) }),
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["admin-smtp"] });
      setForm((f) => ({ ...f, passwort: "" }));
      toast({ title: "SMTP-Astellunge gespäichert" });
    },
    onError: (e: Error) => toast({ title: "Feeler", description: e.message, variant: "destructive" }),
  });

  const testMut = useMutation({
    mutationFn: () => apiFetch("/api/admin/smtp/test", { method: "POST", body: JSON.stringify({ empfaenger: testEmpfaenger }) }),
    onSuccess: () => toast({ title: "Test-Email verschéckt", description: `un ${testEmpfaenger}` }),
    onError: (e: Error) => toast({ title: "Verschécke feelgeschloen", description: e.message, variant: "destructive" }),
  });

  const set = (k: keyof typeof emptyForm, v: any) => setForm((f) => ({ ...f, [k]: v }));

  const inputCls = "w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors";

  if (isLoading) {
    return <div className="space-y-3 max-w-2xl">{[1, 2, 3, 4].map((i) => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}</div>;
  }

  return (
    <div className="space-y-6 animate-in fade-in duration-500 max-w-2xl">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight flex items-center gap-3"><Mail className="text-primary" /> Mail Server</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">
          SMTP-Astellunge fir Invitatiouns- a Passwuert-Emailen un d'Spiller.
        </p>
      </header>

      <div className="bg-card border border-border/50 rounded-xl p-6 space-y-4 shadow-sm">
        <div className="grid grid-cols-3 gap-4">
          <div className="col-span-2 space-y-1.5">
            <Label>SMTP Host</Label>
            <input className={inputCls} value={form.host} onChange={(e) => set("host", e.target.value)} placeholder="smtp.beispill.lu" />
          </div>
          <div className="space-y-1.5">
            <Label>Port</Label>
            <input className={inputCls} type="number" value={form.port} onChange={(e) => set("port", Number(e.target.value) || 0)} />
          </div>
        </div>

        <div className="grid grid-cols-2 gap-4">
          <div className="space-y-1.5">
            <Label>Benotzernumm</Label>
            <input className={inputCls} value={form.username} onChange={(e) => set("username", e.target.value)} autoComplete="off" />
          </div>
          <div className="space-y-1.5">
            <Label>Passwuert {data?.passwortGesat && <span className="normal-case font-normal text-muted-foreground/60">(gesat — eidel = behalen)</span>}</Label>
            <input className={inputCls} type="password" autoComplete="new-password" value={form.passwort}
              onChange={(e) => set("passwort", e.target.value)} placeholder={data?.passwortGesat ? "••••••••" : ""} />
          </div>
        </div>

        <div className="space-y-1.5">
          <Label>Vun-Adress (From)</Label>
          <input className={inputCls} type="email" value={form.fromAddress} onChange={(e) => set("fromAddress", e.target.value)} placeholder="noreply@trapmaster.lu" />
        </div>

        <div className="space-y-1.5">
          <Label>Portal URL (fir an d'Emailen)</Label>
          <input className={inputCls} value={form.portalUrl} onChange={(e) => set("portalUrl", e.target.value)} placeholder="https://portal.trapmaster.lu" />
        </div>

        <div className="space-y-1.5">
          <Label>Verschlësselung</Label>
          <div className="flex gap-2">
            {(["NONE", "STARTTLS", "SSL"] as const).map((v) => (
              <button key={v} type="button" onClick={() => set("verschluesselung", v)}
                className={`px-4 py-2 rounded-lg text-sm font-bold border transition-colors ${form.verschluesselung === v ? "bg-primary/15 border-primary text-primary" : "border-border/60 text-muted-foreground hover:text-foreground"}`}>
                {v === "NONE" ? "Keng" : v}
              </button>
            ))}
          </div>
        </div>

        <button type="button" onClick={() => set("ignoreTlsErrors", !form.ignoreTlsErrors)}
          className="flex items-center gap-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">
          <div className={`w-9 h-5 rounded-full transition-colors relative ${form.ignoreTlsErrors ? "bg-primary" : "bg-secondary"}`}>
            <div className={`absolute top-0.5 w-4 h-4 rounded-full bg-white shadow transition-transform ${form.ignoreTlsErrors ? "translate-x-4" : "translate-x-0.5"}`} />
          </div>
          Ongëlteg SSL-Zertifikater ignoréieren
        </button>
        {form.ignoreTlsErrors && (
          <div className="flex items-center gap-2 text-xs text-amber-500 bg-amber-500/10 border border-amber-500/30 rounded-lg px-3 py-2">
            <ShieldAlert size={14} /> Nëmme fir intern Serveren mat self-signed Zertifikater benotzen.
          </div>
        )}

        <div className="pt-2">
          <button onClick={() => saveMut.mutate()} disabled={saveMut.isPending}
            className="flex items-center gap-2 px-4 py-2.5 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50">
            <Save size={16} /> {saveMut.isPending ? "Späicheren…" : "Späicheren"}
          </button>
        </div>
      </div>

      <div className="bg-card border border-border/50 rounded-xl p-6 space-y-3 shadow-sm">
        <h2 className="font-bold text-sm uppercase tracking-widest text-muted-foreground">Test-Email</h2>
        <div className="flex gap-2">
          <input className={inputCls} type="email" value={testEmpfaenger} onChange={(e) => setTestEmpfaenger(e.target.value)} placeholder="test@beispill.lu" />
          <button onClick={() => testMut.mutate()} disabled={!testEmpfaenger || testMut.isPending}
            className="flex items-center gap-2 px-4 py-2.5 bg-secondary hover:bg-secondary/80 text-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50 shrink-0">
            <Send size={16} /> {testMut.isPending ? "Schécken…" : "Schécken"}
          </button>
        </div>
        <p className="text-xs text-muted-foreground">Späichert d'Astellungen als éischt — den Test benotzt déi gespäichert Konfiguratioun.</p>
      </div>
    </div>
  );
}

function Label({ children }: { children: React.ReactNode }) {
  return <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">{children}</label>;
}
