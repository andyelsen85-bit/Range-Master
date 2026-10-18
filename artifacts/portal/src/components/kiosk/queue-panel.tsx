import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { keepPreviousData, useQueryClient } from "@tanstack/react-query";
import { useLocation } from "wouter";
import {
  useGetKioskPlayers, getGetKioskPlayersQueryKey,
  useGetKioskGroups, getGetKioskGroupsQueryKey,
  useGetKioskPrintDays, getGetKioskPrintDaysQueryKey,
  getGetKioskQueueQueryKey,
  useKioskSignup, useKioskCancel,
  useCreateKioskGroup, useUpdateKioskGroup, useDeleteKioskGroup, useKioskGroupSignup,
} from "@workspace/api-client-react";
import type { QueueSignupInputModus } from "@workspace/api-client-react";
import { modeLabel } from "@/components/scorecard";
import { cn } from "@/lib/utils";
import { apiErrorText, formatDay, reportKioskError, todayIso } from "@/lib/kiosk";
import { KioskKeyboard } from "./keyboard";
import {
  asQueue, parseDays, useKioskQueue, MAX_PLAYERS,
  type KioskGroup, type PickerPlayer, type QueueGame, type QueuePlayer,
} from "./types";
import {
  UserPlus, Users, ArrowLeft, Check, Printer, Search, Lock, Pencil, Trash2, Plus, AlertTriangle, X, LogOut,
} from "lucide-react";

type View = "start" | "single" | "print" | "groups" | "groupEdit" | "done";
interface Selected { id: number; name: string }
interface Draft { id: number | null; name: string; nameTouched: boolean; members: Selected[] }

