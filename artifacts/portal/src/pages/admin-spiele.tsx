import { useState } from "react";
import { Link } from "wouter";
import { keepPreviousData } from "@tanstack/react-query";
import { useGetManagedGames, getGetManagedGamesQueryKey, useListSpieler, Modus } from "@workspace/api-client-react";
import type { GetManagedGamesParams, GetManagedGamesHidden } from "@workspace/api-client-react";
import { modeLabel } from "@/components/scorecard";
import { Skeleton } from "@/components/ui/skeleton";
import { EyeOff, ChevronLeft, ChevronRight, AlertCircle, Search, Pencil, ListChecks } from "lucide-react";

export type ManagedGameRow = import("@workspace/api-client-react").ManagedGameRow;

const PAGE = 50;
export const fmtDateTime = (iso?: string | null) => iso ? new Date(iso).toLocaleString("de-LU", { day: "2-digit", month: "2-digit", year: "numeric", hour: "2-digit", minute: "2-digit" }) : "–";
const inputCls = "bg-background border border-border/60 rounded-lg px-3 py-2 text-sm focus:outline-none focus:border-primary";

export default function AdminSpiele() {
  const [from, setFrom] = useState("");
  const [to, setTo] = useState("");
  const [modus, setModus] = useState("");
  const [spielerId, setSpielerId] = useState("");
  const [hidden, setHidden] = useState<GetManagedGamesHidden>("all");
  const [q, setQ] = useState("");
  const [page, setPage] = useState(1);

  const params: GetManagedGamesParams = {
    page,
    ...(from ? { from } : {}), ...(to ? { to } : {}), ...(modus ? { modus } : {}),
    ...(spielerId ? { spielerId: Number(spielerId) } : {}), hidden, ...(q.trim() ? { q: q.trim() } : {}),
  };
  const { data, isLoading, isError, refetch, isFetching } = useGetManagedGames(params, {
    query: { queryKey: getGetManagedGamesQueryKey(params), placeholderData: keepPreviousData },
  });
  const players = useListSpieler();
  const list = data as { items?: ManagedGameRow[]; total?: number; page?: number } | undefined;
  const items = list?.items ?? [];
  const total = list?.total ?? 0;
  const pages = Math.max(1, Math.ceil(total / PAGE));
  const set = <T,>(fn: (v: T) => void) => (v: T) => { fn(v); setPage(1); };

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight flex items-center gap-3"><ListChecks className="text-primary" /> Spiele verwalten</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">Spiele oder Spieler aus Statistik und Anzeige ausblenden, Ergebnisse korrigieren. Geld, Tauben und Tagesabschluss bleiben unverändert.</p>
      </header>

      <div className="bg-card border border-border/50 rounded-xl p-4 grid gap-3 sm:grid-cols-2 lg:grid-cols-6" data-testid="filters-spiele">
        <label className="space-y-1 text-xs font-bold uppercase tracking-widest text-muted-foreground">Von
          <input type="date" value={from} onChange={(e) => set(setFrom)(e.target.value)} className={`${inputCls} w-full normal-case tracking-normal`} data-testid="input-filter-from" />
        </label>
        <label className="space-y-1 text-xs font-bold uppercase tracking-widest text-muted-foreground">Bis
          <input type="date" value={to} onChange={(e) => set(setTo)(e.target.value)} className={`${inputCls} w-full normal-case tracking-normal`} data-testid="input-filter-to" />
        </label>
        <label className="space-y-1 text-xs font-bold uppercase tracking-widest text-muted-foreground">Modus
          <select value={modus} onChange={(e) => set(setModus)(e.target.value)} className={`${inputCls} w-full normal-case tracking-normal`} data-testid="select-filter-modus">
            <option value="">Alle Modi</option>
            {Object.values(Modus).map((m) => <option key={m} value={m}>{modeLabel(m)}</option>)}
          </select>
        </label>
        <label className="space-y-1 text-xs font-bold uppercase tracking-widest text-muted-foreground">Spieler
          <select value={spielerId} onChange={(e) => set(setSpielerId)(e.target.value)} className={`${inputCls} w-full normal-case tracking-normal`} data-testid="select-filter-spieler">
            <option value="">Alle Spieler</option>
            {(players.data?.spieler ?? []).map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
          </select>
        </label>
        <label className="space-y-1 text-xs font-bold uppercase tracking-widest text-muted-foreground">Versteckt
          <select value={hidden} onChange={(e) => set(setHidden)(e.target.value as GetManagedGamesHidden)} className={`${inputCls} w-full normal-case tracking-normal`} data-testid="select-filter-hidden">
            <option value="all">Alle</option><option value="no">Nur sichtbare</option><option value="yes">Nur versteckte</option>
          </select>
        </label>
        <label className="space-y-1 text-xs font-bold uppercase tracking-widest text-muted-foreground">Suche
          <span className="relative block">
            <Search size={14} className="absolute left-3 top-1/2 -translate-y-1/2 text-muted-foreground" />
            <input value={q} onChange={(e) => set(setQ)(e.target.value)} placeholder="ID, Name, Terminal-ID" className={`${inputCls} w-full pl-8 normal-case tracking-normal`} data-testid="input-filter-q" />
          </span>
        </label>
      </div>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden">
        {isLoading ? (
          <div className="p-4 space-y-2">{Array.from({ length: 8 }, (_, i) => <Skeleton key={i} className="h-10 w-full bg-secondary/30" />)}</div>
        ) : isError ? (
          <div className="p-10 text-center space-y-3">
            <p className="text-destructive flex items-center justify-center gap-2"><AlertCircle size={16} /> Spiele konnten nicht geladen werden.</p>
            <button onClick={() => refetch()} className="px-3 py-1.5 rounded-lg border border-border/60 text-xs font-bold">Erneut versuchen</button>
          </div>
        ) : items.length === 0 ? (
          <div className="p-16 text-center text-muted-foreground font-medium">Keine Spiele für diese Filter.</div>
        ) : (
          <div className={`overflow-x-auto transition-opacity ${isFetching ? "opacity-70" : ""}`}>
            <table className="w-full text-sm">
              <thead>
                <tr className="text-left text-[10px] uppercase tracking-widest text-muted-foreground border-b border-border/50 bg-secondary/20">
                  <th className="px-4 py-3">ID</th><th className="px-3">Datum</th><th className="px-3">Modus</th><th className="px-3 text-right">Läufe</th>
                  <th className="px-3 text-right">Spieler</th><th className="px-3">Status</th><th className="px-3"></th>
                </tr>
              </thead>
              <tbody>
                {items.map((g) => (
                  <tr key={g.id} className={`border-b border-border/30 hover:bg-secondary/20 ${g.hidden_at ? "opacity-50" : ""}`} data-testid={`row-spiel-${g.id}`}>
                    <td className="px-4 py-2.5 font-mono font-bold">
                      <Link href={`/admin/spiele/${g.id}`} className="text-primary hover:underline" data-testid={`link-spiel-${g.id}`}>#{g.id}</Link>
                    </td>
                    <td className="px-3 font-mono text-xs">{fmtDateTime(g.completed_at ?? g.datum)}</td>
                    <td className="px-3">{modeLabel(g.modus)}</td>
                    <td className="px-3 text-right font-mono">{g.lauf}</td>
                    <td className="px-3 text-right font-mono">{g.playerCount ?? "–"}</td>
                    <td className="px-3 text-xs">{g.abgeschlossen === false ? "offen" : "abgeschlossen"}</td>
                    <td className="px-3 whitespace-nowrap">
                      {g.hidden_at && <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full bg-destructive/15 text-destructive text-[11px] font-bold mr-1"><EyeOff size={11} /> versteckt</span>}
                      {g.edited_at && <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded-full bg-amber-500/15 text-amber-400 text-[11px] font-bold"><Pencil size={11} /> korrigiert</span>}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
        <div className="flex items-center justify-between px-4 py-3 border-t border-border/50 text-xs text-muted-foreground">
          <span data-testid="text-spiele-total">{total} Spiele</span>
          <div className="flex items-center gap-2">
            <button disabled={page <= 1} onClick={() => setPage((p) => p - 1)} data-testid="button-page-prev" className="p-1.5 rounded border border-border/60 disabled:opacity-30"><ChevronLeft size={14} /></button>
            <span className="font-mono">Seite {page} / {pages}</span>
            <button disabled={page >= pages} onClick={() => setPage((p) => p + 1)} data-testid="button-page-next" className="p-1.5 rounded border border-border/60 disabled:opacity-30"><ChevronRight size={14} /></button>
          </div>
        </div>
      </div>
    </div>
  );
}
