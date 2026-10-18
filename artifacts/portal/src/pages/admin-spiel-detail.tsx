import { useMemo, useState } from "react";
import { Link, useParams } from "wouter";
import { useQueryClient } from "@tanstack/react-query";
import {
  useGetManagedGame, getGetManagedGameQueryKey,
  useChangeManagedGame, useChangeManagedPlayer, useEditManagedShot,
} from "@workspace/api-client-react";
import type { SharedScorecard } from "@workspace/api-client-react";
import { Scorecard, modeLabel, ScorecardSkeleton } from "@/components/scorecard";
import { errMsg } from "@/components/daily-report-settings";
import { useToast } from "@/hooks/use-toast";
import {
  AlertDialog, AlertDialogAction, AlertDialogCancel, AlertDialogContent, AlertDialogDescription,
  AlertDialogFooter, AlertDialogHeader, AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { fmtDateTime, type ManagedGameRow } from "@/pages/admin-spiele";
import { ArrowLeft, Eye, EyeOff, AlertCircle, History, Pencil, RotateCcw } from "lucide-react";
import { cn } from "@/lib/utils";

type Shot = import("@workspace/api-client-react").ManagedGameShot;
type Audit = import("@workspace/api-client-react").ManagedGameAudit;
type Detail = import("@workspace/api-client-react").ManagedGameDetail;
interface Pending { title: string; run: () => void }

const CONFIRM_TEXT = "Betrifft nur Statistik und Anzeige. Tagesabschluss, Tauben, Kredite und Abrechnung bleiben unverändert.";
const ACTION_LABEL: Record<string, string> = {
  HIDE_GAME: "Spiel versteckt", UNHIDE_GAME: "Spiel eingeblendet", HIDE_PLAYER: "Spieler entfernt",
  UNHIDE_PLAYER: "Spieler eingeblendet", EDIT_RESULT: "Ergebnis korrigiert",
};
/** Keys of everything that shows results; refreshed after every admin action. */
const RELATED = ["rangliste", "statistik", "spiele", "ergebnisse", "scorecard", "dashboard", "last-game", "verlauf"];

export default function AdminSpielDetail() {
  const params = useParams<{ id: string }>();
  const id = Number(params.id);
  const qc = useQueryClient();
  const { toast } = useToast();
  const { data, isLoading, isError, refetch } = useGetManagedGame(id, { query: { queryKey: getGetManagedGameQueryKey(id), enabled: id > 0 } });
  const detail = data as unknown as Detail | undefined;
  const [pending, setPending] = useState<Pending | null>(null);
  const [edits, setEdits] = useState<Record<number, { schuss1: boolean; schuss2: boolean; wiederholt: boolean }>>({});

  const after = (msg: string) => () => {
    qc.invalidateQueries({ predicate: (q) => RELATED.some((k) => String(q.queryKey[0]).toLowerCase().includes(k)) });
    toast({ title: msg });
  };
  const onError = (e: unknown) => toast({ title: "Fehler", description: errMsg(e), variant: "destructive" });
  const gameMut = useChangeManagedGame({ mutation: { onError } });
  const playerMut = useChangeManagedPlayer({ mutation: { onError } });
  const shotMut = useEditManagedShot({ mutation: { onError } });

  const shotsByPlayer = useMemo(() => {
    const m = new Map<number, Shot[]>();
    for (const s of detail?.shots ?? []) {
      const arr = m.get(s.spieler_id) ?? [];
      arr.push(s);
      m.set(s.spieler_id, arr);
    }
    for (const arr of m.values()) arr.sort((a, b) => a.lauf - b.lauf || a.taube - b.taube);
    return m;
  }, [detail?.shots]);

  if (isLoading) return <div className="space-y-4"><div className="h-10 w-64 bg-secondary/30 rounded animate-pulse" /><ScorecardSkeleton /></div>;
  if (isError || !detail) {
    return (
      <div className="p-10 text-center space-y-3">
        <p className="text-destructive flex items-center justify-center gap-2"><AlertCircle size={16} /> Spiel konnte nicht geladen werden.</p>
        <button onClick={() => refetch()} className="px-3 py-1.5 rounded-lg border border-border/60 text-xs font-bold">Erneut versuchen</button>
      </div>
    );
  }
  const g = detail.game;
  const gameHidden = !!g.hidden_at;

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      <Link href="/admin/spiele" className="inline-flex items-center gap-1.5 text-sm text-muted-foreground hover:text-foreground" data-testid="link-back-spiele"><ArrowLeft size={14} /> Alle Spiele</Link>
      <header className="flex flex-wrap items-end justify-between gap-4 border-b border-border/50 pb-5">
        <div>
          <h1 className="text-3xl font-bold tracking-tight flex items-center gap-3">
            Spiel #{g.id}
            {gameHidden && <span className="text-xs px-2 py-1 rounded-full bg-destructive/15 text-destructive font-bold flex items-center gap-1"><EyeOff size={12} /> versteckt</span>}
            {g.edited_at && <span className="text-xs px-2 py-1 rounded-full bg-amber-500/15 text-amber-400 font-bold flex items-center gap-1"><Pencil size={12} /> korrigiert</span>}
          </h1>
          <p className="text-sm text-muted-foreground mt-1 font-mono">{fmtDateTime(g.completed_at ?? g.datum)} · {modeLabel(g.modus)} · {g.lauf} {g.lauf === 1 ? "Lauf" : "Läufe"}{g.external_id ? ` · ${g.external_id}` : ""}</p>
        </div>
        <button data-testid="button-toggle-game-hidden" disabled={gameMut.isPending}
          onClick={() => setPending({
            title: gameHidden ? "Spiel einblenden?" : "Spiel verstecken?",
            run: () => gameMut.mutate({ id, action: gameHidden ? "unhide" : "hide" }, { onSuccess: after(gameHidden ? "Spiel eingeblendet" : "Spiel versteckt") }),
          })}
          className={cn("flex items-center gap-2 px-4 py-2.5 rounded-lg text-sm font-bold", gameHidden ? "bg-primary text-primary-foreground" : "bg-destructive/15 text-destructive border border-destructive/40")}>
          {gameHidden ? <><Eye size={16} /> Spiel einblenden</> : <><EyeOff size={16} /> Spiel verstecken</>}
        </button>
      </header>

      <div className={cn(gameHidden && "opacity-50")}>{detail.scorecard ? <Scorecard game={detail.scorecard} /> : <p className="text-muted-foreground">Noch keine Ergebnisse vorhanden.</p>}</div>

      <section className="space-y-3">
        <h2 className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Spieler und Ergebnisse</h2>
        {detail.players.map((p) => {
          const shots = shotsByPlayer.get(p.id) ?? [];
          return (
            <div key={p.id} className={cn("bg-card border border-border/50 rounded-xl overflow-hidden", (p.hidden || gameHidden) && "opacity-55")} data-testid={`card-managed-player-${p.id}`}>
              <div className="flex flex-wrap items-center justify-between gap-3 px-4 py-3 border-b border-border/40 bg-secondary/20">
                <p className="font-bold flex items-center gap-2">{p.name} {p.hidden && <span className="text-[11px] px-2 py-0.5 rounded-full bg-destructive/15 text-destructive">aus Statistik entfernt</span>}</p>
                <button data-testid={`button-toggle-player-${p.id}`} disabled={playerMut.isPending}
                  onClick={() => setPending({
                    title: p.hidden ? `${p.name} wieder einblenden?` : `${p.name} aus der Statistik entfernen?`,
                    run: () => playerMut.mutate({ id, spielerId: p.id, action: p.hidden ? "unhide" : "hide" }, { onSuccess: after(p.hidden ? "Wieder eingeblendet" : "Aus Statistik entfernt") }),
                  })}
                  className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg border border-border/60 text-xs font-bold hover:bg-secondary/40">
                  {p.hidden ? <><Eye size={14} /> Wieder einblenden</> : <><EyeOff size={14} /> Aus Statistik entfernen</>}
                </button>
              </div>
              {shots.length > 0 && (
                <div className="overflow-x-auto">
                  <table className="w-full text-sm">
                    <thead><tr className="text-[10px] uppercase tracking-widest text-muted-foreground text-left">
                      <th className="px-4 py-2">Lauf</th><th className="px-2">Taube</th><th className="px-2">Masch.</th><th className="px-2">Schuss 1</th><th className="px-2">Schuss 2</th><th className="px-2">Wiederholt</th><th className="px-2 text-right">Punkte</th><th className="px-4"></th>
                    </tr></thead>
                    <tbody>
                      {shots.map((s) => {
                        const e = edits[s.id];
                        const cur = e ?? { schuss1: s.schuss1, schuss2: s.schuss2, wiederholt: s.wiederholt };
                        const dirty = !!e && (e.schuss1 !== s.schuss1 || e.schuss2 !== s.schuss2 || e.wiederholt !== s.wiederholt);
                        const setField = (k: "schuss1" | "schuss2" | "wiederholt") => setEdits((m) => ({ ...m, [s.id]: { ...cur, [k]: !cur[k] } }));
                        return (
                          <tr key={s.id} className={cn("border-t border-border/30", dirty && "bg-amber-500/5")} data-testid={`row-shot-${s.id}`}>
                            <td className="px-4 py-1.5 font-mono">{s.lauf}</td>
                            <td className="px-2 font-mono">{s.taube}</td>
                            <td className="px-2 font-bold">{s.maschine}</td>
                            <td className="px-2"><ShotToggle on={cur.schuss1} onClick={() => setField("schuss1")} testid={`toggle-shot1-${s.id}`} /></td>
                            <td className="px-2"><ShotToggle on={cur.schuss2} onClick={() => setField("schuss2")} testid={`toggle-shot2-${s.id}`} /></td>
                            <td className="px-2"><ShotToggle on={cur.wiederholt} onClick={() => setField("wiederholt")} testid={`toggle-repeat-${s.id}`} labels={["nein", "ja"]} /></td>
                            <td className="px-2 text-right font-mono font-bold">{s.punkte}</td>
                            <td className="px-4 text-right whitespace-nowrap">
                              {dirty && (
                                <span className="inline-flex gap-1">
                                  <button data-testid={`button-shot-save-${s.id}`} disabled={shotMut.isPending}
                                    onClick={() => setPending({
                                      title: `Ergebnis korrigieren (${p.name}, Lauf ${s.lauf}, Taube ${s.taube})?`,
                                      run: () => shotMut.mutate({ id, ergebnisId: s.id, data: cur }, {
                                        onSuccess: () => { setEdits((m) => { const n = { ...m }; delete n[s.id]; return n; }); after("Ergebnis korrigiert")(); },
                                      }),
                                    })}
                                    className="px-2 py-1 rounded bg-primary text-primary-foreground text-xs font-bold">Ergebnis korrigieren</button>
                                  <button aria-label="Verwerfen" onClick={() => setEdits((m) => { const n = { ...m }; delete n[s.id]; return n; })}
                                    className="px-2 py-1 rounded border border-border/60"><RotateCcw size={12} /></button>
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
        })}
      </section>

      <section className="bg-card border border-border/50 rounded-xl p-4 space-y-2" data-testid="section-audit">
        <h2 className="text-xs font-bold uppercase tracking-widest text-muted-foreground flex items-center gap-2"><History size={14} /> Verlauf</h2>
        {detail.audit.length === 0 ? <p className="text-sm text-muted-foreground">Noch keine Änderungen.</p> : (
          <ul className="divide-y divide-border/30">
            {detail.audit.map((a) => (
              <li key={a.id} className="py-2 text-sm grid gap-1 md:grid-cols-[10rem_12rem_1fr]">
                <span className="font-mono text-xs text-muted-foreground">{fmtDateTime(a.at)}</span>
                <span className="font-bold">{ACTION_LABEL[a.action] ?? a.action}{a.spieler_id ? ` · ${detail.players.find((p) => p.id === a.spieler_id)?.name ?? `#${a.spieler_id}`}` : ""}{a.admin_user_id ? <span className="font-normal text-muted-foreground"> · Admin #{a.admin_user_id}</span> : null}</span>
                {(a.before != null || a.after != null) && (
                  <span className="font-mono text-[11px] text-muted-foreground break-all">
                    {a.before != null && <>vorher {JSON.stringify(a.before)}</>} {a.after != null && <>· nachher {JSON.stringify(a.after)}</>}
                  </span>
                )}
              </li>
            ))}
          </ul>
        )}
      </section>

      <AlertDialog open={!!pending} onOpenChange={(o) => !o && setPending(null)}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>{pending?.title}</AlertDialogTitle>
            <AlertDialogDescription data-testid="text-confirm-note">{CONFIRM_TEXT}</AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel data-testid="button-confirm-cancel">Abbrechen</AlertDialogCancel>
            <AlertDialogAction data-testid="button-confirm-ok" onClick={() => { pending?.run(); setPending(null); }}>Bestätigen</AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}

function ShotToggle({ on, onClick, testid, labels = ["Fehler", "Treffer"] }: { on: boolean; onClick: () => void; testid: string; labels?: [string, string] }) {
  return (
    <button type="button" onClick={onClick} data-testid={testid}
      className={cn("px-2 py-0.5 rounded text-xs font-bold border min-w-[4.5rem]", on ? "bg-primary/15 border-primary/50 text-primary" : "border-border/60 text-muted-foreground")}>
      {on ? labels[1] : labels[0]}
    </button>
  );
}
