import { useEffect, useState, type ReactNode } from "react";
import { useQueryClient } from "@tanstack/react-query";
import { useGetKioskQueue, getGetKioskQueueQueryKey, useUnlockPublicPage } from "@workspace/api-client-react";
import { KeyRound, Delete, Lock, MonitorOff, Target } from "lucide-react";
import { KIOSK_LOCKED_EVENT, errStatus, isKioskLocked, retryAfterSec, refreshKioskAccess } from "@/lib/kiosk";
import { cn } from "@/lib/utils";

/**
 * Access gate for /live and /live-print. The rm_kiosk cookie is HttpOnly, so access is
 * determined by asking the server. Only 401 + code "kiosk_locked" shows the PIN pad.
 */
export function KioskGate({ children }: { children: ReactNode }) {
  const [locked, setLocked] = useState(false);
  const probe = useGetKioskQueue({
    query: {
      queryKey: getGetKioskQueueQueryKey(),
      retry: (n, e) => !isKioskLocked(e) && errStatus(e) !== 404 && n < 2,
    },
  });

  useEffect(() => {
    const onLock = () => setLocked(true);
    window.addEventListener(KIOSK_LOCKED_EVENT, onLock);
    return () => window.removeEventListener(KIOSK_LOCKED_EVENT, onLock);
  }, []);
  useEffect(() => {
    if (probe.error && isKioskLocked(probe.error)) setLocked(true);
    else if (probe.data) setLocked(false);
  }, [probe.error, probe.data]);

  if (probe.isLoading) return <KioskChecking />;
  if (locked) return <KioskPinScreen onUnlocked={() => setLocked(false)} />;
  if (errStatus(probe.error) === 404) return <KioskUnavailable />;
  return <>{children}</>;
}

function KioskChecking() {
  return (
    <div className="kiosk-root min-h-[100dvh] bg-background text-foreground flex flex-col items-center justify-center gap-6 p-8" data-testid="screen-kiosk-checking">
      <div className="h-24 w-24 rounded-2xl bg-primary/15 text-primary flex items-center justify-center animate-pulse"><Target size={52} /></div>
      <div className="h-6 w-64 rounded bg-secondary/40 animate-pulse" />
    </div>
  );
}

export function KioskUnavailable() {
  return (
    <div className="kiosk-root min-h-[100dvh] bg-background text-foreground flex flex-col items-center justify-center gap-6 text-center p-8" data-testid="screen-kiosk-unavailable">
      <div className="h-24 w-24 rounded-2xl bg-secondary/40 text-muted-foreground flex items-center justify-center"><MonitorOff size={52} /></div>
      <h1 className="text-5xl font-black tracking-tight">Seite nicht verfügbar</h1>
      <p className="text-2xl text-muted-foreground max-w-2xl">Die öffentliche Seite ist derzeit ausgeschaltet.</p>
    </div>
  );
}

const KEYS = ["1", "2", "3", "4", "5", "6", "7", "8", "9"];