export function QueuePanel({ enabled, resetSignal }: { enabled: boolean; resetSignal: number }) {
  const qc = useQueryClient();
  const [, setLocation] = useLocation();
  const queueQ = useKioskQueue(enabled);
  useEffect(() => { if (queueQ.error) reportKioskError(queueQ.error); }, [queueQ.error]);
  const queue = asQueue(queueQ.data);
  const settings = queue?.settings;
  const modes = settings?.enabledModes?.length ? settings.enabledModes : ["NORMAL"];

  // ── Local UI state: never derived from query data ──────────────────────────
  const [view, setView] = useState<View>("start");
  const [search, setSearch] = useState("");
  const [selected, setSelected] = useState<Selected | null>(null);
  const [modus, setModus] = useState<string>("NORMAL");
  const [message, setMessage] = useState<string | null>(null);
  const [doneText, setDoneText] = useState("");
  const [cancelEntry, setCancelEntry] = useState<QueuePlayer | null>(null);
  const [groupId, setGroupId] = useState<number | null>(null);
  const [confirmDelete, setConfirmDelete] = useState(false);
  const [draft, setDraft] = useState<Draft>({ id: null, name: "", nameTouched: false, members: [] });
  const [kbTarget, setKbTarget] = useState<"search" | "name">("search");
  const [printDay, setPrintDay] = useState<string | null>(null);

  const reset = useCallback(() => {
    setView("start"); setSearch(""); setSelected(null); setModus("NORMAL"); setMessage(null);
    setCancelEntry(null); setGroupId(null); setConfirmDelete(false); setKbTarget("search");
    setDraft({ id: null, name: "", nameTouched: false, members: [] }); setPrintDay(null);
  }, []);

  const firstReset = useRef(resetSignal);
  useEffect(() => {
    if (firstReset.current !== resetSignal) reset();
  }, [resetSignal, reset]);

  useEffect(() => {
    if (view !== "done") return;
    const t = setTimeout(reset, 5_000);
    return () => clearTimeout(t);
  }, [view, reset]);

  const invalidate = () => {
    qc.invalidateQueries({ queryKey: getGetKioskQueueQueryKey() });
    qc.invalidateQueries({ queryKey: getGetKioskGroupsQueryKey() });
    qc.invalidateQueries({ predicate: (q) => String(q.queryKey[0]).includes("/queue/players") });
  };

  const pickerOn = enabled && (view === "single" || view === "groupEdit");
  const picker = useGetKioskPlayers({ q: search }, {
    query: {
      queryKey: getGetKioskPlayersQueryKey({ q: search }),
      enabled: pickerOn,
      placeholderData: keepPreviousData,
      refetchInterval: pickerOn ? 10_000 : false,
    },
  });
  const players = useMemo(
    () => (((picker.data as { players?: PickerPlayer[] } | undefined)?.players) ?? []),
    [picker.data],
  );

  const groupsOn = enabled && !!settings?.groupsEnabled && (view === "groups" || view === "groupEdit");
  const groupsQ = useGetKioskGroups({
    query: { queryKey: getGetKioskGroupsQueryKey(), enabled: groupsOn, placeholderData: keepPreviousData, refetchInterval: groupsOn ? 10_000 : false },
  });
  const groups = (((groupsQ.data as { groups?: KioskGroup[] } | undefined)?.groups) ?? []);

  const signup = useKioskSignup({
    mutation: {
      onSuccess: () => { invalidate(); setDoneText(`${selected?.name ?? ""} ist eingeschrieben.`); setView("done"); },
      onError: (e) => { reportKioskError(e); setMessage(apiErrorText(e)); invalidate(); },
    },
  });
  const cancel = useKioskCancel({
    mutation: {
      onSuccess: () => { invalidate(); setDoneText(`${cancelEntry?.name ?? ""} ist ausgetragen.`); setCancelEntry(null); setView("done"); },
      onError: (e) => { reportKioskError(e); setMessage(apiErrorText(e)); invalidate(); },
    },
  });
  const createGroup = useCreateKioskGroup({
    mutation: {
      onSuccess: () => { invalidate(); setView("groups"); setMessage(null); setSearch(""); },
      onError: (e) => { reportKioskError(e); setMessage(apiErrorText(e)); },
    },
  });
  const updateGroup = useUpdateKioskGroup({
    mutation: {
      onSuccess: () => { invalidate(); setView("groups"); setMessage(null); setSearch(""); },
      onError: (e) => { reportKioskError(e); setMessage(apiErrorText(e)); },
    },
  });
  const deleteGroup = useDeleteKioskGroup({
    mutation: {
      onSuccess: () => { invalidate(); setGroupId(null); setConfirmDelete(false); setMessage(null); },
      onError: (e) => { reportKioskError(e); setMessage(apiErrorText(e)); },
    },
  });
  const groupSignup = useKioskGroupSignup({
    mutation: {
      onSuccess: () => {
        invalidate();
        const g = groups.find((x) => x.id === groupId);
        setDoneText(`${g?.name ?? "Die Gruppe"} ist eingeschrieben.`);
        setView("done");
      },
      onError: (e) => { reportKioskError(e); setMessage(apiErrorText(e)); invalidate(); },
    },
  });

  if (!enabled) return null;

  const busy = signup.isPending || cancel.isPending || groupSignup.isPending || createGroup.isPending || updateGroup.isPending || deleteGroup.isPending;
  const games = queue?.games ?? [];

  // Inline state about the chosen player after refetches (never closes the view).
  const selPlayer = selected ? players.find((p) => p.id === selected.id) : undefined;
  const selMissing = !!selected && !picker.isFetching && picker.isSuccess && !selPlayer;
  const cancelGone = !!cancelEntry && queue && !games.some((g) => g.status === "OPEN" && g.players.some((p) => p.entryId === cancelEntry.entryId));
  const selGroup = groupId != null ? groups.find((g) => g.id === groupId) : undefined;
  const groupGone = groupId != null && groupsQ.isSuccess && !selGroup;

  const header = (title: string, back?: () => void) => (
    <div className="flex items-center gap-3 shrink-0">
      {back && (
        <button type="button" onClick={back} data-testid="button-kiosk-back"
          className="kiosk-key h-14 w-14 rounded-xl flex items-center justify-center" aria-label="Zurück">
          <ArrowLeft size={28} />
        </button>
      )}
      <h2 className="text-3xl font-black tracking-tight">{title}</h2>
    </div>
  );

  const searchBox = (label: string) => (
    <button type="button" onClick={() => setKbTarget("search")} data-testid="input-kiosk-search"
      className={cn("w-full h-16 rounded-xl border-2 px-4 flex items-center gap-3 text-left text-2xl bg-background/60",
        kbTarget === "search" ? "border-primary" : "border-border")}>
      <Search size={26} className="text-muted-foreground shrink-0" />
      <span className={cn("truncate", !search && "text-muted-foreground")} data-testid="text-kiosk-search">{search || label}</span>
      {kbTarget === "search" && <span className="kiosk-caret" />}
    </button>
  );

  const modeChooser = modes.length > 1 && (
    <div className="flex flex-wrap gap-2" data-testid="group-modes">
      {modes.map((m) => (
        <button key={m} type="button" onClick={() => setModus(m)} data-testid={`button-modus-${m}`}
          className={cn("h-14 px-5 rounded-xl text-xl font-bold border-2",
            modus === m ? "border-primary bg-primary/15 text-primary" : "border-border text-muted-foreground")}>
          {modeLabel(m)}
        </button>
      ))}
    </div>
  );

  // ── Views ──────────────────────────────────────────────────────────────────
  let body: React.ReactNode;

  if (view === "done") {
    body = (
      <div className="flex-1 flex flex-col items-center justify-center text-center gap-6" data-testid="view-kiosk-done">
        <div className="h-28 w-28 rounded-full bg-primary/15 text-primary flex items-center justify-center kiosk-pop"><Check size={64} strokeWidth={3} /></div>
        <p className="text-4xl font-black tracking-tight">{doneText}</p>
        <p className="text-xl text-muted-foreground">Gleich geht es zurück zur Übersicht.</p>
      </div>
    );
  } else if (view === "single") {
    const blockedRunning = selPlayer?.running;
    const blockedActive = selPlayer?.activeSignup && !selPlayer?.running;
    body = (
      <div className="flex-1 min-h-0 flex flex-col gap-3" data-testid="view-kiosk-single">
        {header("Ich schreibe mich ein", reset)}
        {searchBox("Namen eintippen…")}
        <PlayerList players={players} loading={picker.isLoading} selectedIds={selected ? [selected.id] : []}
          onTap={(p) => { setSelected({ id: p.id, name: p.displayName }); setMessage(null); }} />
        {selected && (
          <div className="shrink-0 rounded-xl border border-primary/40 bg-primary/5 p-4 space-y-3" data-testid="panel-kiosk-selected">
            <p className="text-2xl font-black" data-testid="text-kiosk-selected">{selected.name}</p>
            {blockedRunning && <InlineNote>Du schießt gerade. Erneut einschreiben ist nach deinem Ergebnis möglich.</InlineNote>}
            {blockedActive && <InlineNote>Du bist bereits eingeschrieben. Zum Austragen tippe deinen Namen in der Warteschlange an.</InlineNote>}
            {selMissing && <InlineNote>Dieser Name ist in der aktuellen Liste nicht mehr verfügbar – eventuell wurde er gerade eingeschrieben.</InlineNote>}
            {message && <InlineNote tone="error">{message}</InlineNote>}
            {settings?.queueEnabled && !blockedRunning && !blockedActive && modeChooser}
            <div className="flex flex-wrap gap-3">
              {settings?.queueEnabled && (
                <button type="button" disabled={busy || !!blockedRunning || !!blockedActive || selMissing}
                  onClick={() => { setMessage(null); signup.mutate({ data: { spielerId: selected.id, modus: (modes.length > 1 ? modus : modes[0]) as QueueSignupInputModus } }); }}
                  data-testid="button-kiosk-signup"
                  className="kiosk-primary h-16 px-8 rounded-xl text-2xl font-black flex items-center gap-3 disabled:opacity-40">
                  <UserPlus size={28} /> {signup.isPending ? "Wird eingeschrieben…" : "Einschreiben"}
                </button>
              )}
              <button type="button" disabled={busy} onClick={() => { setMessage(null); setPrintDay(null); setView("print"); }} data-testid="button-kiosk-print"
                className="kiosk-key h-16 px-6 rounded-xl text-xl font-bold flex items-center gap-3">
                <Printer size={26} /> Meine Ergebnisse drucken
              </button>
            </div>
          </div>
        )}
        {!selected && <KioskKeyboard value={search} onChange={setSearch} className="shrink-0" />}
      </div>
    );
  } else if (view === "print" && selected) {
    body = <PrintDayPicker player={selected} day={printDay} onDay={setPrintDay} onBack={() => setView("single")}
      onPreview={(d) => setLocation(`/live/print/${selected.id}?tag=${d}`)} header={header} />;
  } else if (view === "groups") {
    body = (
      <div className="flex-1 min-h-0 flex flex-col gap-3" data-testid="view-kiosk-groups">
        {header("Gruppen", reset)}
        <div className="flex-1 min-h-0 overflow-y-auto kiosk-scroll space-y-2 pr-1">
          {groupsQ.isLoading && <SkeletonRows />}
          {groupsQ.isSuccess && groups.length === 0 && (
            <p className="text-xl text-muted-foreground py-8 text-center">Noch keine Gruppe angelegt.</p>
          )}
          {groups.map((g) => (
            <button key={g.id} type="button" onClick={() => { setGroupId(g.id); setConfirmDelete(false); setMessage(null); }}
              data-testid={`button-group-${g.id}`}
              className={cn("w-full text-left rounded-xl border-2 p-4 min-h-[56px]",
                g.id === groupId ? "border-primary bg-primary/10" : "border-border bg-card/60")}>
              <p className="text-2xl font-bold">{g.name} <span className="text-lg text-muted-foreground font-mono">{g.members.length}/{MAX_PLAYERS}</span></p>
              <p className="text-lg text-muted-foreground truncate">{g.members.map((m) => m.displayName).join(", ")}</p>
            </button>
          ))}
        </div>
        {groupGone && <InlineNote>Diese Gruppe gibt es nicht mehr.</InlineNote>}
        {message && <InlineNote tone="error">{message}</InlineNote>}
        {selGroup && (
          <div className="shrink-0 rounded-xl border border-primary/40 bg-primary/5 p-4 space-y-3" data-testid="panel-group-selected">
            {settings?.queueEnabled && modeChooser}
            {confirmDelete ? (
              <div className="flex flex-wrap items-center gap-3">
                <span className="text-xl font-bold">Gruppe „{selGroup.name}“ löschen?</span>
                <button type="button" disabled={busy} onClick={() => deleteGroup.mutate({ id: selGroup.id })} data-testid="button-group-delete-confirm"
                  className="h-14 px-6 rounded-xl bg-destructive text-destructive-foreground text-xl font-black">Löschen</button>
                <button type="button" onClick={() => setConfirmDelete(false)} className="kiosk-key h-14 px-6 rounded-xl text-xl font-bold">Abbrechen</button>
              </div>
            ) : (
              <div className="flex flex-wrap gap-3">
                {settings?.queueEnabled && (
                  <button type="button" disabled={busy}
                    onClick={() => { setMessage(null); groupSignup.mutate({ id: selGroup.id, data: { modus: (modes.length > 1 ? modus : modes[0]) as QueueSignupInputModus } }); }}
                    data-testid="button-group-signup"
                    className="kiosk-primary h-16 px-6 rounded-xl text-2xl font-black flex items-center gap-3 disabled:opacity-40">
                    <Users size={26} /> {groupSignup.isPending ? "Wird eingeschrieben…" : "Gruppe einschreiben"}
                  </button>
                )}
                <button type="button" onClick={() => {
                  setDraft({ id: selGroup.id, name: selGroup.name, nameTouched: true, members: selGroup.members.map((m) => ({ id: m.id, name: m.displayName })) });
                  setSearch(""); setKbTarget("search"); setMessage(null); setView("groupEdit");
                }} data-testid="button-group-edit" className="kiosk-key h-16 px-5 rounded-xl text-xl font-bold flex items-center gap-2">
                  <Pencil size={22} /> Bearbeiten
                </button>
                <button type="button" onClick={() => setConfirmDelete(true)} data-testid="button-group-delete"
                  className="kiosk-key h-16 px-5 rounded-xl text-xl font-bold flex items-center gap-2 text-destructive">
                  <Trash2 size={22} /> Löschen
                </button>
              </div>
            )}
          </div>
        )}
        <button type="button" onClick={() => {
          setDraft({ id: null, name: "", nameTouched: false, members: [] }); setSearch(""); setKbTarget("search"); setMessage(null); setView("groupEdit");
        }} data-testid="button-group-new" className="kiosk-key shrink-0 h-16 rounded-xl text-2xl font-bold flex items-center justify-center gap-3">
          <Plus size={28} /> Neue Gruppe
        </button>
      </div>
    );
  } else if (view === "groupEdit") {
    const toggle = (p: PickerPlayer) => {
      setMessage(null);
      setDraft((d) => {
        const has = d.members.some((m) => m.id === p.id);
        if (!has && d.members.length >= MAX_PLAYERS) return d;
        const members = has ? d.members.filter((m) => m.id !== p.id) : [...d.members, { id: p.id, name: p.displayName }];
        const name = d.nameTouched ? d.name : (members[0] ? `Gruppe ${members[0].name}` : "");
        return { ...d, members, name };
      });
    };
    const canSave = draft.name.trim().length > 0 && draft.members.length >= 2 && draft.members.length <= MAX_PLAYERS;
    const save = () => {
      const data = { name: draft.name.trim(), spielerIds: draft.members.map((m) => m.id) };
      if (draft.id == null) createGroup.mutate({ data });
      else updateGroup.mutate({ id: draft.id, data });
    };
    const editGone = draft.id != null && groupsQ.isSuccess && !groups.some((g) => g.id === draft.id);
    body = (
      <div className="flex-1 min-h-0 flex flex-col gap-3" data-testid="view-kiosk-group-edit">
        {header(draft.id == null ? "Neue Gruppe" : "Gruppe bearbeiten", () => { setView("groups"); setMessage(null); })}
        <button type="button" onClick={() => setKbTarget("name")} data-testid="input-group-name"
          className={cn("w-full h-16 rounded-xl border-2 px-4 flex items-center gap-3 text-left text-2xl bg-background/60",
            kbTarget === "name" ? "border-primary" : "border-border")}>
          <span className="text-lg text-muted-foreground shrink-0">Name</span>
          <span className={cn("truncate font-bold", !draft.name && "text-muted-foreground font-normal")}>{draft.name || "Gruppe …"}</span>
          {kbTarget === "name" && <span className="kiosk-caret" />}
        </button>
        <div className="flex flex-wrap gap-2 shrink-0 min-h-[56px]" data-testid="list-group-members">
          {draft.members.length === 0 && <span className="text-xl text-muted-foreground self-center">2 bis 6 Spieler auswählen</span>}
          {draft.members.map((m) => (
            <button key={m.id} type="button" onClick={() => setDraft((d) => ({ ...d, members: d.members.filter((x) => x.id !== m.id) }))}
              data-testid={`chip-member-${m.id}`}
              className="h-14 pl-4 pr-3 rounded-full bg-primary/15 text-primary border border-primary/40 text-xl font-bold flex items-center gap-2">
              {m.name} <X size={20} />
            </button>
          ))}
          <span className="self-center ml-auto text-xl font-mono text-muted-foreground">{draft.members.length}/{MAX_PLAYERS}</span>
        </div>
        {searchBox("Spieler suchen…")}
        <PlayerList players={players} loading={picker.isLoading} selectedIds={draft.members.map((m) => m.id)} onTap={toggle} compact />
        {editGone && <InlineNote>Diese Gruppe wurde inzwischen gelöscht. Speichern legt sie nicht neu an.</InlineNote>}
        {message && <InlineNote tone="error">{message}</InlineNote>}
        <KioskKeyboard className="shrink-0"
          value={kbTarget === "name" ? draft.name : search}
          onChange={(v) => kbTarget === "name" ? setDraft((d) => ({ ...d, name: v.slice(0, 80), nameTouched: true })) : setSearch(v)} />
        <button type="button" disabled={!canSave || busy || editGone} onClick={save} data-testid="button-group-save"
          className="kiosk-primary shrink-0 h-16 rounded-xl text-2xl font-black flex items-center justify-center gap-3 disabled:opacity-40">
          <Check size={28} /> {createGroup.isPending || updateGroup.isPending ? "Wird gespeichert…" : "Gruppe speichern"}
        </button>
      </div>
    );
  } else {
    body = (
      <div className="flex-1 min-h-0 flex flex-col gap-4" data-testid="view-kiosk-start">
        <div className="flex items-baseline justify-between shrink-0">
          <h2 className="text-3xl font-black tracking-tight">Warteschlange</h2>
          <span className="text-xl text-muted-foreground font-mono" data-testid="text-queue-count">
            {games.length} {games.length === 1 ? "Spiel" : "Spiele"}
          </span>
        </div>
        <div className="flex-1 min-h-0 overflow-y-auto kiosk-scroll space-y-3 pr-1" data-testid="list-queue">
          {queueQ.isLoading && <SkeletonRows />}
          {queue && games.length === 0 && (
            <div className="rounded-2xl border-2 border-dashed border-border p-10 text-center space-y-2">
              <p className="text-3xl font-black">Niemand wartet.</p>
              <p className="text-xl text-muted-foreground">Schreib dich ein – der Stand übernimmt die Reihenfolge.</p>
            </div>
          )}
          {games.map((g) => (
            <QueueGameCard key={g.queueGameId} game={g} activeEntryId={cancelEntry?.entryId ?? null}
              onTapPlayer={(p) => { setCancelEntry(p); setMessage(null); }} />
          ))}
        </div>
        {cancelEntry && (
          <div className="shrink-0 rounded-xl border border-primary/40 bg-primary/5 p-4 space-y-3" data-testid="panel-kiosk-cancel">
            {cancelGone ? (
              <InlineNote>Der Eintrag von {cancelEntry.name} ist nicht mehr offen – das Spiel wird gerade aufgesetzt oder er wurde schon ausgetragen.</InlineNote>
            ) : (
              <p className="text-2xl font-bold">{cancelEntry.name} aus der Warteschlange austragen?</p>
            )}
            {message && <InlineNote tone="error">{message}</InlineNote>}
            <div className="flex gap-3">
              {!cancelGone && (
                <button type="button" disabled={busy} onClick={() => cancel.mutate({ data: { entryId: cancelEntry.entryId } })} data-testid="button-kiosk-cancel-entry"
                  className="h-16 px-6 rounded-xl bg-destructive text-destructive-foreground text-2xl font-black flex items-center gap-3 disabled:opacity-40">
                  <LogOut size={26} /> {cancel.isPending ? "Wird ausgetragen…" : "Austragen"}
                </button>
              )}
              <button type="button" onClick={() => { setCancelEntry(null); setMessage(null); }} data-testid="button-kiosk-cancel-close"
                className="kiosk-key h-16 px-6 rounded-xl text-xl font-bold">Schließen</button>
            </div>
          </div>
        )}
        {!settings?.queueEnabled && queue && (
          <p className="text-lg text-muted-foreground shrink-0">Die Warteschlange ist derzeit ausgeschaltet.</p>
        )}
        <div className="grid grid-cols-2 gap-3 shrink-0">
          <button type="button" onClick={() => { setCancelEntry(null); setView("single"); }} data-testid="button-kiosk-start-single"
            className="kiosk-primary h-20 rounded-2xl text-2xl font-black flex items-center justify-center gap-3">
            <UserPlus size={30} /> {settings?.queueEnabled ? "Ich schreibe mich ein" : "Meine Ergebnisse"}
          </button>
          {settings?.groupsEnabled ? (
            <button type="button" onClick={() => { setCancelEntry(null); setView("groups"); }} data-testid="button-kiosk-start-groups"
              className="kiosk-key h-20 rounded-2xl text-2xl font-black flex items-center justify-center gap-3">
              <Users size={30} /> Gruppe
            </button>
          ) : <span />}
        </div>
      </div>
    );
  }

  return (
    <section className="h-full min-h-0 flex flex-col rounded-2xl border border-border/60 bg-card/50 p-5" data-testid="panel-queue">
      {body}
    </section>
  );
}

