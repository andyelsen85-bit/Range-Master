import { randomBytes } from "node:crypto";
import { Router } from "express";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { authenticate, requireAdmin } from "./auth";
import { checkKioskOrigin, deviceHash, kioskHeaders, kioskIp, setKioskCookie } from "../lib/kiosk";
import { hashKioskPin, kioskPinLock, lockDurationsSec, remainingLock, validKioskPin, verifyKioskPin } from "../lib/kiosk-pin";
import { queueAudit } from "../lib/queue";

const router = Router();
router.use((_req, res, next) => { res.setHeader("Cache-Control", "no-store"); next(); });
router.post("/public/unlock", checkKioskOrigin, async (req, res): Promise<void> => {
  const ipHash = kioskIp(req.ip ?? req.socket.remoteAddress ?? "unknown");
  const result = await db.transaction(async tx => {
    // Shared across replicas/restarts. Serialize PIN changes with verification.
    await kioskPinLock(tx);
    const [settings] = (await tx.execute(sql`SELECT enabled,pin_hash,pin_epoch FROM public_page_settings WHERE id=1`)).rows as any[];
    if (!settings?.enabled) return { status: 404 } as const;
    await tx.execute(sql`DELETE FROM kiosk_pin_limits WHERE updated_at<now()-interval '30 days'`);
    await tx.execute(sql`INSERT INTO kiosk_pin_limits(key) VALUES('global') ON CONFLICT DO NOTHING`);
    const [global] = (await tx.execute(sql`SELECT *,now() AS clock FROM kiosk_pin_limits WHERE key='global'`)).rows as any[];
    const now = new Date(global.clock).getTime();
    const globalWindow = new Date(global.window_started_at).getTime();
    const globalFailures = now - globalWindow < 3600000 ? global.failures : 0;
    if (globalFailures >= 50) {
      return { status: 429, retryAfterSec: Math.max(1, Math.ceil((globalWindow + 3600000 - now) / 1000)) } as const;
    }
    await tx.execute(sql`INSERT INTO kiosk_pin_limits(key) VALUES(${ipHash}) ON CONFLICT DO NOTHING`);
    const [limit] = (await tx.execute(sql`SELECT * FROM kiosk_pin_limits WHERE key=${ipHash}`)).rows as any[];
    const retryAfterSec = remainingLock(limit.locked_until, now);
    if (retryAfterSec) {
      return { status: 429, retryAfterSec } as const;
    }
    if (!await verifyKioskPin(settings.pin_hash, req.body?.pin)) {
      const failures = (limit.locked_until ? 0 : limit.failures) + 1;
      const locked = failures >= 5;
      const seconds = locked ? lockDurationsSec[Math.min(limit.strikes, 2)] : 0;
      await tx.execute(sql`UPDATE kiosk_pin_limits SET failures=${failures},strikes=strikes+${locked ? 1 : 0},
        locked_until=${seconds ? new Date(now + seconds * 1000) : null},updated_at=now() WHERE key=${ipHash}`);
      await tx.execute(sql`UPDATE kiosk_pin_limits SET failures=${globalFailures + 1},
        window_started_at=${globalFailures ? new Date(globalWindow) : new Date(now)},updated_at=now() WHERE key='global'`);
      await queueAudit(tx, "KIOSK_PIN_FAILED", {}, { ipHash });
      if (locked || globalFailures + 1 >= 50) {
        await queueAudit(tx, "KIOSK_PIN_LOCKED", { scope: locked ? "ip" : "global" }, { ipHash });
        return { status: 429, retryAfterSec: locked ? seconds : Math.ceil((globalWindow + 3600000 - now) / 1000), freshLock: true } as const;
      }
      return { status: 401 } as const;
    }
    await tx.execute(sql`UPDATE kiosk_pin_limits SET failures=0,strikes=0,locked_until=NULL,updated_at=now() WHERE key=${ipHash}`);
    const value = randomBytes(32).toString("base64url");
    const ua = req.get("user-agent") ?? "";
    const browser = /Edg\//.test(ua) ? "Edge" : /Firefox\//.test(ua) ? "Firefox" : /Chrome\//.test(ua) ? "Chrome" : /Safari\//.test(ua) ? "Safari" : null;
    const os = /Android/.test(ua) ? "Android" : /iPhone|iPad/.test(ua) ? "iOS" : /Windows/.test(ua) ? "Windows" : /Macintosh/.test(ua) ? "macOS" : /Linux/.test(ua) ? "Linux" : null;
    const [device] = (await tx.execute(sql`INSERT INTO kiosk_devices(token_hash,name,pin_epoch,last_used_ip_hash)
      VALUES(${deviceHash(value)},${[browser, os].filter(Boolean).join(" / ") || null},${settings.pin_epoch},${ipHash}) RETURNING id`)).rows as any[];
    await queueAudit(tx, "KIOSK_UNLOCKED", {}, { id: device.id, ipHash });
    return { status: 200, value, deviceId: device.id } as const;
  });
  if (result.status === 404) { res.sendStatus(404); return; }
  if (result.status === 429) {
    if ("freshLock" in result) req.log.warn({ event: "KIOSK_PIN_LOCKED", ipHash, retryAfterSec: result.retryAfterSec }, "Stand-PC: PIN-Eingabe gesperrt");
    res.setHeader("Retry-After", result.retryAfterSec);
    res.status(429).json({ code: "pin_locked", retryAfterSec: result.retryAfterSec }); return;
  }
  if (result.status === 401) {
    req.log.warn({ event: "KIOSK_PIN_FAILED", ipHash }, "Stand-PC: PIN falsch");
    res.status(401).json({ code: "pin_invalid" }); return;
  }
  setKioskCookie(res, result.value);
  req.log.info({ event: "KIOSK_UNLOCKED", deviceId: result.deviceId, ...kioskHeaders(req) }, "Stand-PC entsperrt");
  res.json({ ok: true });
});
router.use("/admin/public-page", authenticate, requireAdmin, checkKioskOrigin);
router.put("/admin/public-page/pin", async (req, res): Promise<void> => {
  if (!validKioskPin(req.body?.pin)) {
    res.status(400).json({ code: "pin_format", message: "Die PIN muss aus 6 bis 32 Ziffern bestehen." }); return;
  }
  const hash = await hashKioskPin(req.body.pin);
  await db.transaction(async tx => {
    await kioskPinLock(tx);
    await tx.execute(sql`UPDATE public_page_settings SET pin_hash=${hash},pin_changed_at=now(),pin_epoch=pin_epoch+1 WHERE id=1`);
    await queueAudit(tx, "KIOSK_PIN_CHANGED", { adminId: (req as any).user.id });
  });
  res.json({ ok: true });
});
router.get("/admin/public-page/devices", async (_req, res): Promise<void> => {
  const devices = (await db.execute(sql`SELECT d.id,d.name,d.created_at "createdAt",d.last_used_at "lastUsedAt"
    FROM kiosk_devices d JOIN public_page_settings s ON s.id=1 AND s.pin_epoch=d.pin_epoch
    WHERE d.revoked_at IS NULL AND d.last_renewed_at>now()-interval '365 days' ORDER BY d.last_used_at DESC,d.id DESC`)).rows;
  res.json({ devices });
});
router.post("/admin/public-page/devices/reset", async (req, res): Promise<void> => {
  await db.transaction(async tx => {
    await kioskPinLock(tx);
    await tx.execute(sql`UPDATE public_page_settings SET pin_epoch=pin_epoch+1 WHERE id=1`);
    await queueAudit(tx, "KIOSK_DEVICES_RESET", { adminId: (req as any).user.id });
  });
  res.json({ ok: true });
});
router.delete("/admin/public-page/devices/:id", async (req, res): Promise<void> => {
  const id = Number(req.params.id);
  if (!Number.isSafeInteger(id) || id <= 0) { res.status(400).json({ code: "device_invalid", message: "Ungültiger Stand-PC." }); return; }
  await db.transaction(async tx => {
    await tx.execute(sql`UPDATE kiosk_devices SET revoked_at=COALESCE(revoked_at,now()) WHERE id=${id}`);
    await queueAudit(tx, "KIOSK_DEVICE_REVOKED", { deviceId: id, adminId: (req as any).user.id });
  });
  res.json({ ok: true });
});
export default router;
