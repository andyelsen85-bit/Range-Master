import { useEffect, useState } from "react";

/** localStorage key read by custom-fetch to attach the X-Kiosk-Token header. */
export const KIOSK_STORAGE_KEY = "rangemaster-kiosk";
/** Event dispatched by custom-fetch on a public 401/403. */
export const KIOSK_UNAUTHORIZED_EVENT = "kiosk-unauthorized";

export function getKioskToken(): string | null {
  try {
    return localStorage.getItem(KIOSK_STORAGE_KEY);
  } catch {
    return null;
  }
}

export function clearKioskToken() {
  try {
    localStorage.removeItem(KIOSK_STORAGE_KEY);
  } catch {
    /* ignore */
  }
}

/**
 * Reads `?kiosk=<token>` once, persists it and removes it from the address bar
 * immediately so it never stays visible or ends up in history/bookmarks.
 */
export function captureKioskTokenFromUrl() {
  if (typeof window === "undefined") return;
  const url = new URL(window.location.href);
  const token = url.searchParams.get("kiosk");
  if (token === null) return;
  url.searchParams.delete("kiosk");
  window.history.replaceState(window.history.state, "", url.pathname + url.search + url.hash);
  if (token.trim()) {
    try {
      localStorage.setItem(KIOSK_STORAGE_KEY, token.trim());
    } catch {
      /* ignore */
    }
  }
}

/** Returns the current kiosk token; null after a 401/403 or when missing. */
export function useKioskToken(): string | null {
  const [token, setToken] = useState<string | null>(() => {
    captureKioskTokenFromUrl();
    return getKioskToken();
  });
  useEffect(() => {
    const onUnauthorized = () => {
      clearKioskToken();
      setToken(null);
    };
    const onStorage = (e: StorageEvent) => {
      if (e.key === KIOSK_STORAGE_KEY) setToken(e.newValue);
    };
    window.addEventListener(KIOSK_UNAUTHORIZED_EVENT, onUnauthorized);
    window.addEventListener("storage", onStorage);
    return () => {
      window.removeEventListener(KIOSK_UNAUTHORIZED_EVENT, onUnauthorized);
      window.removeEventListener("storage", onStorage);
    };
  }, []);
  return token;
}

/** Fallback in case a 401/403 reaches a component: same effect as the fetch event. */
export function reportKioskError(err: unknown) {
  const status = (err as { status?: number } | null)?.status;
  if (status === 401 || status === 403) {
    clearKioskToken();
    window.dispatchEvent(new Event(KIOSK_UNAUTHORIZED_EVENT));
  }
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
  const any = e as { data?: { error?: string }; message?: string } | null;
  return any?.data?.error ?? any?.message ?? "Unbekannter Fehler";
}
