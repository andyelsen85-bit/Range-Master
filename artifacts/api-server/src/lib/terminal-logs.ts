import { z } from "zod";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";

export const terminalIdentity = z.string().min(1).max(120).regex(/^[A-Za-z0-9._:-]+$/);
export const logEntry = z.object({
  id: terminalIdentity, ts: z.string().datetime(),
  type: z.enum(["fire_trace","gateway_trace","sync","data_loss_risk","ui_commit","wifi"]),
  level: z.enum(["INFO","WARN","ERROR"]), msg: z.string().max(200),
  data: z.record(z.unknown()),
}).strict().superRefine((entry, ctx) => {
  const issue = (message: string) => ctx.addIssue({ code: "custom", message });
  if (Buffer.byteLength(JSON.stringify(entry)) > 1024 || Buffer.byteLength(JSON.stringify(entry.data)) > 512) issue("Logeintrag zu groß");
  if (entry.type === "data_loss_risk" && entry.level !== "ERROR") issue("Datenverlustrisiken müssen ERROR sein");
  if (new Date(entry.ts).getTime() > Date.now() + 300_000) issue("Zeitstempel liegt in der Zukunft");
  if (unsafeLogValue(entry.msg) || unsafeLogValue(entry.data)) issue("Diagnose enthält vertrauliche Daten");
  if (entry.type === "gateway_trace" && (!terminalIdentity.safeParse(entry.data.gatewayId).success ||
      !Number.isSafeInteger(entry.data.seq) || Number(entry.data.seq) < 1)) issue("Gateway-ID und Sequenznummer fehlen");
});

export function unsafeLogValue(value: unknown, depth = 0): boolean {
  if (depth > 8) return true;
  if (typeof value === "string") return /(?:https?:\/\/[^\s]*\?|bearer\s+\S+|(?:password|passwort|token|secret|api[_ -]?key)\s*[:=])/i.test(value);
  if (Array.isArray(value)) return value.some(v => unsafeLogValue(v, depth + 1));
  if (value && typeof value === "object") return Object.entries(value).some(([key,v]) =>
    /password|passwort|secret|token|authorization|api.?key|hmac|nonce|ciphertext/i.test(key) || unsafeLogValue(v, depth + 1));
  return false;
}

export function csvCell(value: unknown) {
  let text = typeof value === "object" ? JSON.stringify(value) : String(value ?? "");
  if (/^[\s]*[=+\-@]/.test(text)) text = "'" + text;
  return `"${text.replaceAll('"','""')}"`;
}

export async function pruneTerminalLogs() {
  await db.transaction(async tx => {
    await tx.execute(sql`DELETE FROM terminal_log WHERE ts < now()-INTERVAL '7 days'`);
    await tx.execute(sql`WITH ranked AS (
      SELECT id,row_number() OVER(PARTITION BY terminal_id ORDER BY ts DESC,id DESC) AS n FROM terminal_log)
      DELETE FROM terminal_log t USING ranked r WHERE t.id=r.id AND r.n>50000`);
    await tx.execute(sql`DELETE FROM terminal_game_manifest WHERE datum <
      (now() AT TIME ZONE 'Europe/Luxembourg')::date-7`);
  });
}
