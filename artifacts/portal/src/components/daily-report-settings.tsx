import { useEffect, useRef, useState } from "react";
import { useQueryClient } from "@tanstack/react-query";
import {
  useGetDailyReportSettings, getGetDailyReportSettingsQueryKey,
  useUpdateDailyReportSettings, useSendTestDailyReport, useResendDailyReport,
} from "@workspace/api-client-react";
import type { DailyReportRecipient } from "@workspace/api-client-react";
import { useToast } from "@/hooks/use-toast";
import { Skeleton } from "@/components/ui/skeleton";
import { CalendarClock, Plus, Trash2, Save, Send, RotateCcw, AlertCircle, CircleDot } from "lucide-react";
import { cn } from "@/lib/utils";

const MAX_RECIPIENTS = 20;
const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

export function errMsg(e: unknown): string {
  const any = e as { data?: { error?: string }; message?: string } | null;
  return any?.data?.error ?? any?.message ?? "Unbekannter Fehler";
}

function fmtDate(d: string) {
  const [y, m, day] = d.slice(0, 10).split("-");
  return `${day}.${m}.${y}`;
}

export function DailyReportSettingsCard({ inputCls }: { inputCls: string }) {
  const qc = useQueryClient();
  const { toast } = useToast();
  const { data, isLoading, isError, refetch } = useGetDailyReportSettings();

  const [enabled, setEnabled] = useState(false);
  const [recipients, setRecipients] = useState<DailyReportRecipient[]>([]);
  const [newEmail, setNewEmail] = useState("");
  const [dirty, setDirty] = useState(false);
  const initialized = useRef(false);

  useEffect(() => {
    if (data && !initialized.current) {
      initialized.current = true;
      setEnabled(!!data.enabled);
      setRecipients(data.recipients ?? []);
    }
  }, [data]);

  const invalidate = () => qc.invalidateQueries({ queryKey: getGetDailyReportSettingsQueryKey() });

  const saveMut = useUpdateDailyReportSettings({
    mutation: {
      onSuccess: () => { setDirty(false); initialized.current = false; invalidate(); toast({ title: "Tagesmail-Einstellungen gespeichert" }); },
      onError: (e) => toast({ title: "Fehler", description: errMsg(e), variant: "destructive" }),
    },
  });
  const testMut = useSendTestDailyReport({
    mutation: {
      onSuccess: () => { invalidate(); toast({ title: "Testmail gesendet", description: "Betreff mit TEST, an alle aktiven Empfänger." }); },
      onError: (e) => toast({ title: "Testmail fehlgeschlagen", description: errMsg(e), variant: "destructive" }),
    },
  });
  const resendMut = useResendDailyReport({
    mutation: {
      onSuccess: (_d, v) => { invalidate(); toast({ title: "Erneut gesendet", description: fmtDate(v.data.datum) }); },
      onError: (e) => { invalidate(); toast({ title: "Senden fehlgeschlagen", description: errMsg(e), variant: "destructive" }); },
    },
  });

  const normalized = newEmail.trim().toLowerCase();
  const duplicate = recipients.some((r) => r.email.toLowerCase() === normalized);
  const full = recipients.length >= MAX_RECIPIENTS;
  const validNew = EMAIL_RE.test(normalized) && !duplicate && !full;

  const addRecipient = () => {
    if (!validNew) return;
    setRecipients((r) => [...r, { email: normalized, active: true }]);
    setNewEmail("");
    setDirty(true);
  };
  const toggle = (i: number) => { setRecipients((r) => r.map((x, j) => (j === i ? { ...x, active: !x.active } : x))); setDirty(true); };
  const remove = (i: number) => { setRecipients((r) => r.filter((_, j) => j !== i)); setDirty(true); };

  const activeCount = recipients.filter((r) => r.active).length;

  if (isLoading) return <Skeleton className="h-64 w-full rounded-xl bg-secondary/30" />;
  if (isError) {
    return (
      <div className="bg-card border border-destructive/30 rounded-xl p-6 flex items-center justify-between gap-3 text-sm">
        <span className="flex items-center gap-2 text-destructive"><AlertCircle size={16} /> Tagesmail-Einstellungen konnten nicht geladen werden.</span>
        <button onClick={() => refetch()} className="px-3 py-1.5 rounded-lg border border-border/60 text-xs font-bold hover:bg-secondary/40">Erneut versuchen</button>
      </div>
    );
  }

  const history = data?.history ?? [];

  return (
    <div className="bg-card border border-border/50 rounded-xl p-6 space-y-5 shadow-sm" data-testid="card-daily-report">
      <div className="flex items-start justify-between gap-4">
        <div>
          <h2 className="font-bold flex items-center gap-2"><CalendarClock size={18} className="text-primary" /> Tägliche Abrechnung</h2>
          <p className="text-xs text-muted-foreground mt-1">Am Folgetag wird die Tagesabrechnung automatisch an die aktiven Empfänger gesendet. Nutzt die SMTP-Einstellungen oben.</p>
        </div>
      </div>

      <button type="button" data-testid="toggle-daily-report" onClick={() => { setEnabled(!enabled); setDirty(true); }}
        className="flex items-center gap-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">
        <div className={`w-9 h-5 rounded-full transition-colors relative ${enabled ? "bg-primary" : "bg-secondary"}`}>
          <div className={`absolute top-0.5 w-4 h-4 rounded-full bg-white shadow transition-transform ${enabled ? "translate-x-4" : "translate-x-0.5"}`} />
        </div>
        Tagesabrechnung per Mail senden
      </button>

      <div className="space-y-2">
        <div className="flex items-center justify-between">
          <span className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Empfänger</span>
          <span className="text-[11px] font-mono text-muted-foreground">{activeCount} aktiv · {recipients.length}/{MAX_RECIPIENTS}</span>
        </div>
        {recipients.length === 0 ? (
          <p className="text-sm text-muted-foreground italic border border-dashed border-border/60 rounded-lg px-4 py-3">Noch keine Empfänger. Ohne aktive Adresse wird nichts gesendet.</p>
        ) : (
          <ul className="divide-y divide-border/30 border border-border/50 rounded-lg overflow-hidden">
            {recipients.map((r, i) => (
              <li key={r.email} className={cn("flex items-center justify-between gap-3 px-3 py-2", !r.active && "opacity-55")} data-testid={`row-recipient-${i}`}>
                <span className={cn("font-mono text-sm truncate", !r.active && "line-through")}>{r.email}</span>
                <div className="flex items-center gap-1 shrink-0">
                  <button onClick={() => toggle(i)} data-testid={`button-toggle-recipient-${i}`}
                    className="px-2 py-1 rounded-md text-[11px] font-bold text-muted-foreground hover:text-foreground hover:bg-secondary/50">
                    {r.active ? "Deaktivieren" : "Aktivieren"}
                  </button>
                  <button onClick={() => remove(i)} aria-label="Entfernen" data-testid={`button-remove-recipient-${i}`}
                    className="p-1.5 rounded-md text-muted-foreground hover:text-destructive hover:bg-destructive/10">
                    <Trash2 size={14} />
                  </button>
                </div>
              </li>
            ))}
          </ul>
        )}
        <div className="flex gap-2">
          <input className={inputCls} type="email" value={newEmail} placeholder="kassierer@beispill.lu" disabled={full}
            data-testid="input-recipient" onChange={(e) => setNewEmail(e.target.value)}
            onKeyDown={(e) => { if (e.key === "Enter") { e.preventDefault(); addRecipient(); } }} />
          <button onClick={addRecipient} disabled={!validNew} data-testid="button-add-recipient"
            className="flex items-center gap-1.5 px-4 py-2.5 bg-secondary hover:bg-secondary/80 text-sm font-bold rounded-lg disabled:opacity-50 shrink-0">
            <Plus size={16} /> Hinzufügen
          </button>
        </div>
        {normalized && duplicate && <p className="text-xs text-amber-500">Diese Adresse ist bereits eingetragen.</p>}
        {full && <p className="text-xs text-muted-foreground">Maximal {MAX_RECIPIENTS} Empfänger.</p>}
      </div>

      <div className="flex flex-wrap gap-2 pt-1">
        <button onClick={() => saveMut.mutate({ data: { enabled, recipients } })} disabled={saveMut.isPending} data-testid="button-save-daily-report"
          className="flex items-center gap-2 px-4 py-2.5 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg disabled:opacity-50">
          <Save size={16} /> {saveMut.isPending ? "Wird gespeichert…" : "Speichern"}
        </button>
        <button onClick={() => testMut.mutate()} disabled={testMut.isPending || dirty || activeCount === 0} data-testid="button-test-daily-report"
          className="flex items-center gap-2 px-4 py-2.5 bg-secondary hover:bg-secondary/80 text-sm font-bold rounded-lg disabled:opacity-50">
          <Send size={16} /> {testMut.isPending ? "Wird gesendet…" : "Testmail senden"}
        </button>
        {dirty && <span className="self-center text-xs text-amber-500">Ungespeicherte Änderungen</span>}
      </div>

      <div className="space-y-2 pt-2 border-t border-border/40">
        <span className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Verlauf</span>
        {history.length === 0 ? (
          <p className="text-sm text-muted-foreground italic">Noch keine Tagesmails versendet.</p>
        ) : (
          <ul className="divide-y divide-border/30 border border-border/50 rounded-lg overflow-hidden">
            {history.map((h) => {
              const failed = /fail|error/i.test(h.status);
              const sending = resendMut.isPending && resendMut.variables?.data.datum === h.datum;
              return (
                <li key={h.datum} className={cn("flex items-center justify-between gap-3 px-3 py-2.5", failed && "bg-destructive/10")} data-testid={`row-history-${h.datum}`}>
                  <div className="min-w-0">
                    <div className="flex items-center gap-2 flex-wrap">
                      <span className="font-mono text-sm font-bold">{fmtDate(h.datum)}</span>
                      <span className={cn("text-[10px] font-bold uppercase tracking-widest", failed ? "text-destructive" : "text-emerald-500")}>
                        {failed ? "Fehlgeschlagen" : h.status === "SENT" || /sent/i.test(h.status) ? "Gesendet" : h.status}
                      </span>
                      {h.attempts > 1 && <span className="text-[10px] text-muted-foreground font-mono">{h.attempts} Versuche</span>}
                      {h.changed && (
                        <span className="flex items-center gap-1 text-[10px] font-bold text-amber-500" title="Die Tagesdaten haben sich seit dem Versand geändert">
                          <CircleDot size={11} /> Daten geändert
                        </span>
                      )}
                    </div>
                    {failed && h.error && <p className="text-xs text-destructive mt-0.5 truncate" title={h.error}>{h.error}</p>}
                    {!failed && h.sentAt && <p className="text-[11px] text-muted-foreground mt-0.5">{new Date(h.sentAt).toLocaleString("de-LU")}</p>}
                  </div>
                  <button onClick={() => resendMut.mutate({ data: { datum: h.datum } })} disabled={resendMut.isPending} data-testid={`button-resend-${h.datum}`}
                    className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg border border-border/60 text-xs font-bold hover:bg-secondary/40 disabled:opacity-50 shrink-0">
                    <RotateCcw size={13} /> {sending ? "Sendet…" : "Erneut senden"}
                  </button>
                </li>
              );
            })}
          </ul>
        )}
      </div>
    </div>
  );
}
