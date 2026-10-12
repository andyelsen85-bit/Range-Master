import { Router } from "express";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { authenticate, requireAdmin, requireApiKey } from "./auth";
import { z } from "zod";
import { applyDayEvent, DayConflict, inventoryState } from "../lib/inventory";
import { daySchema, localDay } from "../lib/local-day";
import { buildDailyReport } from "../lib/daily-report";
import { sendDailyReport, testDailyReport } from "../lib/daily-scheduler";
import { gameScorecard } from "../lib/game-scorecard";
import { publicName } from "@workspace/scorecard";
import type { ErrorRequestHandler } from "express";

const router = Router();
router.get("/sync/inventory/state", requireApiKey, async (req, res) => {
  if ((req as any).apiKeyType !== "TERMINAL") return res.sendStatus(403);
  return res.json(await inventoryState(daySchema.parse(req.query.datum ?? localDay())));
});
for (const kind of ["opening","closing","gameday"] as const) {
  router.post(kind === "gameday" ? "/sync/gameday" : `/sync/inventory/${kind}`, requireApiKey, async (req, res) => {
    if ((req as any).apiKeyType !== "TERMINAL") return res.sendStatus(403);
    // Bind terminal identity exactly as configuration sync does; never trust a
    // server-derived inventory difference supplied by a device.
    try { return res.json(await applyDayEvent(kind, req.body)); }
    catch (err) { if (err instanceof DayConflict) return res.status(409).json({ error: err.message }); throw err; }
  });
}
router.get("/admin/daily-report", authenticate, requireAdmin, async (_req, res) => {
  const [settings] = (await db.execute(sql`SELECT enabled FROM daily_report_settings WHERE id=1`)).rows;
  const recipients = (await db.execute(sql`SELECT id,email,active,created_at "createdAt" FROM daily_report_recipient ORDER BY id`)).rows;
  const logs = (await db.execute(sql`SELECT datum::text,status,sent_at "sentAt",attempts,error,recipients_snapshot "recipientsSnapshot",data_hash "dataHash"
    FROM daily_report_log ORDER BY datum DESC LIMIT 30`)).rows as any[];
  await Promise.all(logs.map(async log => {log.changed=log.status==="sent"&&(await buildDailyReport(log.datum)).dataHash!==log.dataHash;}));
  res.json({ enabled: (settings as any)?.enabled ?? false, recipients, history: logs });
});
router.put("/admin/daily-report", authenticate, requireAdmin, async (req, res) => {
  const body = z.object({ enabled: z.boolean(), recipients: z.array(z.object({
    email: z.string().trim().email().transform(s => s.toLowerCase()), active: z.boolean(),
  })).max(20) }).parse(req.body);
  const unique = new Map(body.recipients.map(r => [r.email,r]));
  await db.transaction(async tx => {
    await tx.execute(sql`SELECT pg_advisory_xact_lock(hashtextextended('daily-report-settings',0))`);
    await tx.execute(sql`INSERT INTO daily_report_settings(id,enabled) VALUES(1,${body.enabled}) ON CONFLICT(id) DO UPDATE SET enabled=EXCLUDED.enabled`);
    const emails = [...unique.keys()];
    if (emails.length) await tx.execute(sql`DELETE FROM daily_report_recipient WHERE email NOT IN (${sql.join(emails.map(email=>sql`${email}`),sql`, `)})`);
    else await tx.execute(sql`DELETE FROM daily_report_recipient`);
    for (const r of unique.values()) await tx.execute(sql`INSERT INTO daily_report_recipient(email,active) VALUES(${r.email},${r.active})
      ON CONFLICT(email) DO UPDATE SET active=EXCLUDED.active`);
  });
  res.json({ success: true });
});
router.post("/admin/daily-report/test", authenticate, requireAdmin, async (_req, res) => res.json(await testDailyReport()));
router.post("/admin/daily-report/resend", authenticate, requireAdmin, async (req, res) => {
  const datum = daySchema.parse(req.body.datum);
  if (datum >= localDay()) return res.status(400).json({ error: "Nur abgeschlossene Kalendertage" });
  const result = await sendDailyReport(datum, true);
  return res.status(result.sent ? 200 : 409).json(result);
});
router.get("/admin/public-page", authenticate, requireAdmin, async (_req, res) => {
  const [row] = (await db.execute(sql`SELECT enabled,name_mode "nameMode" FROM public_page_settings WHERE id=1`)).rows;
  res.json(row ?? { enabled: false, nameMode: "INITIAL" });
});
router.put("/admin/public-page", authenticate, requireAdmin, async (req, res) => {
  const body = z.object({ enabled: z.boolean(), nameMode: z.enum(["FULL","INITIAL"]) }).parse(req.body);
  await db.execute(sql`INSERT INTO public_page_settings(id,enabled,name_mode) VALUES(1,${body.enabled},${body.nameMode})
    ON CONFLICT(id) DO UPDATE SET enabled=EXCLUDED.enabled,name_mode=EXCLUDED.name_mode`);
  res.json(body);
});
const ips = new Map<string,{ at: number; count: number }>();
router.get("/public/last-game", async (req, res) => {
  res.setHeader("Cache-Control","no-store");
  res.removeHeader("Access-Control-Allow-Origin");
  const now = Date.now(), ip = req.ip ?? req.socket.remoteAddress ?? "unknown";
  // Bound map memory and expire windows without trusting spoofable X-Forwarded-For.
  for (const [key,value] of ips) if (now-value.at >= 60_000) ips.delete(key);
  if (!ips.has(ip) && ips.size >= 4096) return res.sendStatus(429);
  const limit = ips.get(ip) ?? { at: now, count: 0 }; limit.count++; ips.set(ip,limit);
  if (limit.count > 30) { res.setHeader("Retry-After","60"); return res.sendStatus(429); }
  const [settings] = (await db.execute(sql`SELECT enabled,name_mode FROM public_page_settings WHERE id=1`)).rows as any[];
  if (!settings?.enabled) return res.sendStatus(404);
  const [last] = (await db.execute(sql`SELECT id FROM spiele WHERE abgeschlossen
    ORDER BY completed_at DESC NULLS LAST,id DESC LIMIT 1`)).rows as { id: number }[];
  if (!last) return res.json(null);
  const card = await gameScorecard(last.id);
  if (card) card.players = card.players.map(p => ({ ...p, name: publicName(p.name, settings.name_mode) }));
  return res.json(card);
});
router.get("/spiele/:id/scorecard", authenticate, async (req, res) => {
  const id = z.coerce.number().int().positive().parse(req.params.id);
  const user = (req as any).user;
  if (!user.isAdmin) {
    const participates = (await db.execute(sql`SELECT 1 FROM spiel_teilnahmen WHERE spiel_id=${id} AND spieler_id=${user.id} LIMIT 1`)).rows;
    if (!participates.length) return res.sendStatus(404);
  }
  const card = await gameScorecard(id); if (!card) return res.sendStatus(404);
  return res.json(card);
});
const validationError: ErrorRequestHandler = (err,_req,res,next) => {
  if(err instanceof z.ZodError) {res.status(400).json({error:"Ungültige Eingabe",details:err.flatten()});return;}
  next(err);
};
router.use(validationError);
export default router;
