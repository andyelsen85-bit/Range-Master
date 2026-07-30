import { useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { useToast } from "@/hooks/use-toast";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Badge } from "@/components/ui/badge";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogDescription, DialogFooter } from "@/components/ui/dialog";
import { Skeleton } from "@/components/ui/skeleton";
import { RefreshCw, Copy, Check, Monitor, Radio } from "lucide-react";

interface ApiKey {
  id: number;
  name: string;
  key: string;       // last 8 chars only — full key only returned on regenerate
  type: "EMULATOR" | "TERMINAL";
  active: boolean;
  createdAt: string;
}

interface RegeneratedKey {
  id: number;
  fullKey: string;   // one-time full value
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

export default function AdminApiKeys() {
  const qc = useQueryClient();
  const apiFetch = useAdminFetch();
  const { toast } = useToast();
  const [revealed, setRevealed] = useState<RegeneratedKey | null>(null);
  const [copied, setCopied] = useState(false);

  const { data, isLoading } = useQuery<{ keys: ApiKey[] }>({
    queryKey: ["admin-api-keys"],
    queryFn: () => apiFetch("/api/admin/api-keys"),
  });

  const toggleMut = useMutation({
    mutationFn: ({ id, active }: { id: number; active: boolean }) =>
      apiFetch(`/api/admin/api-keys/${id}`, { method: "PATCH", body: JSON.stringify({ active }) }),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["admin-api-keys"] }),
    onError: (e: Error) => toast({ title: "Feeler", description: e.message, variant: "destructive" }),
  });

  const regenMut = useMutation({
    mutationFn: (id: number) =>
      apiFetch(`/api/admin/api-keys/${id}/regenerate`, { method: "POST" }),
    onSuccess: (data: { id: number; key: string }) => {
      qc.invalidateQueries({ queryKey: ["admin-api-keys"] });
      setRevealed({ id: data.id, fullKey: data.key });
      setCopied(false);
    },
    onError: (e: Error) => toast({ title: "Feeler", description: e.message, variant: "destructive" }),
  });

  const handleCopy = async () => {
    if (!revealed) return;
    await navigator.clipboard.writeText(revealed.fullKey);
    setCopied(true);
    setTimeout(() => setCopied(false), 3000);
  };

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight">API Schlësselen</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">
          Sync-Schlësselen fir d'Emulator an d'Terminals. Regeneréieren ersetzt de Schlëssel direkt.
        </p>
      </header>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-3">
            {[1,2,3,4,5,6].map(i => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}
          </div>
        ) : (
          <div className="overflow-x-auto">
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Numm</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Typ</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Schlëssel (leschten 8)</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Aktiv</TableHead>
                  <TableHead className="w-36"></TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {(data?.keys ?? []).map((k) => (
                  <TableRow key={k.id} className="border-border/30 hover:bg-secondary/20 transition-colors">
                    <TableCell className="font-bold">{k.name}</TableCell>
                    <TableCell>
                      <Badge
                        variant="outline"
                        className={k.type === "EMULATOR"
                          ? "text-blue-400 border-blue-400/30 bg-blue-400/10 font-bold"
                          : "text-amber-400 border-amber-400/30 bg-amber-400/10 font-bold"}
                      >
                        {k.type === "EMULATOR"
                          ? <><Monitor size={11} className="mr-1.5" />Emulator</>
                          : <><Radio size={11} className="mr-1.5" />Terminal</>}
                      </Badge>
                    </TableCell>
                    <TableCell className="font-mono text-sm text-muted-foreground tracking-widest">
                      ••••••••••••••••<span className="text-foreground">{k.key}</span>
                    </TableCell>
                    <TableCell className="text-center">
                      <button
                        onClick={() => toggleMut.mutate({ id: k.id, active: !k.active })}
                        disabled={toggleMut.isPending}
                        className={`w-10 h-5 rounded-full transition-colors relative ${k.active ? "bg-primary" : "bg-secondary"}`}
                        title={k.active ? "Deaktivéieren" : "Aktivéieren"}
                      >
                        <div className={`absolute top-0.5 w-4 h-4 rounded-full bg-white shadow transition-transform ${k.active ? "translate-x-5" : "translate-x-0.5"}`} />
                      </button>
                    </TableCell>
                    <TableCell className="text-right">
                      <button
                        onClick={() => regenMut.mutate(k.id)}
                        disabled={regenMut.isPending}
                        title="Schlëssel regeneréieren"
                        className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-bold text-muted-foreground hover:text-foreground hover:bg-secondary rounded-lg transition-colors ml-auto"
                      >
                        <RefreshCw size={13} className={regenMut.isPending ? "animate-spin" : ""} />
                        Regeneréieren
                      </button>
                    </TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </div>
        )}
      </div>

      <div className="bg-secondary/20 border border-border/50 rounded-xl p-5">
        <h3 className="text-xs font-black uppercase tracking-widest text-muted-foreground mb-2">Wéi benotzen?</h3>
        <ol className="text-sm text-muted-foreground space-y-1 font-medium list-decimal list-inside">
          <li>Klickt <strong className="text-foreground">Regeneréieren</strong> fir e neie Schlëssel ze generéieren.</li>
          <li>Kopéiert de komplette Schlëssel mat dem Copy-Button (nëmmen eemol ugewisen).</li>
          <li>Am Emulator / Terminal: <strong className="text-foreground">Astellungen</strong> → API URL an Schlëssel aginn.</li>
          <li>De Schlëssel gëtt direkt aktiv a replazéiert den ale.</li>
        </ol>
      </div>

      {/* Reveal modal — shown once after regenerate */}
      <Dialog open={!!revealed} onOpenChange={(o) => !o && setRevealed(null)}>
        <DialogContent className="sm:max-w-lg">
          <DialogHeader>
            <DialogTitle>Neie Schlëssel generéiert ✓</DialogTitle>
            <DialogDescription>
              Kopéiert de Schlëssel elo — hien gëtt nëmmen <strong>eemol</strong> am Kloertext ugewisen.
            </DialogDescription>
          </DialogHeader>
          <div className="py-3">
            <div className="flex items-center gap-3 bg-background border border-border/60 rounded-xl p-4">
              <code className="flex-1 font-mono text-sm break-all text-primary font-bold tracking-wider">
                {revealed?.fullKey}
              </code>
              <button
                onClick={handleCopy}
                className={`shrink-0 flex items-center gap-2 px-4 py-2 rounded-lg font-bold text-sm transition-colors ${
                  copied ? "bg-green-500/20 text-green-400 border border-green-500/30" : "bg-secondary hover:bg-secondary/80 text-foreground"
                }`}
              >
                {copied ? <><Check size={14} /> Kopéiert!</> : <><Copy size={14} /> Kopéieren</>}
              </button>
            </div>
          </div>
          <DialogFooter>
            <button
              onClick={() => setRevealed(null)}
              className="px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors"
            >
              Fäerdeg
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}
