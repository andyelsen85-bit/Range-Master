import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { buildDailyReport, dailyReportMessage } from "./daily-report";
import { getSmtpSettings, sendMail } from "./mailer";
import { localDay, shiftDay } from "./local-day";
import { pruneBackups } from "./backup-retention";
import { logger } from "./logger";

type Delivery = { send: typeof sendMail; portalUrl: () => Promise<string | null> };
const smtpDelivery: Delivery = { send: sendMail, portalUrl: async () => {
  const settings = await getSmtpSettings(); return settings ? settings.portalUrl ?? "" : null;
} };
export function catchupDays(today: string) { return [3,2,1].map(n => shiftDay(today,-n)); }
export async function sendDailyReport(datum: string, manual = false, delivery: Delivery = smtpDelivery) {
  const report = await buildDailyReport(datum);
  if (!report.exists) return { sent: false, skipped: "empty" };
  const recipients = (await db.execute(sql`SELECT email FROM daily_report_recipient WHERE active ORDER BY email`)).rows.map((r: any) => r.email as string);
  const snapshot = JSON.stringify(recipients);
  const claimed = await db.execute(manual ? sql`INSERT INTO daily_report_log(datum,status,attempts,recipients_snapshot,data_hash)
    VALUES(${datum},'sending',1,${snapshot}::jsonb,${report.dataHash}) ON CONFLICT(datum) DO UPDATE
    SET status='sending',attempts=daily_report_log.attempts+1,last_attempt_at=now(),first_attempt_at=now(),
      recipients_snapshot=EXCLUDED.recipients_snapshot,data_hash=EXCLUDED.data_hash,error=NULL
    WHERE daily_report_log.status<>'sending' RETURNING datum` : sql`
    INSERT INTO daily_report_log(datum,status,attempts,recipients_snapshot,data_hash)
    VALUES(${datum},'sending',1,${snapshot}::jsonb,${report.dataHash}) ON CONFLICT DO NOTHING RETURNING datum`);
  if (!claimed.rows.length) {
    if (manual) return { sent: false, skipped: "busy" };
    const retry = await db.execute(sql`UPDATE daily_report_log SET status='sending',attempts=attempts+1,
      last_attempt_at=now(),recipients_snapshot=${snapshot}::jsonb,data_hash=${report.dataHash}
      WHERE datum=${datum} AND status='failed' AND last_attempt_at<=now()-INTERVAL '15 minutes'
      AND first_attempt_at>=now()-INTERVAL '6 hours' RETURNING datum`);
    if (!retry.rows.length) return { sent: false, skipped: "claimed" };
  }
  try {
    if (!recipients.length) throw new Error("Keine aktiven Empfänger");
    const url = await delivery.portalUrl();
    if (url === null) throw new Error("SMTP nicht konfiguriert");
    const message = dailyReportMessage(report, url);
    await delivery.send(recipients, message.subject, message.text, message.html);
    await db.execute(sql`UPDATE daily_report_log SET status='sent',sent_at=now(),error=NULL WHERE datum=${datum}`);
    return { sent: true };
  } catch (err) {
    const message = err instanceof Error ? err.message : "Mailversand fehlgeschlagen";
    logger.warn({ datum, error: message }, "Daily report send failed");
    await db.execute(sql`UPDATE daily_report_log SET status='failed',error=${message} WHERE datum=${datum}`);
    return { sent: false, error: message };
  }
}
export async function testDailyReport() {
  const [row] = (await db.execute(sql`SELECT max(datum)::text datum FROM (
    SELECT datum FROM sale_events UNION SELECT datum FROM bill_payments
    UNION SELECT (datum AT TIME ZONE 'Europe/Luxembourg')::date FROM spiele WHERE abgeschlossen) days`)).rows as { datum: string | null }[];
  if (!row?.datum) throw new Error("Noch keine Tagesabrechnung vorhanden");
  const recipients = (await db.execute(sql`SELECT email FROM daily_report_recipient WHERE active ORDER BY email`)).rows.map((r: any) => r.email);
  if (!recipients.length) throw new Error("Keine aktiven Empfänger");
  const smtp = await getSmtpSettings(); if (!smtp) throw new Error("SMTP nicht konfiguriert");
  const message = dailyReportMessage(await buildDailyReport(row.datum), smtp.portalUrl ?? "");
  await sendMail(recipients, `TEST ${message.subject}`, message.text, message.html);
  return { sent: true, datum: row.datum };
}
let running = false, retainedDay = "";
export async function dailyTick() {
  if (running) return;
  running = true;
  try {
    const today = localDay();
    if (retainedDay !== today) { const removed = await pruneBackups(today); logger.info({ removed }, "Daily backup retention"); retainedDay = today; }
    const [settings] = (await db.execute(sql`SELECT enabled FROM daily_report_settings WHERE id=1`)).rows as { enabled: boolean }[];
    if (!settings?.enabled) return;
    // Includes retry candidates each tick; their atomic claim enforces 15 minutes.
    // An interrupted 'sending' claim is shown as uncertain, never blindly resent.
    await db.execute(sql`UPDATE daily_report_log SET status='failed',error='Versand unterbrochen; Zustellung unklar, bitte manuell prüfen',
      first_attempt_at=now()-INTERVAL '7 hours' WHERE status='sending' AND last_attempt_at<now()-INTERVAL '2 minutes'`);
    const retries = (await db.execute(sql`SELECT datum::text FROM daily_report_log WHERE status='failed'
      AND first_attempt_at>=now()-INTERVAL '6 hours' AND last_attempt_at<=now()-INTERVAL '15 minutes'`)).rows as {datum:string}[];
    for (const datum of new Set([...catchupDays(today),...retries.map(r=>r.datum)])) await sendDailyReport(datum);
  } catch (err) { logger.error({ err }, "Daily scheduler tick failed"); }
  finally { running = false; }
}
export function startDailyScheduler() {
  void dailyTick(); const timer = setInterval(() => void dailyTick(), 30_000); timer.unref();
  return () => clearInterval(timer);
}