function QueueGameCard({ game, onTapPlayer, activeEntryId }: {
  game: QueueGame; onTapPlayer: (p: QueuePlayer) => void; activeEntryId: number | null;
}) {
  const locked = game.status === "LOCKED";
  const taken = MAX_PLAYERS - game.free;
  return (
    <article className={cn("rounded-2xl border-2 p-4 space-y-3", locked ? "border-border/40 opacity-50" : "border-border bg-background/40")}
      data-testid={`card-queue-game-${game.queueGameId}`}>
      <header className="flex items-center justify-between gap-3">
        <div className="flex items-center gap-3">
          <span className="h-12 w-12 rounded-xl bg-secondary flex items-center justify-center text-2xl font-black font-mono">{game.position}</span>
          <span className="text-2xl font-bold">{modeLabel(game.modus)}</span>
        </div>
        {locked ? (
          <span className="flex items-center gap-2 text-xl font-bold text-muted-foreground" data-testid={`status-queue-locked-${game.queueGameId}`}><Lock size={20} /> wird aufgesetzt</span>
        ) : (
          <span className="text-xl font-mono font-bold"><span className="text-primary">{taken}</span>/{MAX_PLAYERS} Plätze</span>
        )}
      </header>
      <div className="flex flex-wrap gap-2">
        {game.players.map((p) => locked ? (
          <span key={p.entryId} className="h-14 px-4 rounded-xl bg-secondary/40 text-xl flex items-center">{p.name}</span>
        ) : (
          <button key={p.entryId} type="button" onClick={() => onTapPlayer(p)} data-testid={`button-queue-entry-${p.entryId}`}
            className={cn("h-14 px-4 rounded-xl text-xl font-semibold border-2",
              activeEntryId === p.entryId ? "border-primary bg-primary/15" : "border-transparent bg-secondary/60")}>
            {p.name}
          </button>
        ))}
        {!locked && Array.from({ length: game.free }, (_, i) => (
          <span key={`free-${i}`} className="h-14 w-14 rounded-xl border-2 border-dashed border-border/60" aria-hidden />
        ))}
      </div>
    </article>
  );
}

