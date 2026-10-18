import type { QueryClient } from "@tanstack/react-query";

/** Event dispatched when the server answers 401 with code "kiosk_locked". */
export const KIOSK_LOCKED_EVENT = "kiosk-locked";

function errBody(err: unknown): { code?: string; message?: string; error?: string; retryAfterSec?: number } | null {
  const d = (err as { data?: unknown } | null)?.data;
  return d && typeof d === "object" ? (d as Record<string, never>) : null;
}

export function errStatus(err: unknown): number | undefined {
  return (err as { status?: number } | null)?.status;
}

export function errCode(err: unknown): string | undefined {
  return errBody(err)?.code;
}

/** Only an exact 401 + code "kiosk_locked" locks the stand-PC session. */
export function isKioskLocked(err: unknown): boolean {
  return errStatus(err) === 401 && errCode(err) === "kiosk_locked";
}

export function retryAfterSec(err: unknown): number | undefined {
  const n = errBody(err)?.retryAfterSec;
  return typeof n === "number" && n > 0 ? n : undefined;
}

/** Broadcasts a lock only for the exact kiosk_locked code; all other errors stay local. */
export function reportKioskError(err: unknown) {
  if (isKioskLocked(err)) window.dispatchEvent(new Event(KIOSK_LOCKED_EVENT));
}

/** Drop old-cookie failures before remounting widgets after a successful unlock. */
export async function refreshKioskAccess(client: QueryClient) {
  const publicQueries = {
    predicate: (query: { queryKey: readonly unknown[] }) =>
      query.queryKey.some(part => typeof part === "string" && /\/public(?:\/|$)/.test(part)),
  };
  await client.cancelQueries(publicQueries);
  // invalidateQueries preserves inactive errors; those would re-lock the gate
  // from a widget's mount effect before its new-cookie request completes.
  await client.resetQueries(publicQueries);
}

export function todayIso() {
  const d = new Date();
  const p = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
}

export function formatDay(iso: string) {
  const [y, m, d] = iso.slice(0, 10).split("-");
  return `${d}.${m}.${y}`;
}

export function apiErrorText(e: unknown): string {
  const b = errBody(e);
  if (b?.message) return b.message;
  if (b?.error) return b.error;
  if (errStatus(e) === undefined) return "Keine Verbindung zum Server. Neuer Versuch läuft automatisch.";
  return (e as { message?: string } | null)?.message ?? "Unbekannter Fehler";
}
