import { useEffect, useRef, useState } from "react";
import { useQueryClient } from "@tanstack/react-query";
import {
  useGetPublicPageSettings, getGetPublicPageSettingsQueryKey, useUpdatePublicPageSettings,
  getGetPublicLastGameQueryKey,
} from "@workspace/api-client-react";
import type { PublicPageSettingsNameMode } from "@workspace/api-client-react";
import { useToast } from "@/hooks/use-toast";
import { Skeleton } from "@/components/ui/skeleton";
import { Tv, Save, ShieldAlert, ExternalLink, AlertCircle } from "lucide-react";
import { errMsg } from "@/components/daily-report-settings";

export function PublicPageSettingsCard() {
  const qc = useQueryClient();
  const { toast } = useToast();
  const { data, isLoading, isError, refetch } = useGetPublicPageSettings();
  const [enabled, setEnabled] = useState(false);
  const [nameMode, setNameMode] = useState<PublicPageSettingsNameMode>("INITIAL");
  const init = useRef(false);

  useEffect(() => {
    if (data && !init.current) {
      init.current = true;
      setEnabled(!!data.enabled);
      setNameMode(data.nameMode ?? "INITIAL");
    }
  }, [data]);

  const saveMut = useUpdatePublicPageSettings({
    mutation: {
      onSuccess: () => {
        init.current = false;
        qc.invalidateQueries({ queryKey: getGetPublicPageSettingsQueryKey() });
        qc.invalidateQueries({ queryKey: getGetPublicLastGameQueryKey() });
        toast({ title: "Öffentliche Seite gespeichert" });
      },
      onError: (e) => toast({ title: "Fehler", description: errMsg(e), variant: "destructive" }),
    },
  });

  const base = import.meta.env.BASE_URL?.replace(/\/$/, "") ?? "";
  const liveUrl = `${base}/live`;

  if (isLoading) return <Skeleton className="h-48 w-full rounded-xl bg-secondary/30" />;
  if (isError) {
    return (
      <div className="bg-card border border-destructive/30 rounded-xl p-6 flex items-center justify-between gap-3 text-sm">
        <span className="flex items-center gap-2 text-destructive"><AlertCircle size={16} /> Einstellungen konnten nicht geladen werden.</span>
        <button onClick={() => refetch()} className="px-3 py-1.5 rounded-lg border border-border/60 text-xs font-bold hover:bg-secondary/40">Erneut versuchen</button>
      </div>
    );
  }

  return (
    <div className="bg-card border border-border/50 rounded-xl p-6 space-y-4 shadow-sm" data-testid="card-public-page">
      <div className="flex items-start justify-between gap-4">
        <div>
          <h2 className="font-bold flex items-center gap-2"><Tv size={18} className="text-primary" /> Öffentliche Seite</h2>
          <p className="text-xs text-muted-foreground mt-1">Zeigt ohne Login das zuletzt abgeschlossene Spiel – z. B. auf dem Fernseher im Clubhaus.</p>
        </div>
        <a href={liveUrl} target="_blank" rel="noreferrer" data-testid="link-live"
          className="flex items-center gap-1.5 text-xs font-bold text-primary hover:underline shrink-0">
          /live <ExternalLink size={12} />
        </a>
      </div>

      <div className="flex items-start gap-2 text-xs text-amber-500 bg-amber-500/10 border border-amber-500/30 rounded-lg px-3 py-2.5">
        <ShieldAlert size={15} className="shrink-0 mt-px" />
        <span>Wenn aktiviert, werden die Namen der Spieler des letzten Spiels und ihre Resultate <strong>öffentlich im Internet sichtbar</strong> – für jeden, der die Adresse kennt. Nur einschalten, wenn die Mitglieder einverstanden sind.</span>
      </div>

      <button type="button" data-testid="toggle-public-page" onClick={() => setEnabled(!enabled)}
        className="flex items-center gap-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">
        <div className={`w-9 h-5 rounded-full transition-colors relative ${enabled ? "bg-primary" : "bg-secondary"}`}>
          <div className={`absolute top-0.5 w-4 h-4 rounded-full bg-white shadow transition-transform ${enabled ? "translate-x-4" : "translate-x-0.5"}`} />
        </div>
        Öffentliche Seite aktiv
      </button>

      <div className="space-y-1.5">
        <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Namensanzeige</label>
        <div className="flex flex-wrap gap-2">
          {([["FULL", "Vor- und Nachname", "Jean Weber"], ["INITIAL", "Vorname + Initial", "Jean W."]] as const).map(([v, label, ex]) => (
            <button key={v} type="button" onClick={() => setNameMode(v)} data-testid={`button-namemode-${v}`}
              className={`px-4 py-2 rounded-lg text-sm font-bold border transition-colors text-left ${nameMode === v ? "bg-primary/15 border-primary text-primary" : "border-border/60 text-muted-foreground hover:text-foreground"}`}>
              {label} <span className="font-mono font-normal text-xs opacity-70 ml-1">{ex}</span>
            </button>
          ))}
        </div>
      </div>

      <button onClick={() => saveMut.mutate({ data: { enabled, nameMode } })} disabled={saveMut.isPending} data-testid="button-save-public-page"
        className="flex items-center gap-2 px-4 py-2.5 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg disabled:opacity-50">
        <Save size={16} /> {saveMut.isPending ? "Wird gespeichert…" : "Speichern"}
      </button>
    </div>
  );
}