export function PlayerList({ players, loading, selectedIds, onTap, compact }: {
  players: PickerPlayer[]; loading: boolean; selectedIds: number[]; onTap: (p: PickerPlayer) => void; compact?: boolean;
}) {
  return (
    <div className={cn("min-h-0 overflow-y-auto kiosk-scroll pr-1", compact ? "flex-1 max-h-[22rem]" : "flex-1")} data-testid="list-kiosk-players">
      {loading && <SkeletonRows />}
      {!loading && players.length === 0 && <p className="text-xl text-muted-foreground py-6 text-center">Kein Spieler gefunden.</p>}
      <div className="grid grid-cols-2 gap-2">
        {players.map((p) => {
          const sel = selectedIds.includes(p.id);
          return (
            <button key={p.id} type="button" onClick={() => onTap(p)} data-testid={`button-player-${p.id}`} aria-pressed={sel}
              className={cn("min-h-[56px] px-4 py-2 rounded-xl text-left text-xl font-semibold border-2 flex items-center justify-between gap-2",
                sel ? "border-primary bg-primary/15 text-foreground" : "border-border/60 bg-background/40")}>
              <span className="truncate">{p.displayName}</span>
              {p.running ? <span className="text-sm font-bold uppercase tracking-wider text-destructive shrink-0">schießt</span>
                : p.activeSignup ? <span className="text-sm font-bold uppercase tracking-wider text-muted-foreground shrink-0">eingeschr.</span>
                : sel ? <Check size={22} className="text-primary shrink-0" /> : null}
            </button>
          );
        })}
      </div>
    </div>
  );
}

