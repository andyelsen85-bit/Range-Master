import { Fragment, useMemo, useState } from "react";
import { keepPreviousData } from "@tanstack/react-query";
import { useGetTerminalLogs, exportTerminalLogs, getGetTerminalLogsQueryKey, type GetTerminalLogsParams, type TerminalLatencySummary } from "@workspace/api-client-react";
import { Activity, AlertTriangle, ChevronDown, ChevronRight, Download, RefreshCw, ScrollText, Search } from "lucide-react";
import { useToast } from "@/hooks/use-toast";
import { Skeleton } from "@/components/ui/skeleton";
import { cn } from "@/lib/utils";

const TYPES = ["fire_trace", "gateway_trace", "sync", "data_loss_risk", "ui_commit", "wifi"] as const;
const LEVELS = ["INFO", "WARN", "ERROR"] as const;
const PAGE = 100;

const TYPE_LABEL: Record<string, string> = {
  fire_trace: "Terminal: Tastendruck bis Endantwort",
  gateway_trace: "Gateway: Anfrage-Eingang bis Relais-ACK/Endergebnis",
  sync: "Synchronisierung",
  data_loss_risk: "Datenverlust-Risiko",
  ui_commit: "UI-Commit",
  wifi: "WLAN",
};

function fmtTs(v: string) {
  const d = new Date(v);
  return isNaN(d.getTime()) ? v : new Intl.DateTimeFormat("de-DE", { dateStyle: "short", timeStyle: "medium" }).format(d);
}
const ms = (n: number) => `${Math.round(n).toLocaleString("de-DE")} ms`;

function levelClass(l: string) {
  return l === "ERROR" ? "bg-destructive/15 text-destructive" : l === "WARN" ? "bg-amber-500/15 text-amber-400" : "bg-secondary text-muted-foreground";
}

const inputCls = "w-full rounded-lg border border-border bg-background px-3 py-2 text-sm outline-none focus:ring-2 focus:ring-primary/40";

function DailyChart({ rows, type, terminal }: { rows: TerminalLatencySummary[]; type: string; terminal: string }) {
  const days = useMemo(
    () => rows.filter((x) => x.type === type && x.terminalId === terminal).sort((a, b) => a.datum.localeCompare(b.datum)).slice(-30),
    [rows, type, terminal],
  );
  if (!terminal) return <p className="py-8 text-center text-sm text-muted-foreground">Kein Terminal mit Messwerten vorhanden.</p>;
  if (days.length === 0) return <p className="py-8 text-center text-sm text-muted-foreground">Keine Messwerte für {TYPE_LABEL[type]} auf {terminal} im gewählten Zeitraum.</p>;
  const top = Math.max(...days.map((d) => d.max), 1);
  const bars: { key: "median" | "p95" | "max"; cls: string }[] = [
    { key: "median", cls: "bg-primary" },
    { key: "p95", cls: "bg-primary/55" },
    { key: "max", cls: "bg-primary/25" },
  ];
  return (
    <div>
      <div className="flex h-44 items-end gap-2 border-b border-border/50 pt-4">
        {days.map((d) => (
          <div key={d.datum} className="flex h-full flex-1 items-end gap-px" title={`${d.datum}: Median ${ms(d.median)}, p95 ${ms(d.p95)}, Max ${ms(d.max)}, n=${d.count}`}>
            {bars.map((b) => <div key={b.key} className={cn("flex-1 rounded-t", b.cls)} style={{ height: `${(d[b.key] / top) * 100}%` }} />)}
          </div>
        ))}
      </div>
      <div className="mt-1 flex justify-between font-mono text-[10px] text-muted-foreground">
        <span>{days[0].datum}</span><span>Skala bis {ms(top)}</span><span>{days[days.length - 1].datum}</span>
      </div>
      <div className="mt-2 flex gap-4 text-xs text-muted-foreground">
        <span className="flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded-sm bg-primary" />Median</span>
        <span className="flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded-sm bg-primary/55" />p95</span>
        <span className="flex items-center gap-1.5"><span className="h-2.5 w-2.5 rounded-sm bg-primary/25" />Max</span>
      </div>
    </div>
  );
}

