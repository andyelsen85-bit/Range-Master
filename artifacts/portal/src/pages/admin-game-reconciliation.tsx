import { useMemo, useState } from "react";
import { useGetGameReconciliation, getGetGameReconciliationQueryKey, type GetGameReconciliationParams } from "@workspace/api-client-react";
import { AlertTriangle, CheckCircle2, GitCompareArrows, RefreshCw } from "lucide-react";
import { Skeleton } from "@/components/ui/skeleton";
import { cn } from "@/lib/utils";

const inputCls = "w-full rounded-lg border border-border bg-background px-3 py-2 text-sm outline-none focus:ring-2 focus:ring-primary/40";

export default function AdminGameReconciliation() {
  const [terminal, setTerminal] = useState("");
  const [datum, setDatum] = useState("");
  const params = useMemo(() => {
    const p: GetGameReconciliationParams = {};
    if (terminal.trim()) p.terminal = terminal.trim();
    if (datum) p.datum = datum;
    return p;
  }, [terminal, datum]);
  const query = useGetGameReconciliation(params, { query: { queryKey: getGetGameReconciliationQueryKey(params), refetchInterval: 30_000, refetchIntervalInBackground: false } });
  const items = query.data?.items ?? [];
  const missingTotal = items.reduce((s, i) => s + i.missingIds.length, 0);

  return (
    <div className="mx-auto max-w-6xl space-y-6 pb-10 animate-in fade-in duration-500">
      <header className="flex flex-col gap-4 border-b border-border/40 pb-6 sm:flex-row sm:items-start sm:justify-between">
        <div className="flex items-start gap-4">
          <div className="rounded-xl bg-primary/10 p-3 text-primary ring-1 ring-primary/20"><GitCompareArrows size={26} /></div>
          <div>
            <h1 className="text-3xl font-bold tracking-tight">Spiele-Abgleich</h1>
            <p className="mt-2 max-w-2xl text-sm text-muted-foreground">Vergleicht die Spielliste jedes Terminals mit den im Portal angekommenen Spielen. Fehlende IDs wurden vom Terminal gemeldet, sind aber noch nicht synchronisiert.</p>
          </div>
        </div>
        <button onClick={() => query.refetch()} disabled={query.isFetching} className="flex items-center gap-2 self-start rounded-lg border border-border/50 bg-card px-3 py-2 text-sm font-medium text-muted-foreground hover:text-foreground disabled:opacity-50" data-testid="button-refresh-reconciliation">
          <RefreshCw size={14} className={cn(query.isFetching && "animate-spin text-primary")} /> Aktualisieren
        </button>
      </header>

      <section className="grid grid-cols-1 gap-3 rounded-xl border border-border/50 bg-card p-4 sm:grid-cols-3">
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">Terminal
          <input value={terminal} onChange={(e) => setTerminal(e.target.value)} placeholder="Alle Terminals" className={cn(inputCls, "font-mono")} data-testid="input-terminal" />
        </label>
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">Datum
          <input type="date" value={datum} onChange={(e) => setDatum(e.target.value)} className={inputCls} data-testid="input-datum" />
        </label>
        <div className="flex items-end">
          <div className={cn("w-full rounded-lg px-3 py-2 text-sm font-semibold", missingTotal ? "bg-destructive/10 text-destructive" : "bg-emerald-500/10 text-emerald-400")}>
            {query.isLoading ? "Wird geladen..." : missingTotal ? `${missingTotal} fehlende Spiele` : "Keine fehlenden Spiele"}
          </div>
        </div>
      </section>

      {query.isError ? (
        <div className="flex flex-col items-center gap-3 rounded-xl border border-destructive/30 bg-destructive/5 p-8 text-center">
          <AlertTriangle className="text-destructive" />
          <p className="text-sm">Abgleich konnte nicht geladen werden: {(query.error as Error)?.message}</p>
          <button onClick={() => query.refetch()} className="rounded-lg bg-destructive px-4 py-2 text-sm font-bold text-destructive-foreground">Erneut versuchen</button>
        </div>
      ) : query.isLoading ? (
        <div className="space-y-3">{[1, 2, 3].map((i) => <Skeleton key={i} className="h-20 bg-secondary/40" />)}</div>
      ) : items.length === 0 ? (
        <div className="flex flex-col items-center rounded-xl border border-border/50 bg-card py-14 text-center">
          <GitCompareArrows size={30} className="mb-3 text-muted-foreground" />
          <h3 className="font-semibold">Keine Spiellisten vorhanden</h3>
          <p className="max-w-sm text-sm text-muted-foreground">Für diese Filter hat noch kein Terminal eine Spielliste hochgeladen.</p>
        </div>
      ) : (
        <div className="space-y-3">
          {items.map((it) => {
            const ok = it.missingIds.length === 0;
            const pct = it.total ? (it.present / it.total) * 100 : 100;
            return (
              <article key={`${it.terminalId}-${it.datum}`} className={cn("rounded-xl border bg-card p-4", ok ? "border-border/50" : "border-destructive/30")} data-testid={`card-reconciliation-${it.terminalId}-${it.datum}`}>
                <div className="flex flex-wrap items-center justify-between gap-3">
                  <div className="flex items-center gap-3">
                    {ok ? <CheckCircle2 size={18} className="text-emerald-400" /> : <AlertTriangle size={18} className="text-destructive" />}
                    <span className="font-mono font-bold">{it.terminalId}</span>
                    <span className="font-mono text-sm text-muted-foreground">{it.datum}</span>
                  </div>
                  <div className="text-sm tabular-nums"><strong>{it.present}</strong><span className="text-muted-foreground"> / {it.total} im Portal</span></div>
                </div>
                <div className="mt-3 h-1.5 overflow-hidden rounded-full bg-secondary">
                  <div className={cn("h-full", ok ? "bg-emerald-500" : "bg-destructive")} style={{ width: `${pct}%` }} />
                </div>
                {!ok && (
                  <div className="mt-3">
                    <p className="mb-1.5 text-xs font-semibold text-destructive">{it.missingIds.length} fehlende Spiel-IDs</p>
                    <div className="flex max-h-32 flex-wrap gap-1.5 overflow-auto">
                      {it.missingIds.map((id) => <code key={id} className="select-all rounded bg-destructive/10 px-1.5 py-0.5 text-[11px] text-destructive">{id}</code>)}
                    </div>
                  </div>
                )}
                <p className="mt-2 text-[11px] text-muted-foreground">Liste aktualisiert {new Intl.DateTimeFormat("de-DE", { dateStyle: "short", timeStyle: "short" }).format(new Date(it.updatedAt))}</p>
              </article>
            );
          })}
        </div>
      )}
    </div>
  );
}
