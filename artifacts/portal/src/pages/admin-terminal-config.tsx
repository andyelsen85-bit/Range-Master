import { useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { ShieldCheck, KeyRound, Ban, Copy, CheckCircle2, AlertTriangle, RefreshCw, Terminal, Clock, HardDrive, ShieldAlert } from "lucide-react";
import { useAuthStore } from "@/store/use-auth-store";
import { useToast } from "@/hooks/use-toast";
import { Badge } from "@/components/ui/badge";
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import { Skeleton } from "@/components/ui/skeleton";
import { cn } from "@/lib/utils";

interface ConfigBackup {
  id: number;
  terminalId: string;
  schemaVersion: number;
  firmwareVersion: string | null;
  checksum: string;
  createdAt: string;
  updatedAt: string;
  lastRestoredAt: string | null;
  revokedAt: string | null;
}

interface TerminalKey {
  id: number;
  name: string;
}

function useAdminFetch() {
  const token = useAuthStore((s) => s.token);
  return async (path: string, options?: RequestInit) => {
    const res = await fetch(path, {
      ...options,
      headers: { "Content-Type": "application/json", Authorization: `Bearer ${token}`, ...options?.headers },
    });
    const ct = res.headers.get("content-type") ?? "";
    const data = ct.includes("application/json") ? await res.json() : null;
    if (!res.ok) throw new Error(data?.error ?? `HTTP ${res.status}`);
    return data;
  };
}

function formatDate(value: string | null) {
  if (!value) return null;
  return new Intl.DateTimeFormat("lb-LU", { dateStyle: "medium", timeStyle: "short" }).format(new Date(value));
}

export default function AdminTerminalConfig() {
  const apiFetch = useAdminFetch();
  const qc = useQueryClient();
  const { toast } = useToast();
  const [code, setCode] = useState<{ value: string; expiresAt: string } | null>(null);
  const [authorizeId, setAuthorizeId] = useState<number | null>(null);
  const [targetTerminalId, setTargetTerminalId] = useState("");
  const [targetApiKeyId, setTargetApiKeyId] = useState("");
  const [revokeId, setRevokeId] = useState<number | null>(null);
  
  const backupsQuery = useQuery<{ backups: ConfigBackup[]; terminalKeys: TerminalKey[] }>({
    queryKey: ["admin-terminal-config-backups"],
    queryFn: () => apiFetch("/api/admin/terminal-config/backups"),
    refetchInterval: 30_000,
  });

  const invalidate = () => qc.invalidateQueries({ queryKey: ["admin-terminal-config-backups"] });
  
  const authorize = useMutation({
    mutationFn: ({ id, terminalId, apiKeyId }: { id: number; terminalId: string; apiKeyId: number }) =>
      apiFetch(`/api/admin/terminal-config/backups/${id}/authorize-restore`, {
        method: "POST",
        body: JSON.stringify({ targetTerminalId: terminalId, targetApiKeyId: apiKeyId }),
      }),
    onSuccess: (data) => {
      setAuthorizeId(null);
      setTargetTerminalId("");
      setTargetApiKeyId("");
      setCode({ value: data.code, expiresAt: data.expiresAt });
    },
    onError: (error: Error) => toast({ title: "Fehler", description: error.message, variant: "destructive" }),
  });
  
  const revoke = useMutation({
    mutationFn: (id: number) => apiFetch(`/api/admin/terminal-config/backups/${id}/revoke`, { method: "POST" }),
    onSuccess: () => { invalidate(); setRevokeId(null); toast({ title: "Backup widerrufen" }); },
    onError: (error: Error) => toast({ title: "Fehler", description: error.message, variant: "destructive" }),
  });

  const copyCode = async () => {
    if (!code) return;
    await navigator.clipboard?.writeText(code.value);
    toast({ title: "Code kopiert" });
  };

  return (
    <div className="space-y-8 animate-in fade-in duration-500 max-w-6xl mx-auto pb-10">
      <header className="border-b border-border/40 pb-6">
        <div className="flex items-start justify-between gap-4">
          <div className="flex items-start gap-4">
            <div className="rounded-xl bg-primary/10 p-3 text-primary ring-1 ring-primary/20 shadow-sm shadow-primary/5">
              <ShieldCheck size={26} strokeWidth={2} />
            </div>
            <div>
              <h1 className="text-3xl font-bold tracking-tight text-foreground">Terminal-Konfiguration</h1>
              <p className="mt-2 text-sm font-medium text-muted-foreground max-w-2xl leading-relaxed">
                Versionen anzeigen und eine einmalige Genehmigung für ein Ersatzterminal erstellen. Backups werden automatisch jede Nacht oder nach wichtigen Änderungen erstellt.
              </p>
            </div>
          </div>
        </div>
      </header>

      <div className="rounded-xl border border-primary/20 bg-primary/5 p-4 text-sm shadow-sm relative overflow-hidden">
        <div className="absolute left-0 top-0 bottom-0 w-1 bg-primary/40"></div>
        <div className="flex gap-3 items-start">
          <ShieldAlert size={20} className="shrink-0 text-primary mt-0.5" />
          <div className="text-muted-foreground leading-relaxed">
             <strong className="text-foreground font-semibold">Datenschutz & Sicherheit:</strong> WLAN-Passwörter und Gateway-Schlüssel werden verschlüsselt gespeichert und hier nie im Klartext angezeigt. Der Terminal-API-Schlüssel ist nicht im Backup enthalten; das Ersatzterminal behält seinen eigenen Schlüssel. Spieler, Guthaben, Ergebnisse und ausstehende Synchronisierungselemente sind nicht im Backup enthalten.
          </div>
        </div>
      </div>

      <div className="rounded-xl border border-border/50 bg-card shadow-sm flex flex-col overflow-hidden">
        <div className="flex items-center justify-between border-b border-border/40 bg-secondary/30 px-5 py-4">
          <div>
             <h2 className="text-lg font-bold text-card-foreground tracking-tight">Gespeicherte Backups</h2>
             <p className="mt-0.5 text-xs font-medium text-muted-foreground">Es werden nur sichere Metadaten angezeigt</p>
          </div>
          <button 
            type="button" 
            onClick={() => backupsQuery.refetch()} 
            disabled={backupsQuery.isFetching}
            className="flex items-center gap-2 rounded-lg bg-background px-3 py-1.5 text-sm font-medium text-muted-foreground hover:text-foreground hover:bg-secondary border border-border/50 transition-all disabled:opacity-50 shadow-sm"
             title="Aktualisieren"
            data-testid="button-refresh-backups"
          >
            <RefreshCw size={14} className={cn(backupsQuery.isFetching && "animate-spin text-primary")} />
             <span>Aktualisieren</span>
          </button>
        </div>

        <div className="relative">
          {backupsQuery.isLoading ? (
            <div className="divide-y divide-border/30">
              {[1, 2, 3].map((i) => (
                <div key={i} className="flex items-center justify-between p-5">
                  <div className="flex items-center gap-6 w-full">
                    <Skeleton className="h-6 w-32 bg-secondary/40 rounded-md" />
                    <Skeleton className="h-4 w-16 bg-secondary/40 rounded" />
                    <Skeleton className="h-4 w-24 bg-secondary/40 rounded" />
                    <Skeleton className="h-6 w-20 bg-secondary/40 rounded ml-auto" />
                  </div>
                </div>
              ))}
            </div>
          ) : (backupsQuery.data?.backups ?? []).length === 0 ? (
            <div className="flex flex-col items-center justify-center py-16 text-center px-4">
              <div className="bg-secondary/20 p-4 rounded-full mb-4 text-muted-foreground ring-1 ring-border/50">
                <HardDrive size={32} strokeWidth={1.5} />
              </div>
               <h3 className="text-base font-semibold text-foreground mb-1 tracking-tight">Keine Backups gefunden</h3>
              <p className="text-sm text-muted-foreground max-w-sm">
                 Derzeit sind keine Terminal-Backups verfügbar. Speicher- und Konfigurations-Backups werden hier automatisch angezeigt.
              </p>
            </div>
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full text-sm text-left border-collapse">
                <thead className="bg-secondary/10 border-b border-border/40 text-xs uppercase tracking-wider text-muted-foreground font-semibold">
                  <tr>
                    <th className="px-5 py-4 whitespace-nowrap">Terminal</th>
                    <th className="px-5 py-4 whitespace-nowrap">Schema</th>
                    <th className="px-5 py-4 whitespace-nowrap">Firmware</th>
                     <th className="px-5 py-4 whitespace-nowrap">Letztes Backup</th>
                     <th className="px-5 py-4 whitespace-nowrap">Wiederhergestellt</th>
                    <th className="px-5 py-4 whitespace-nowrap">Status</th>
                     <th className="px-5 py-4 text-right whitespace-nowrap">Aktionen</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-border/30">
                  {(backupsQuery.data?.backups ?? []).map((backup) => (
                    <tr key={backup.id} className="hover:bg-secondary/20 transition-colors group">
                      <td className="px-5 py-4 whitespace-nowrap">
                        <div className="flex items-center gap-2.5">
                          <Terminal size={15} className="text-primary/70" />
                          <span className="font-mono font-bold tracking-tight text-foreground" data-testid={`text-terminal-id-${backup.id}`}>
                            {backup.terminalId}
                          </span>
                        </div>
                      </td>
                      <td className="px-5 py-4 whitespace-nowrap font-mono text-xs text-muted-foreground font-medium">
                        v{backup.schemaVersion}
                      </td>
                      <td className="px-5 py-4 whitespace-nowrap text-muted-foreground">
                        {backup.firmwareVersion || <span className="opacity-40">—</span>}
                      </td>
                      <td className="px-5 py-4 whitespace-nowrap text-muted-foreground tabular-nums">
                        {formatDate(backup.updatedAt) || <span className="opacity-40">—</span>}
                      </td>
                      <td className="px-5 py-4 whitespace-nowrap text-muted-foreground tabular-nums">
                        {formatDate(backup.lastRestoredAt) || <span className="opacity-40">—</span>}
                      </td>
                      <td className="px-5 py-4 whitespace-nowrap">
                        {backup.revokedAt ? (
                          <Badge variant="destructive" className="bg-destructive/15 text-destructive hover:bg-destructive/25 border-0 font-medium">
                             Widerrufen
                          </Badge>
                        ) : (
                          <Badge className="bg-emerald-500/15 text-emerald-400 hover:bg-emerald-500/25 border-0 font-medium gap-1.5 px-2">
                            <CheckCircle2 size={12} strokeWidth={2.5} /> Aktiv
                          </Badge>
                        )}
                      </td>
                      <td className="px-5 py-4 whitespace-nowrap">
                        {!backup.revokedAt ? (
                          <div className="flex items-center justify-end gap-2 opacity-100 sm:opacity-0 sm:group-hover:opacity-100 focus-within:opacity-100 transition-opacity">
                            <button 
                              type="button" 
                              onClick={() => setAuthorizeId(backup.id)}
                              disabled={authorize.isPending} 
                              className="flex items-center gap-1.5 rounded-md bg-primary/10 px-3 py-1.5 text-xs font-bold text-primary hover:bg-primary/20 transition-colors disabled:opacity-50 ring-1 ring-primary/20 shadow-sm" 
                               title="Wiederherstellungscode erstellen"
                              data-testid={`button-authorize-restore-${backup.id}`}
                            >
                              <KeyRound size={14} /> Code
                            </button>
                            <button 
                              type="button" 
                              onClick={() => setRevokeId(backup.id)} 
                              className="rounded-md p-1.5 text-muted-foreground hover:bg-destructive/15 hover:text-destructive transition-colors ring-1 ring-transparent hover:ring-destructive/30" 
                               title="Backup widerrufen"
                              data-testid={`button-revoke-backup-${backup.id}`}
                            >
                              <Ban size={16} />
                            </button>
                          </div>
                        ) : (
                           <div className="flex justify-end text-xs text-muted-foreground/50 font-medium italic">
                             Keine Aktionen
                           </div>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
      </div>

      <Dialog open={authorizeId !== null} onOpenChange={(open) => !open && setAuthorizeId(null)}>
        <DialogContent className="sm:max-w-md border-border/50 bg-card shadow-2xl">
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2 text-xl tracking-tight">
              <KeyRound className="text-primary" size={20} />
               Ersatzterminal autorisieren
            </DialogTitle>
            <DialogDescription className="text-muted-foreground text-sm leading-relaxed pt-1.5">
               Der Code funktioniert nur für dieses Terminal mit diesem aktiven Terminal-API-Schlüssel.
            </DialogDescription>
          </DialogHeader>
          <div className="space-y-4 py-2">
            <label className="block space-y-2 text-sm font-semibold">
               <span>Terminal-ID des Ersatzterminals</span>
              <input
                value={targetTerminalId}
                onChange={(event) => setTargetTerminalId(event.target.value)}
                placeholder="ESP32-P4-..."
                className="w-full rounded-lg border border-border bg-background px-3 py-2 font-mono text-sm outline-none focus:ring-2 focus:ring-primary/40"
                data-testid="input-target-terminal-id"
              />
            </label>
            <label className="block space-y-2 text-sm font-semibold">
               <span>Terminal-API-Schlüssel</span>
              <select
                value={targetApiKeyId}
                onChange={(event) => setTargetApiKeyId(event.target.value)}
                className="w-full rounded-lg border border-border bg-background px-3 py-2 text-sm outline-none focus:ring-2 focus:ring-primary/40"
                data-testid="select-target-api-key"
              >
                 <option value="">Schlüssel auswählen</option>
                {(backupsQuery.data?.terminalKeys ?? []).map((key) => (
                  <option key={key.id} value={key.id}>{key.name}</option>
                ))}
              </select>
            </label>
          </div>
          <DialogFooter className="gap-2 sm:gap-0 border-t border-border/30 pt-4">
            <button
              type="button"
              onClick={() => setAuthorizeId(null)}
              className="rounded-lg px-4 py-2 text-sm font-semibold text-muted-foreground hover:bg-secondary/80"
            >
               Abbrechen
            </button>
            <button
              type="button"
              disabled={!targetTerminalId.trim() || !targetApiKeyId || authorize.isPending}
              onClick={() => authorizeId !== null && authorize.mutate({
                id: authorizeId,
                terminalId: targetTerminalId.trim(),
                apiKeyId: Number(targetApiKeyId),
              })}
              className="rounded-lg bg-primary px-5 py-2 text-sm font-bold text-primary-foreground disabled:opacity-50"
              data-testid="button-create-restore-code"
            >
              Code erstellen
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={!!code} onOpenChange={(open) => !open && setCode(null)}>
        <DialogContent className="sm:max-w-md border-border/50 bg-card shadow-2xl">
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2 text-xl tracking-tight">
              <KeyRound className="text-primary" size={20} />
              Restore-Code
            </DialogTitle>
            <DialogDescription className="text-muted-foreground text-sm leading-relaxed pt-1.5">
               Dieser Code wird nur einmal angezeigt und ist 15 Minuten gültig. Geben Sie ihn am Ersatzterminal ein, um eine vollständige Wiederherstellung zu aktivieren.
            </DialogDescription>
          </DialogHeader>
          
          <div className="my-2 rounded-xl border border-primary/20 bg-background p-6 text-center relative overflow-hidden group">
            <div className="absolute inset-0 bg-primary/[0.02] group-hover:bg-primary/[0.04] transition-colors" />
            <div 
              className="relative select-all font-mono text-4xl font-black tracking-[0.25em] text-primary"
              data-testid="text-restore-code"
            >
              {code?.value}
            </div>
            <div className="relative mt-4 flex items-center justify-center gap-1.5 text-xs font-medium text-muted-foreground">
              <Clock size={14} className="text-primary/70" />
               Gültig bis {formatDate(code?.expiresAt ?? null)}
            </div>
          </div>
          
          <DialogFooter className="gap-2 sm:gap-0 border-t border-border/30 pt-4 mt-2">
            <button 
              type="button" 
              onClick={() => setCode(null)} 
              className="rounded-lg px-4 py-2 text-sm font-semibold text-muted-foreground hover:bg-secondary/80 transition-colors"
              data-testid="button-close-code"
            >
               Schließen
            </button>
            <button 
              type="button" 
              onClick={copyCode} 
              className="flex items-center justify-center gap-2 rounded-lg bg-primary px-5 py-2 text-sm font-bold text-primary-foreground hover:bg-primary/90 transition-colors shadow-md shadow-primary/20"
              data-testid="button-copy-code"
            >
               <Copy size={16} strokeWidth={2.5} /> Code kopieren
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog open={revokeId !== null} onOpenChange={(open) => !open && setRevokeId(null)}>
        <DialogContent className="sm:max-w-md border-destructive/20 bg-card shadow-2xl">
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2 text-destructive text-xl tracking-tight">
              <AlertTriangle size={20} />
               Backup widerrufen?
            </DialogTitle>
            <DialogDescription className="text-muted-foreground pt-1.5 text-sm leading-relaxed">
               Dieses Backup wird dauerhaft als ungültig markiert. Aktive Wiederherstellungsgenehmigungen für dieses Backup werden sofort ungültig.
              <br/><br/>
               <strong className="text-foreground">Diese Aktion kann nicht rückgängig gemacht werden.</strong>
            </DialogDescription>
          </DialogHeader>
          
          <DialogFooter className="gap-2 sm:gap-0 border-t border-border/30 pt-4 mt-2">
            <button 
              type="button" 
              onClick={() => setRevokeId(null)} 
              className="rounded-lg px-4 py-2 text-sm font-semibold text-muted-foreground hover:bg-secondary/80 transition-colors"
              data-testid="button-cancel-revoke"
            >
               Abbrechen
            </button>
            <button 
              type="button" 
              onClick={() => revokeId !== null && revoke.mutate(revokeId)} 
              disabled={revoke.isPending} 
              className="flex items-center justify-center gap-2 rounded-lg bg-destructive px-5 py-2 text-sm font-bold text-destructive-foreground hover:bg-destructive/90 transition-colors shadow-md shadow-destructive/20 disabled:opacity-50"
              data-testid="button-confirm-revoke"
            >
              {revoke.isPending ? (
                 <>Wird widerrufen…</>
              ) : (
                <>
                   <Ban size={16} strokeWidth={2.5} /> Ja, widerrufen
                </>
              )}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