export default function AdminTerminalLogs() {
  const { toast } = useToast();
  const [terminal, setTerminal] = useState("");
  const [type, setType] = useState("");
  const [level, setLevel] = useState("");
  const [from, setFrom] = useState("");
  const [to, setTo] = useState("");
  const [q, setQ] = useState("");
  const [offset, setOffset] = useState(0);
  const [open, setOpen] = useState<Set<string>>(new Set());
  const [chartType, setChartType] = useState("fire_trace");
  const [exporting, setExporting] = useState(false);
  const [chartTerminalSel, setChartTerminal] = useState("");

  const filters = useMemo(() => {
    const p: GetTerminalLogsParams = {};
    if (terminal) p.terminal = terminal;
    if (type) p.type = type;
    if (level) p.level = level;
    if (from) p.from = new Date(from).toISOString();
    if (to) p.to = new Date(to).toISOString();
    if (q.trim()) p.q = q.trim();
    return p;
  }, [terminal, type, level, from, to, q]);
  const params = useMemo(() => ({ ...filters, offset, limit: PAGE }), [filters, offset]);

  const query = useGetTerminalLogs(params, { query: { queryKey: getGetTerminalLogsQueryKey(params), refetchInterval: 30_000, refetchIntervalInBackground: false, placeholderData: keepPreviousData } });
  const data = query.data;
  const set = (fn: (v: string) => void) => (v: string) => { fn(v); setOffset(0); };

  const toggle = (id: string) => setOpen((s) => { const n = new Set(s); n.has(id) ? n.delete(id) : n.add(id); return n; });

  const doExport = async () => {
    setExporting(true);
    try {
      const csv = await exportTerminalLogs(filters);
      const blob = new Blob([typeof csv === "string" ? csv : JSON.stringify(csv)], { type: "text/csv;charset=utf-8" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = `terminal-logs-${new Date().toISOString().slice(0, 10)}.csv`;
      a.click();
      URL.revokeObjectURL(a.href);
    } catch (e) {
      toast({ title: "Export fehlgeschlagen", description: (e as Error).message, variant: "destructive" });
    } finally { setExporting(false); }
  };

  const summaries = useMemo(() => [...(data?.summaries ?? [])].sort((a, b) => b.datum.localeCompare(a.datum) || a.terminalId.localeCompare(b.terminalId) || a.type.localeCompare(b.type)), [data?.summaries]);
  const total = data?.total ?? 0;
  const chartTerminals = useMemo(() => [...new Set((data?.summaries ?? []).map((s) => s.terminalId))].sort(), [data?.summaries]);
  const chartTerminal = chartTerminals.includes(chartTerminalSel) ? chartTerminalSel : (terminal && chartTerminals.includes(terminal) ? terminal : chartTerminals[0] ?? "");

  return (
    <div className="mx-auto max-w-6xl space-y-6 pb-10 animate-in fade-in duration-500">
      <header className="flex flex-col gap-4 border-b border-border/40 pb-6 sm:flex-row sm:items-start sm:justify-between">
        <div className="flex items-start gap-4">
          <div className="rounded-xl bg-primary/10 p-3 text-primary ring-1 ring-primary/20"><ScrollText size={26} /></div>
          <div>
            <h1 className="text-3xl font-bold tracking-tight">Terminal-Logs</h1>
            <p className="mt-2 max-w-2xl text-sm text-muted-foreground">Diagnosedaten der Terminals. Einträge mit Datenverlust-Risiko stehen immer oben. Aktualisiert alle 30 Sekunden, solange die Seite sichtbar ist.</p>
          </div>
        </div>
        <div className="flex gap-2">
          <button onClick={() => query.refetch()} disabled={query.isFetching} className="flex items-center gap-2 rounded-lg border border-border/50 bg-card px-3 py-2 text-sm font-medium text-muted-foreground hover:text-foreground disabled:opacity-50" data-testid="button-refresh-logs">
            <RefreshCw size={14} className={cn(query.isFetching && "animate-spin text-primary")} /> Aktualisieren
          </button>
          <button onClick={doExport} disabled={exporting} className="flex items-center gap-2 rounded-lg bg-primary px-3 py-2 text-sm font-bold text-primary-foreground disabled:opacity-50" data-testid="button-export-logs">
            <Download size={14} /> {exporting ? "Exportiert..." : "CSV-Export"}
          </button>
        </div>
      </header>

      <section className="grid grid-cols-1 gap-3 rounded-xl border border-border/50 bg-card p-4 sm:grid-cols-2 lg:grid-cols-6">
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">Terminal
          <select value={terminal} onChange={(e) => set(setTerminal)(e.target.value)} className={inputCls} data-testid="select-terminal">
            <option value="">Alle</option>
            {(data?.terminals ?? []).map((t) => <option key={t} value={t}>{t}</option>)}
          </select>
        </label>
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">Typ
          <select value={type} onChange={(e) => set(setType)(e.target.value)} className={inputCls} data-testid="select-type">
            <option value="">Alle</option>
            {TYPES.map((t) => <option key={t} value={t}>{t}</option>)}
          </select>
        </label>
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">Level
          <select value={level} onChange={(e) => set(setLevel)(e.target.value)} className={inputCls} data-testid="select-level">
            <option value="">Alle</option>
            {LEVELS.map((t) => <option key={t} value={t}>{t}</option>)}
          </select>
        </label>
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">Von
          <input type="datetime-local" value={from} onChange={(e) => set(setFrom)(e.target.value)} className={inputCls} data-testid="input-from" />
        </label>
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">Bis
          <input type="datetime-local" value={to} onChange={(e) => set(setTo)(e.target.value)} className={inputCls} data-testid="input-to" />
        </label>
        <label className="space-y-1 text-xs font-semibold text-muted-foreground">Freitext
          <div className="relative">
            <Search size={14} className="absolute left-2.5 top-1/2 -translate-y-1/2 text-muted-foreground" />
            <input value={q} onChange={(e) => set(setQ)(e.target.value)} placeholder="Nachricht, ID..." className={cn(inputCls, "pl-8")} data-testid="input-search" />
          </div>
        </label>
      </section>

      {query.isError ? (
        <div className="flex flex-col items-center gap-3 rounded-xl border border-destructive/30 bg-destructive/5 p-8 text-center">
          <AlertTriangle className="text-destructive" />
          <p className="text-sm">Logs konnten nicht geladen werden: {(query.error as Error)?.message}</p>
          <button onClick={() => query.refetch()} className="rounded-lg bg-destructive px-4 py-2 text-sm font-bold text-destructive-foreground">Erneut versuchen</button>
        </div>
      ) : (
        <>
          <section className="rounded-xl border border-border/50 bg-card p-5">
            <div className="mb-2 flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
              <div>
                <h2 className="flex items-center gap-2 text-lg font-bold"><Activity size={18} className="text-primary" /> Tagesverlauf der Latenzen</h2>
                <p className="text-xs text-muted-foreground">
                  {chartType === "fire_trace" ? "Gemessen am Terminal: vom Tastendruck bis zur endgültigen Antwort. Ein Relais-Impuls wird damit nicht bestätigt." : "Gemessen am Gateway: vom Eingang der Anfrage am Gateway bis zum Relais-ACK bzw. Endergebnis. Ein tatsächlicher Relais-Impuls wird nur bestätigt, wenn er in den Daten ausdrücklich gemeldet ist."}
                </p>
              </div>
              <div className="flex flex-wrap gap-2">
              <select value={chartTerminal} onChange={(e) => setChartTerminal(e.target.value)} className="rounded-lg border border-border bg-background px-2 py-1.5 font-mono text-xs" data-testid="select-chart-terminal">
                {chartTerminals.length === 0 && <option value="">Kein Terminal</option>}
                {chartTerminals.map((t) => <option key={t} value={t}>{t}</option>)}
              </select>
              <div className="flex rounded-lg border border-border/50 p-0.5 text-xs font-semibold">
                {["fire_trace", "gateway_trace"].map((t) => (
                  <button key={t} onClick={() => setChartType(t)} className={cn("rounded-md px-3 py-1.5", chartType === t ? "bg-primary/15 text-primary" : "text-muted-foreground")}>{t === "fire_trace" ? "Terminal" : "Gateway"}</button>
                ))}
              </div>
              </div>
            </div>
            {query.isLoading ? <Skeleton className="h-44 w-full bg-secondary/40" /> : <DailyChart rows={data?.summaries ?? []} type={chartType} terminal={chartTerminal} />}
          </section>

          <section className="overflow-hidden rounded-xl border border-border/50 bg-card">
            <div className="border-b border-border/40 bg-secondary/30 px-5 py-3">
              <h2 className="font-bold">Kennzahlen pro Terminal und Tag</h2>
              <p className="text-xs text-muted-foreground">Zeiten in Millisekunden. Nur Typen mit Zeitmessung.</p>
            </div>
            {query.isLoading ? <div className="space-y-2 p-4">{[1, 2, 3].map((i) => <Skeleton key={i} className="h-6 bg-secondary/40" />)}</div>
              : summaries.length === 0 ? <p className="p-6 text-center text-sm text-muted-foreground">Keine Zeitmessungen für diese Filter.</p>
              : (
                <div className="max-h-80 overflow-auto">
                  <table className="w-full text-sm">
                    <thead className="sticky top-0 bg-card text-left text-xs uppercase tracking-wider text-muted-foreground">
                      <tr><th className="px-4 py-2">Tag</th><th className="px-4 py-2">Terminal</th><th className="px-4 py-2">Messung</th><th className="px-4 py-2 text-right">n</th><th className="px-4 py-2 text-right">Median</th><th className="px-4 py-2 text-right">p95</th><th className="px-4 py-2 text-right">Max</th></tr>
                    </thead>
                    <tbody className="divide-y divide-border/30 tabular-nums">
                      {summaries.map((s) => (
                        <tr key={`${s.datum}-${s.terminalId}-${s.type}`}>
                          <td className="whitespace-nowrap px-4 py-2 font-mono text-xs">{s.datum}</td>
                          <td className="whitespace-nowrap px-4 py-2 font-mono text-xs">{s.terminalId}</td>
                          <td className="px-4 py-2 text-xs text-muted-foreground">{TYPE_LABEL[s.type] ?? s.type}</td>
                          <td className="px-4 py-2 text-right">{s.count}</td>
                          <td className="px-4 py-2 text-right">{ms(s.median)}</td>
                          <td className="px-4 py-2 text-right">{ms(s.p95)}</td>
                          <td className="px-4 py-2 text-right">{ms(s.max)}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              )}
          </section>

          <section className="overflow-hidden rounded-xl border border-border/50 bg-card">
            <div className="flex items-center justify-between border-b border-border/40 bg-secondary/30 px-5 py-3">
              <h2 className="font-bold">Einträge</h2>
              <span className="text-xs text-muted-foreground tabular-nums">{total.toLocaleString("de-DE")} gesamt</span>
            </div>
            {query.isLoading ? <div className="space-y-2 p-4">{[1, 2, 3, 4, 5].map((i) => <Skeleton key={i} className="h-8 bg-secondary/40" />)}</div>
              : (data?.entries ?? []).length === 0 ? (
                <div className="flex flex-col items-center py-14 text-center">
                  <ScrollText size={30} className="mb-3 text-muted-foreground" />
                  <h3 className="font-semibold">Keine Einträge</h3>
                  <p className="max-w-sm text-sm text-muted-foreground">Für diese Filter liegen keine Terminal-Logs vor. Terminals laden Logs nach der nächsten Synchronisierung hoch.</p>
                </div>
              ) : (
                <div className="overflow-x-auto">
                  <table className="w-full text-sm">
                    <thead className="text-left text-xs uppercase tracking-wider text-muted-foreground">
                      <tr><th className="w-8" /><th className="px-3 py-2">Zeit</th><th className="px-3 py-2">Terminal</th><th className="px-3 py-2">Typ</th><th className="px-3 py-2">Level</th><th className="px-3 py-2">Nachricht</th></tr>
                    </thead>
                    <tbody className="divide-y divide-border/30">
                      {data!.entries.map((e) => {
                        const isOpen = open.has(`${e.terminalId}:${e.id}`);
                        const risk = e.type === "data_loss_risk";
                        return (
                          <Fragment key={`${e.terminalId}:${e.id}`}>
                            <tr onClick={() => toggle(`${e.terminalId}:${e.id}`)} className={cn("cursor-pointer hover:bg-secondary/20", risk && "bg-destructive/5")} data-testid={`row-log-${e.id}`}>
                              <td className="pl-3">{isOpen ? <ChevronDown size={14} /> : <ChevronRight size={14} />}</td>
                              <td className="whitespace-nowrap px-3 py-2 font-mono text-xs tabular-nums">{fmtTs(e.ts)}</td>
                              <td className="whitespace-nowrap px-3 py-2 font-mono text-xs">{e.terminalId}</td>
                              <td className="whitespace-nowrap px-3 py-2"><span className={cn("rounded px-1.5 py-0.5 font-mono text-[11px]", risk ? "bg-destructive/20 font-bold text-destructive" : "bg-secondary/60 text-muted-foreground")}>{e.type}</span></td>
                              <td className="px-3 py-2"><span className={cn("rounded px-1.5 py-0.5 text-[11px] font-bold", levelClass(e.level))}>{e.level}</span></td>
                              <td className="min-w-[14rem] px-3 py-2">{e.msg}</td>
                            </tr>
                            {isOpen && (
                              <tr className="bg-background/60">
                                <td />
                                <td colSpan={5} className="px-3 py-3">
                                  <div className="mb-2 text-[11px] text-muted-foreground">ID <span className="font-mono">{e.id}</span> · empfangen {fmtTs(e.receivedAt)}</div>
                                  <pre className="max-h-72 overflow-auto rounded-lg border border-border/50 bg-card p-3 font-mono text-xs">{JSON.stringify(e.data, null, 2)}</pre>
                                </td>
                              </tr>
                            )}
                          </Fragment>
                        );
                      })}
                    </tbody>
                  </table>
                </div>
              )}
            {total > PAGE && (
              <div className="flex items-center justify-between border-t border-border/40 px-5 py-3 text-sm">
                <button disabled={offset === 0} onClick={() => setOffset(Math.max(0, offset - PAGE))} className="rounded-lg border border-border/50 px-3 py-1.5 disabled:opacity-40">Zurück</button>
                <span className="text-xs text-muted-foreground tabular-nums">{offset + 1}–{Math.min(offset + PAGE, total)} von {total}</span>
                <button disabled={offset + PAGE >= total} onClick={() => setOffset(offset + PAGE)} className="rounded-lg border border-border/50 px-3 py-1.5 disabled:opacity-40">Weiter</button>
              </div>
            )}
          </section>
        </>
      )}
    </div>
  );
}