function PrintDayPicker({ player, day, onDay, onBack, onPreview, header }: {
  player: Selected; day: string | null; onDay: (d: string) => void; onBack: () => void;
  onPreview: (d: string) => void; header: (t: string, back?: () => void) => React.ReactNode;
}) {
  const q = useGetKioskPrintDays(player.id, {
    query: { queryKey: getGetKioskPrintDaysQueryKey(player.id), placeholderData: keepPreviousData },
  });
  useEffect(() => { if (q.error) reportKioskError(q.error); }, [q.error]);
  const days = useMemo(() => parseDays(q.data), [q.data]);
  const today = todayIso();
  const chosen = day ?? (days.includes(today) ? today : days[0] ?? null);
  return (
    <div className="flex-1 min-h-0 flex flex-col gap-4" data-testid="view-kiosk-print">
      {header("Ergebnisse drucken", onBack)}
      <p className="text-2xl font-bold">{player.name} – Tag wählen</p>
      <div className="flex-1 min-h-0 overflow-y-auto kiosk-scroll grid grid-cols-2 gap-2 content-start pr-1">
        {q.isLoading && <SkeletonRows />}
        {q.isError && <InlineNote tone="error">{apiErrorText(q.error)}</InlineNote>}
        {q.isSuccess && days.length === 0 && <p className="col-span-2 text-xl text-muted-foreground py-6 text-center">Für dich gibt es noch keine Ergebnisse.</p>}
        {days.map((d) => (
          <button key={d} type="button" onClick={() => onDay(d)} data-testid={`button-print-day-${d}`}
            className={cn("h-16 rounded-xl text-2xl font-bold font-mono border-2",
              chosen === d ? "border-primary bg-primary/15" : "border-border bg-background/40")}>
            {formatDay(d)}{d === today && <span className="ml-2 text-base font-sans text-primary">heute</span>}
          </button>
        ))}
      </div>
      <button type="button" disabled={!chosen} onClick={() => chosen && onPreview(chosen)} data-testid="button-print-preview"
        className="kiosk-primary shrink-0 h-20 rounded-2xl text-2xl font-black flex items-center justify-center gap-3 disabled:opacity-40">
        <Printer size={30} /> Vorschau anzeigen
      </button>
    </div>
  );
}

function InlineNote({ children, tone }: { children: React.ReactNode; tone?: "error" }) {
  return (
    <p className={cn("flex items-start gap-2 text-xl rounded-lg px-3 py-2",
      tone === "error" ? "bg-destructive/10 text-destructive" : "bg-amber-500/10 text-amber-400")} data-testid="text-kiosk-inline">
      <AlertTriangle size={22} className="shrink-0 mt-0.5" /> <span>{children}</span>
    </p>
  );
}

function SkeletonRows() {
  return (
    <div className="space-y-2 col-span-2">
      {[0, 1, 2].map((i) => <div key={i} className="h-14 rounded-xl bg-secondary/30 animate-pulse" />)}
    </div>
  );
}