export function KioskPinScreen({ onUnlocked }: { onUnlocked: () => void }) {
  const qc = useQueryClient();
  const [pin, setPin] = useState("");
  const [msg, setMsg] = useState<string | null>(null);
  const [lockUntil, setLockUntil] = useState<number | null>(null);
  const [now, setNow] = useState(() => Date.now());

  useEffect(() => {
    if (!lockUntil) return;
    const t = setInterval(() => setNow(Date.now()), 250);
    return () => clearInterval(t);
  }, [lockUntil]);
  const remaining = lockUntil ? Math.max(0, Math.ceil((lockUntil - now) / 1000)) : 0;
  useEffect(() => { if (lockUntil && remaining === 0) { setLockUntil(null); setMsg(null); } }, [lockUntil, remaining]);

  const unlock = useUnlockPublicPage({
    mutation: {
      onSuccess: async () => {
        setPin("");
        setMsg(null);
        await refreshKioskAccess(qc);
        if (isKioskLocked(qc.getQueryState(getGetKioskQueueQueryKey())?.error)) {
          setMsg("Freigabe wurde erneut gesperrt. Bitte PIN eingeben.");
          return;
        }
        onUnlocked();
      },
      onError: (e) => {
        setPin("");
        const status = errStatus(e);
        const code = (e as { data?: { code?: string } } | null)?.data?.code;
        if (status === 429 || code === "pin_locked") {
          const sec = retryAfterSec(e) ?? 60;
          setNow(Date.now());
          setLockUntil(Date.now() + sec * 1000);
          setMsg("Zu viele Versuche.");
        } else if (status === 401 || code === "pin_invalid") {
          setMsg("PIN falsch");
        } else if (status === 404) {
          setMsg("Die öffentliche Seite ist ausgeschaltet.");
        } else if (status === undefined) {
          setMsg("Keine Verbindung. Bitte erneut versuchen.");
        } else {
          setMsg("Entsperren nicht möglich. Bitte erneut versuchen.");
        }
      },
    },
  });

  const blocked = remaining > 0 || unlock.isPending;
  const add = (d: string) => { if (!blocked && pin.length < 32) { setPin((p) => p + d); if (msg && !lockUntil) setMsg(null); } };
  const back = () => setPin((p) => p.slice(0, -1));
  const submit = () => { if (!blocked && pin.length >= 6) unlock.mutate({ data: { pin } }); };

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (/^[0-9]$/.test(e.key)) add(e.key);
      else if (e.key === "Backspace") back();
      else if (e.key === "Enter") submit();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  });

  const key = "kiosk-key h-[clamp(3rem,8.5dvh,6rem)] rounded-2xl text-4xl font-black flex items-center justify-center active:scale-95 transition-transform disabled:opacity-40";
  return (
    <div className="kiosk-root min-h-[100dvh] bg-background text-foreground flex flex-col items-center justify-center gap-[clamp(1rem,2dvh,2rem)] p-4 sm:p-6 select-none" data-testid="screen-kiosk-pin">
      <div className="flex flex-col items-center gap-3 text-center">
        <div className="h-16 w-16 rounded-2xl bg-primary/15 text-primary flex items-center justify-center">{remaining > 0 ? <Lock size={44} /> : <KeyRound size={44} />}</div>
        <h1 className="text-4xl font-black tracking-tight">Stand-PC entsperren</h1>
        <p className="text-xl text-muted-foreground">PIN eingeben (6 bis 32 Ziffern)</p>
      </div>
      <div className="h-16 min-w-[20rem] max-w-[90vw] px-6 rounded-2xl border border-border bg-secondary/20 flex items-center justify-center gap-3 overflow-hidden" data-testid="display-pin" aria-label={`${pin.length} Ziffern eingegeben`}>
        {pin.length === 0 ? <span className="text-xl text-muted-foreground">PIN</span> :
          Array.from({ length: pin.length }).map((_, i) => <span key={i} className="h-4 w-4 shrink-0 rounded-full bg-foreground" />)}
      </div>
      <p className={cn("h-8 text-2xl font-bold", remaining > 0 || msg ? "text-destructive" : "text-transparent")} role="alert" data-testid="text-pin-message">
        {remaining > 0 ? `Zu viele Versuche. Erneut in ${remaining} s` : msg ?? "."}
      </p>
      <div className="grid grid-cols-3 gap-3 w-full max-w-md">
        {KEYS.map((d) => <button key={d} type="button" disabled={blocked} onClick={() => add(d)} className={key} data-testid={`button-pin-${d}`}>{d}</button>)}
        <button type="button" disabled={blocked || !pin} onClick={back} className={key} aria-label="Löschen" data-testid="button-pin-back"><Delete size={36} /></button>
        <button type="button" disabled={blocked} onClick={() => add("0")} className={key} data-testid="button-pin-0">0</button>
        <button type="button" disabled={blocked || pin.length < 6} onClick={submit}
          className="kiosk-primary h-[clamp(3rem,8.5dvh,6rem)] rounded-2xl text-3xl font-black active:scale-95 transition-transform disabled:opacity-40" data-testid="button-pin-ok">
          {unlock.isPending ? "…" : "OK"}
        </button>
      </div>
    </div>
  );
}
