import { createHash } from "node:crypto";
import type { Request, RequestHandler, Response } from "express";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";

export const deviceHash = (value: string) => createHash("sha256").update(value).digest("hex");
export const kioskIp = (ip: string) => deviceHash(`kiosk-ip:${ip}`);
export const kioskCookie = "rm_kiosk";
export const cookieLifetimeMs = 365 * 24 * 60 * 60 * 1000;
export function setKioskCookie(res: Response, value: string) {
  res.cookie(kioskCookie, value, { httpOnly: true, secure: true, sameSite: "lax", path: "/", maxAge: cookieLifetimeMs });
}
export function kioskHeaders(req: Request) {
  return {
    origin: req.get("origin")?.slice(0, 512),
    host: req.get("host")?.slice(0, 512),
    forwardedHost: req.get("x-forwarded-host")?.slice(0, 512),
  };
}
export const checkKioskOrigin: RequestHandler = (req, res, next) => {
  if (!["POST", "PUT", "PATCH", "DELETE"].includes(req.method) || !req.get("origin")) { next(); return; }
  const headers = kioskHeaders(req);
  let accepted = false;
  try {
    const origin = new URL(headers.origin!);
    const configuredHost = process.env.PUBLIC_HOST;
    const hosts = [req.get("host"), req.get("x-forwarded-host")?.split(",")[0]?.trim(),
      configuredHost ? new URL(configuredHost.includes("://") ? configuredHost : `https://${configuredHost}`).host : undefined];
    accepted = ["http:", "https:"].includes(origin.protocol) && origin.origin === headers.origin &&
      hosts.some(host => host?.toLowerCase() === origin.host.toLowerCase());
  } catch { /* Malformed/multiple origins fail closed. */ }
  if (!accepted) {
    req.log.warn({ event: "KIOSK_ORIGIN_REJECTED", ...headers }, "Stand-PC: Herkunft abgelehnt");
    res.status(403).json({ code: "origin_mismatch", message: "Die Herkunft der Anfrage passt nicht zum Portal." });
    return;
  }
  next();
};
export const requireKiosk: RequestHandler = async (req, res, next) => {
  res.setHeader("Cache-Control", "no-store");
  res.removeHeader("Access-Control-Allow-Origin");
  const [settings] = (await db.execute(sql`SELECT enabled,name_mode,queue_enabled,groups_enabled,
    show_ranking,max_queue_games,enabled_modes,pin_hash,pin_epoch FROM public_page_settings WHERE id=1`)).rows as any[];
  if (!settings?.enabled) { res.sendStatus(404); return; }
  checkKioskOrigin(req, res, async error => {
    if (error) { next(error); return; }
    try {
      const value = req.get("cookie")?.split(";").map(part => part.trim()).find(part => part.startsWith(`${kioskCookie}=`))?.slice(kioskCookie.length + 1);
      const [device] = /^[A-Za-z0-9_-]{43}$/.test(value ?? "") && settings.pin_hash
        ? (await db.execute(sql`SELECT id,pin_epoch,last_used_at,last_renewed_at FROM kiosk_devices
            WHERE token_hash=${deviceHash(value!)} AND revoked_at IS NULL
              AND last_renewed_at>now()-interval '365 days'`)).rows as any[] : [];
      if (!device || device.pin_epoch !== settings.pin_epoch) {
        res.status(401).json({ code: "kiosk_locked", message: "Bitte Stand-PC mit PIN entsperren." }); return;
      }
      const ipHash = kioskIp(req.ip ?? req.socket.remoteAddress ?? "unknown");
      const renew = Date.now() - new Date(device.last_renewed_at).getTime() >= 86400000;
      await db.execute(sql`UPDATE kiosk_devices SET last_used_at=now(),last_used_ip_hash=${ipHash},
        last_renewed_at=CASE WHEN ${renew} THEN now() ELSE last_renewed_at END
        WHERE id=${device.id} AND (last_used_at<now()-interval '30 seconds' OR ${renew})`);
      if (renew) setKioskCookie(res, value!);
      // Never put the PIN verifier into the public request context.
      const { pin_hash: _hash, pin_epoch: _epoch, ...publicSettings } = settings;
      res.locals.kiosk = { id: device.id, ipHash, settings: publicSettings };
      next();
    } catch (err) { next(err); }
  });
};
