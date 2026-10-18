import { useState } from "react";
import { useQueryClient } from "@tanstack/react-query";
import {
  useGetKioskTokens, getGetKioskTokensQueryKey,
  useCreateKioskToken, useRenameKioskToken, useChangeKioskToken,
} from "@workspace/api-client-react";
import { useToast } from "@/hooks/use-toast";
import { Skeleton } from "@/components/ui/skeleton";
import { errMsg } from "@/components/daily-report-settings";
import { MonitorSmartphone, Plus, Copy, Check, AlertCircle, RefreshCw, Ban, Pencil, X, ShieldAlert } from "lucide-react";

type KioskTokenRow = import("@workspace/api-client-react").KioskTokenMetadata;
interface Revealed { id: number; name: string; token: string; url: string }

const fmt = (iso: string | null) => iso ? new Date(iso).toLocaleString("de-LU", { day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit" }) : "–";

/** Resolve a (possibly relative) URL returned by the API against the current origin. */
function absoluteUrl(u: string) {
  try { return new URL(u, window.location.origin).toString(); } catch { return u; }
}

export function KioskTokenSettingsCard() {
  const qc = useQueryClient();
  const { toast } = useToast();
  const { data, isLoading, isError, refetch } = useGetKioskTokens();
  const tokens = ((data as { tokens?: KioskTokenRow[] } | undefined)?.tokens) ?? [];

  const [newName, setNewName] = useState("Stand-PC");
  const [revealed, setRevealed] = useState<Revealed | null>(null);
  const [copied, setCopied] = useState(false);
  const [renaming, setRenaming] = useState<{ id: number; name: string } | null>(null);
  const [confirm, setConfirm] = useState<{ id: number; action: "revoke" | "renew" } | null>(null);

  const invalidate = () => qc.invalidateQueries({ queryKey: getGetKioskTokensQueryKey() });
  const reveal = (name: string) => (res: unknown) => {
    const r = res as { id: number; token: string; url: string };
    setRevealed({ id: r.id, name, token: r.token, url: absoluteUrl(r.url) });
    setCopied(false);
    invalidate();
  };
  const onError = (e: unknown) => toast({ title: "Fehler", description: errMsg(e), variant: "destructive" });

  const createMut = useCreateKioskToken({ mutation: { onError } });
  const renameMut = useRenameKioskToken({ mutation: { onSuccess: () => { setRenaming(null); invalidate(); toast({ title: "Umbenannt" }); }, onError } });
  const changeMut = useChangeKioskToken({ mutation: { onError } });

  const copy = async () => {
    if (!revealed) return;
    try {
      await navigator.clipboard.writeText(revealed.url);
      setCopied(true);
    } catch {
      toast({ title: "Kopieren nicht möglich", description: "Bitte die Adresse manuell markieren und kopieren.", variant: "destructive" });
    }
  };

  if (isLoading) return <Skeleton className="h-48 w-full rounded-xl bg-secondary/30" />;
  if (isError) {
    return (
      <div className="bg-card border border-destructive/30 rounded-xl p-6 flex items-center justify-between gap-3 text-sm">
        <span className="flex items-center gap-2 text-destructive"><AlertCircle size={16} /> Kiosk-Zugänge konnten nicht geladen werden.</span>
        <button onClick={() => refetch()} className="px-3 py-1.5 rounded-lg border border-border/60 text-xs font-bold hover:bg-secondary/40">Erneut versuchen</button>
      </div>
    );
  }

  return (
    <div className="bg-card border border-border/50 rounded-xl p-6 space-y-4 shadow-sm" data-testid="card-kiosk-tokens">
      <div>
        <h2 className="font-bold flex items-center gap-2"><MonitorSmartphone size={18} className="text-primary" /> Kiosk-Zugang</h2>
        <p className="text-xs text-muted-foreground mt-1">Nur ein Browser mit gültigem Token kann /live öffnen. Den Stand-PC einmal mit der angezeigten Adresse starten – der Token bleibt im Browser gespeichert.</p>
      </div>

      {revealed && (
        <div className="rounded-lg border border-primary/50 bg-primary/10 p-4 space-y-3" data-testid="panel-token-revealed">
          <div className="flex items-start justify-between gap-3">
            <p className="text-sm font-bold flex items-center gap-2"><ShieldAlert size={16} className="text-primary" /> Token für „{revealed.name}“ – wird nur jetzt angezeigt</p>
            <button onClick={() => setRevealed(null)} aria-label="Schließen" data-testid="button-token-dismiss" className="text-muted-foreground hover:text-foreground"><X size={16} /></button>
          </div>
          <div>
            <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Fertige Adresse</p>
            <div className="flex gap-2 mt-1">
              <code className="flex-1 min-w-0 break-all font-mono text-xs bg-background/70 border border-border/60 rounded-lg px-3 py-2 select-all" data-testid="text-token-url">{revealed.url}</code>
              <button onClick={copy} data-testid="button-token-copy"
                className="shrink-0 flex items-center gap-1.5 px-3 py-2 rounded-lg bg-primary text-primary-foreground text-xs font-bold">
                {copied ? <Check size={14} /> : <Copy size={14} />} {copied ? "Kopiert" : "Kopieren"}
              </button>
            </div>
          </div>
          <div>
            <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Token</p>
            <code className="block break-all font-mono text-xs mt-1 select-all" data-testid="text-token-plain">{revealed.token}</code>
          </div>
          <p className="text-xs text-muted-foreground">Nach dem Schließen ist der Token nicht mehr abrufbar. Bei Verlust: „Erneuern“.</p>
        </div>
      )}

      <form className="flex gap-2" onSubmit={(e) => {
        e.preventDefault();
        const name = newName.trim();
        if (!name) return;
        createMut.mutate({ data: { name } }, { onSuccess: reveal(name) });
      }}>
        <input value={newName} onChange={(e) => setNewName(e.target.value)} maxLength={80} placeholder="Name, z. B. Stand-PC" data-testid="input-token-name"
          className="flex-1 bg-background border border-border/60 rounded-lg px-3 py-2 text-sm focus:outline-none focus:border-primary" />
        <button type="submit" disabled={!newName.trim() || createMut.isPending} data-testid="button-token-create"
          className="flex items-center gap-2 px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg disabled:opacity-50 shrink-0">
          <Plus size={16} /> {createMut.isPending ? "Wird erzeugt…" : "Neuen Token erzeugen"}
        </button>
      </form>

      {tokens.length === 0 ? (
        <p className="text-sm text-muted-foreground border border-dashed border-border/60 rounded-lg p-5 text-center">
          Noch kein Token – ohne aktiven Token ist /live ausgeschaltet.
        </p>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="text-left text-[10px] uppercase tracking-widest text-muted-foreground border-b border-border/50">
                <th className="py-2 pr-3">Name</th><th className="pr-3">Erstellt</th><th className="pr-3">Zuletzt benutzt</th><th className="pr-3">Status</th><th className="text-right">Aktionen</th>
              </tr>
            </thead>
            <tbody>
              {tokens.map((t) => {
                const revoked = !!t.revokedAt;
                return (
                  <tr key={t.id} className={`border-b border-border/30 ${revoked ? "opacity-50" : ""}`} data-testid={`row-token-${t.id}`}>
                    <td className="py-2.5 pr-3 font-semibold">
                      {renaming?.id === t.id ? (
                        <form className="flex gap-1" onSubmit={(e) => { e.preventDefault(); if (renaming.name.trim()) renameMut.mutate({ id: t.id, data: { name: renaming.name.trim() } }); }}>
                          <input autoFocus value={renaming.name} maxLength={80} onChange={(e) => setRenaming({ id: t.id, name: e.target.value })} data-testid={`input-token-rename-${t.id}`}
                            className="w-36 bg-background border border-border/60 rounded px-2 py-1 text-sm" />
                          <button type="submit" className="px-2 rounded bg-primary text-primary-foreground" aria-label="Speichern"><Check size={14} /></button>
                          <button type="button" onClick={() => setRenaming(null)} className="px-2 rounded border border-border/60" aria-label="Abbrechen"><X size={14} /></button>
                        </form>
                      ) : t.name}
                    </td>
                    <td className="pr-3 font-mono text-xs">{fmt(t.createdAt)}</td>
                    <td className="pr-3 font-mono text-xs">{fmt(t.lastUsedAt)}{t.lastUsedIpHash && <span className="block text-muted-foreground">IP {t.lastUsedIpHash.slice(0, 10)}</span>}</td>
                    <td className="pr-3">
                      <span className={`px-2 py-0.5 rounded-full text-[11px] font-bold ${revoked ? "bg-destructive/15 text-destructive" : "bg-emerald-500/15 text-emerald-400"}`}>
                        {revoked ? "widerrufen" : "aktiv"}
                      </span>
                    </td>
                    <td className="text-right whitespace-nowrap">
                      {confirm?.id === t.id ? (
                        <span className="inline-flex items-center gap-2">
                          <span className="text-xs">{confirm.action === "revoke" ? "Wirklich widerrufen?" : "Alten Token sofort ungültig machen?"}</span>
                          <button data-testid={`button-token-confirm-${t.id}`} disabled={changeMut.isPending}
                            onClick={() => changeMut.mutate({ id: t.id, action: confirm.action }, {
                              onSuccess: (res) => {
                                setConfirm(null);
                                if (confirm.action === "renew") reveal(t.name)(res);
                                else { invalidate(); toast({ title: "Token widerrufen" }); }
                              },
                            })}
                            className="px-2 py-1 rounded bg-destructive text-destructive-foreground text-xs font-bold">Ja</button>
                          <button onClick={() => setConfirm(null)} className="px-2 py-1 rounded border border-border/60 text-xs">Nein</button>
                        </span>
                      ) : (
                        <span className="inline-flex gap-1">
                          <IconBtn label="Umbenennen" onClick={() => setRenaming({ id: t.id, name: t.name })} testid={`button-token-rename-${t.id}`}><Pencil size={14} /></IconBtn>
                          {!revoked && <IconBtn label="Erneuern" onClick={() => setConfirm({ id: t.id, action: "renew" })} testid={`button-token-renew-${t.id}`}><RefreshCw size={14} /></IconBtn>}
                          {!revoked && <IconBtn label="Widerrufen" onClick={() => setConfirm({ id: t.id, action: "revoke" })} testid={`button-token-revoke-${t.id}`} danger><Ban size={14} /></IconBtn>}
                        </span>
                      )}
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}

function IconBtn({ label, onClick, children, testid, danger }: { label: string; onClick: () => void; children: React.ReactNode; testid: string; danger?: boolean }) {
  return (
    <button type="button" onClick={onClick} title={label} aria-label={label} data-testid={testid}
      className={`flex items-center gap-1 px-2 py-1 rounded-lg border border-border/60 text-xs font-bold hover:bg-secondary/40 ${danger ? "text-destructive" : ""}`}>
      {children} <span className="hidden lg:inline">{label}</span>
    </button>
  );
}
