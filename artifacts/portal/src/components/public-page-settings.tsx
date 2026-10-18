import { useEffect, useRef, useState } from "react";
import { useQueryClient } from "@tanstack/react-query";
import {
  useGetPublicPageSettings, getGetPublicPageSettingsQueryKey, useUpdatePublicPageSettings,
  getGetPublicLastGameQueryKey,
  useSetPublicPagePin, useGetPublicPageDevices, getGetPublicPageDevicesQueryKey,
  useRevokePublicPageDevice, useResetPublicPageDevices,
} from "@workspace/api-client-react";
import { QueueSignupInputModus } from "@workspace/api-client-react";
import type { PublicPageSettingsNameMode } from "@workspace/api-client-react";
import { modeLabel } from "@/components/scorecard";
import { useToast } from "@/hooks/use-toast";
import { Skeleton } from "@/components/ui/skeleton";
import { Tv, Save, ShieldAlert, ExternalLink, AlertCircle, MonitorSmartphone, KeyRound, Ban, RotateCcw } from "lucide-react";
import { errMsg } from "@/components/daily-report-settings";

export function PublicPageSettingsCard() {
  const qc = useQueryClient();
  const { toast } = useToast();
  const { data, isLoading, isError, refetch } = useGetPublicPageSettings();
  const [enabled, setEnabled] = useState(false);
  const [nameMode, setNameMode] = useState<PublicPageSettingsNameMode>("INITIAL");
  const [queueEnabled, setQueueEnabled] = useState(false);
  const [groupsEnabled, setGroupsEnabled] = useState(true);
  const [showRanking, setShowRanking] = useState(false);
  const [maxQueueGames, setMaxQueueGames] = useState("6");
  const [enabledModes, setEnabledModes] = useState<string[]>(["NORMAL"]);
  const init = useRef(false);

  useEffect(() => {
    if (data && !init.current) {
      init.current = true;
      setEnabled(!!data.enabled);
      setNameMode(data.nameMode ?? "INITIAL");
      setQueueEnabled(!!data.queueEnabled);
      setGroupsEnabled(data.groupsEnabled ?? true);
      setShowRanking(!!data.showRanking);
      setMaxQueueGames(String(data.maxQueueGames ?? 6));
      setEnabledModes(data.enabledModes?.length ? data.enabledModes : ["NORMAL"]);
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
          <p className="text-xs text-muted-foreground mt-1">Live-Resultate, Warteschlange und Ausdruck für den Touch-PC am Stand. Nur auf mit PIN entsperrten Stand-PCs erreichbar.</p>
        </div>
        <a href={liveUrl} target="_blank" rel="noreferrer" data-testid="link-live"
          className="flex items-center gap-1.5 text-xs font-bold text-primary hover:underline shrink-0">
          /live <ExternalLink size={12} />
        </a>
      </div>

      <div className="flex items-start gap-2 text-xs text-amber-500 bg-amber-500/10 border border-amber-500/30 rounded-lg px-3 py-2.5">
        <ShieldAlert size={15} className="shrink-0 mt-px" />
        <span>Die Seite zeigt Namen und Resultate nur auf Stand-PCs, die einmal mit der PIN entsperrt wurden. Die PIN nur dem Vorstand bekanntgeben. Nur einschalten, wenn die Mitglieder einverstanden sind.</span>
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

      <div className="space-y-3 border-t border-border/40 pt-4">
        <p className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Warteschlange am Stand-PC</p>
        <Toggle on={queueEnabled} set={setQueueEnabled} label="Warteschlange aktiv (Einschreiben und Ergebnisse drucken)" testid="toggle-queue-enabled" />
        <Toggle on={groupsEnabled} set={setGroupsEnabled} label="Gruppen erlaubt" testid="toggle-groups-enabled" />
        <Toggle on={showRanking} set={setShowRanking} label="Top 15 anzeigen, wenn kein Spiel läuft" testid="toggle-show-ranking" />
        <label className="flex items-center gap-3 text-sm">
          <span className="text-muted-foreground font-semibold">Max. Spiele in der Warteschlange</span>
          <input type="number" min={1} max={40} value={maxQueueGames} onChange={(e) => setMaxQueueGames(e.target.value)} data-testid="input-max-queue-games"
            className="w-20 bg-background border border-border/60 rounded-lg px-2 py-1.5 text-sm font-mono" />
        </label>
        <div className="space-y-1.5">
          <span className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Modi zum Einschreiben</span>
          <div className="flex flex-wrap gap-2">
            {Object.values(QueueSignupInputModus).map((m) => {
              const on = enabledModes.includes(m);
              return (
                <button key={m} type="button" data-testid={`button-mode-${m}`}
                  onClick={() => setEnabledModes((cur) => on ? (cur.length > 1 ? cur.filter((x) => x !== m) : cur) : [...cur, m])}
                  className={`px-3 py-1.5 rounded-lg text-xs font-bold border ${on ? "bg-primary/15 border-primary text-primary" : "border-border/60 text-muted-foreground hover:text-foreground"}`}>
                  {modeLabel(m)}
                </button>
              );
            })}
          </div>
        </div>
      </div>

      <button onClick={() => saveMut.mutate({ data: {
        enabled, nameMode, queueEnabled, groupsEnabled, showRanking, enabledModes,
        maxQueueGames: Math.min(40, Math.max(1, Math.round(Number(maxQueueGames) || 6))),
      } })} disabled={saveMut.isPending} data-testid="button-save-public-page"
        className="flex items-center gap-2 px-4 py-2.5 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg disabled:opacity-50">
        <Save size={16} /> {saveMut.isPending ? "Wird gespeichert…" : "Speichern"}
      </button>

      <StandPcAccess pinConfigured={!!data?.pinConfigured} pinChangedAt={data?.pinChangedAt ?? null} />
    </div>
  );
}

function Toggle({ on, set, label, testid }: { on: boolean; set: (v: boolean) => void; label: string; testid: string }) {
  return (
    <button type="button" data-testid={testid} onClick={() => set(!on)}
      className="flex items-center gap-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">
      <div className={`w-9 h-5 rounded-full transition-colors relative shrink-0 ${on ? "bg-primary" : "bg-secondary"}`}>
        <div className={`absolute top-0.5 w-4 h-4 rounded-full bg-white shadow transition-transform ${on ? "translate-x-4" : "translate-x-0.5"}`} />
      </div>
      {label}
    </button>
  );
}

const fmtTs = (iso: string | null | undefined) => iso ? new Date(iso).toLocaleString("de-LU", { day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit" }) : "noch nie";

function StandPcAccess({ pinConfigured, pinChangedAt }: { pinConfigured: boolean; pinChangedAt: string | null }) {
  const qc = useQueryClient();
  const { toast } = useToast();
  const [pin, setPin] = useState("");
  const [pin2, setPin2] = useState("");
  const [confirmReset, setConfirmReset] = useState(false);
  const [confirmRevoke, setConfirmRevoke] = useState<number | null>(null);
  const devicesQ = useGetPublicPageDevices();
  const devices = devicesQ.data?.devices ?? [];
  const onError = (e: unknown) => toast({ title: "Fehler", description: errMsg(e), variant: "destructive" });
  const refresh = () => {
    qc.invalidateQueries({ queryKey: getGetPublicPageDevicesQueryKey() });
    qc.invalidateQueries({ queryKey: getGetPublicPageSettingsQueryKey() });
  };

  const pinMut = useSetPublicPagePin({ mutation: {
    onSuccess: () => { setPin(""); setPin2(""); refresh(); toast({ title: "PIN gespeichert", description: "Alle Stand-PCs wurden abgemeldet." }); },
    onError,
  } });
  const revokeMut = useRevokePublicPageDevice({ mutation: { onSuccess: () => { setConfirmRevoke(null); refresh(); toast({ title: "Stand-PC abgemeldet" }); }, onError } });
  const resetMut = useResetPublicPageDevices({ mutation: { onSuccess: () => { setConfirmReset(false); refresh(); toast({ title: "Alle Stand-PCs abgemeldet" }); }, onError } });

  const digits = (v: string) => v.replace(/[^0-9]/g, "").slice(0, 32);
  const valid = /^[0-9]{6,32}$/.test(pin);
  const match = pin === pin2;
  const inputCls = "w-full bg-background border border-border/60 rounded-lg px-3 py-2 text-sm font-mono tracking-widest focus:outline-none focus:border-primary";

  return (
    <div className="border-t border-border/40 pt-4 space-y-4" data-testid="section-standpc-access">
      <div>
        <p className="text-xs font-bold uppercase tracking-widest text-muted-foreground flex items-center gap-2"><MonitorSmartphone size={14} /> Stand-PC Zugang</p>
        <p className="text-xs text-muted-foreground mt-1">Am Stand-PC /live öffnen und einmal die PIN eingeben. Der Browser bleibt danach entsperrt.</p>
      </div>

      {!pinConfigured ? (
        <p className="text-xs text-amber-500 bg-amber-500/10 border border-amber-500/30 rounded-lg px-3 py-2.5" data-testid="text-pin-missing">
          Noch keine PIN festgelegt. Ohne PIN bleibt /live auf jedem Gerät gesperrt.
        </p>
      ) : (
        <p className="text-xs text-muted-foreground" data-testid="text-pin-changed">PIN gesetzt, zuletzt geändert: <span className="font-mono">{fmtTs(pinChangedAt)}</span></p>
      )}

      <form className="grid gap-2 sm:grid-cols-[1fr_1fr_auto] items-end" onSubmit={(e) => { e.preventDefault(); if (valid && match) pinMut.mutate({ data: { pin } }); }}>
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">{pinConfigured ? "Neue PIN" : "PIN"}
          <input type="password" inputMode="numeric" autoComplete="new-password" value={pin} onChange={(e) => setPin(digits(e.target.value))} placeholder="6 bis 32 Ziffern" data-testid="input-pin" className={inputCls} />
        </label>
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">PIN bestätigen
          <input type="password" inputMode="numeric" autoComplete="new-password" value={pin2} onChange={(e) => setPin2(digits(e.target.value))} data-testid="input-pin-confirm" className={inputCls} />
        </label>
        <button type="submit" disabled={!valid || !match || pinMut.isPending} data-testid="button-pin-save"
          className="flex items-center justify-center gap-2 px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg disabled:opacity-50">
          <KeyRound size={15} /> {pinMut.isPending ? "Speichert…" : pinConfigured ? "PIN ändern" : "PIN festlegen"}
        </button>
      </form>
      {pin.length > 0 && !valid && <p className="text-xs text-destructive">Mindestens 6 Ziffern.</p>}
      {valid && pin2.length > 0 && !match && <p className="text-xs text-destructive">Die PINs stimmen nicht überein.</p>}
      <p className="text-xs text-muted-foreground">Eine PIN-Änderung oder das Zurücksetzen meldet alle Stand-PCs ab. Danach muss an jedem Stand-PC die PIN einmal neu eingegeben werden.</p>

      <div className="space-y-2">
        <div className="flex items-center justify-between gap-2">
          <p className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Aktive Stand-PCs</p>
          {confirmReset ? (
            <span className="inline-flex items-center gap-2 text-xs">
              Alle abmelden?
              <button type="button" disabled={resetMut.isPending} onClick={() => resetMut.mutate()} data-testid="button-reset-confirm" className="px-2 py-1 rounded bg-destructive text-destructive-foreground font-bold">Ja</button>
              <button type="button" onClick={() => setConfirmReset(false)} className="px-2 py-1 rounded border border-border/60">Nein</button>
            </span>
          ) : (
            <button type="button" onClick={() => setConfirmReset(true)} disabled={devices.length === 0} data-testid="button-reset-devices"
              className="flex items-center gap-1.5 px-2.5 py-1 rounded-lg border border-border/60 text-xs font-bold text-destructive hover:bg-secondary/40 disabled:opacity-40">
              <RotateCcw size={13} /> Alle abmelden
            </button>
          )}
        </div>
        {devicesQ.isLoading ? <Skeleton className="h-16 w-full rounded-lg bg-secondary/30" /> : devicesQ.isError ? (
          <div className="flex items-center justify-between gap-2 text-xs text-destructive border border-destructive/30 rounded-lg px-3 py-2">
            <span className="flex items-center gap-2"><AlertCircle size={14} /> Geräte konnten nicht geladen werden.</span>
            <button type="button" onClick={() => devicesQ.refetch()} className="px-2 py-1 rounded border border-border/60 font-bold text-foreground">Erneut versuchen</button>
          </div>
        ) : devices.length === 0 ? (
          <p className="text-sm text-muted-foreground border border-dashed border-border/60 rounded-lg p-4 text-center">Kein Stand-PC entsperrt.</p>
        ) : (
          <ul className="divide-y divide-border/30 border border-border/40 rounded-lg">
            {devices.map((d) => (
              <li key={d.id} className="flex flex-wrap items-center justify-between gap-2 px-3 py-2.5 text-sm" data-testid={`row-device-${d.id}`}>
                <div>
                  <p className="font-semibold">{d.name ?? `Stand-PC #${d.id}`}</p>
                  <p className="text-xs text-muted-foreground font-mono">Entsperrt {fmtTs(d.createdAt)} · zuletzt aktiv {fmtTs(d.lastUsedAt)}</p>
                </div>
                {confirmRevoke === d.id ? (
                  <span className="inline-flex items-center gap-2 text-xs">
                    Abmelden?
                    <button type="button" disabled={revokeMut.isPending} onClick={() => revokeMut.mutate({ id: d.id })} data-testid={`button-revoke-confirm-${d.id}`} className="px-2 py-1 rounded bg-destructive text-destructive-foreground font-bold">Ja</button>
                    <button type="button" onClick={() => setConfirmRevoke(null)} className="px-2 py-1 rounded border border-border/60">Nein</button>
                  </span>
                ) : (
                  <button type="button" onClick={() => setConfirmRevoke(d.id)} data-testid={`button-revoke-${d.id}`}
                    className="flex items-center gap-1.5 px-2.5 py-1 rounded-lg border border-border/60 text-xs font-bold text-destructive hover:bg-secondary/40">
                    <Ban size={13} /> Abmelden
                  </button>
                )}
              </li>
            ))}
          </ul>
        )}
      </div>
    </div>
  );
}
