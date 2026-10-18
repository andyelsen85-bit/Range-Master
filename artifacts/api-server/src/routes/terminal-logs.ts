import { Router } from "express";
import { db, terminalLogTable } from "@workspace/db";
import { sql } from "drizzle-orm";
import { z } from "zod";
import { authenticate, requireAdmin, requireApiKey } from "./auth";
import { csvCell, logEntry, terminalIdentity } from "../lib/terminal-logs";
import { isAttributableTerminal } from "../lib/terminal-identity";

const router = Router();
const limits = new Map<number, { start: number; count: number }>();
function rateLimit(req: any, res: any, next: any) {
  const now = Date.now(), key = req.apiKeyId;
  for (const [id,v] of limits) if (now-v.start >= 60_000) limits.delete(id);
  const state = limits.get(key) ?? { start: now, count: 0 };
  limits.set(key,state);
  if (++state.count > 30) return res.status(429).set("Retry-After","60").json({ error: "Zu viele Diagnose-Uploads" });
  if (req.apiKeyType !== "TERMINAL") return res.status(403).json({ error: "Terminal-Schlüssel erforderlich" });
  return next();
}
router.post("/sync/terminal-logs", requireApiKey, rateLimit, async (req,res) => {
  const parsed = z.object({ terminalId: terminalIdentity.refine(isAttributableTerminal), entries: z.array(logEntry).max(100) }).strict().safeParse(req.body);
  if (!parsed.success) return res.status(400).json({ error: "Ungültige Diagnoseeinträge", details: parsed.error.issues });
  const { terminalId, entries } = parsed.data;
  await db.transaction(async tx => {
    if (entries.length) await tx.insert(terminalLogTable).values(entries.map(e => ({
      terminalId, entryId: e.id, ts: new Date(e.ts), type: e.type, level: e.level, msg: e.msg,
      data: e.data, gatewayId: e.type === "gateway_trace" ? String(e.data.gatewayId) : null,
      gatewaySeq: e.type === "gateway_trace" ? Number(e.data.seq) : null,
    }))).onConflictDoNothing();
  });
  // Duplicate gateway rows uploaded by a second terminal are also acknowledged.
  return res.json({ acceptedIds: entries.map(e => e.id) });
});

const filters = z.object({
  terminal: terminalIdentity.optional(), type: z.string().max(40).optional(),
  level: z.enum(["INFO","WARN","ERROR"]).optional(),
  from: z.string().datetime({ offset: true }).optional(), to: z.string().datetime({ offset: true }).optional(),
  q: z.string().max(200).optional(), offset: z.coerce.number().int().min(0).max(500_000).default(0),
  limit: z.coerce.number().int().min(1).max(200).default(100),
});
function where(f: z.infer<typeof filters>) {
  const parts = [sql`t.ts>=now()-INTERVAL '7 days'`];
  if (f.terminal) parts.push(sql`t.terminal_id=${f.terminal}`);
  if (f.type) parts.push(sql`t.type=${f.type}`);
  if (f.level) parts.push(sql`t.level=${f.level}`);
  if (f.from) parts.push(sql`t.ts>=${f.from}::timestamptz`);
  if (f.to) parts.push(sql`t.ts<=${f.to}::timestamptz`);
  if (f.q) parts.push(sql`position(lower(${f.q}) in lower(t.msg || ' ' || t.data::text))>0`);
  return sql.join(parts,sql` AND `);
}
router.get("/admin/terminal-logs", authenticate, requireAdmin, async (req,res) => {
  const parsed = filters.safeParse(req.query);
  if (!parsed.success) return res.status(400).json({ error: "Ungültige Filter" });
  const f = parsed.data, condition = where(f);
  const [entries,total,terminals,summaries] = await Promise.all([
    db.execute(sql`SELECT entry_id id,terminal_id "terminalId",ts,type,level,msg,data,received_at "receivedAt"
      FROM terminal_log t WHERE ${condition}
      ORDER BY (type='data_loss_risk') DESC,ts DESC,t.id DESC LIMIT ${f.limit} OFFSET ${f.offset}`),
    db.execute(sql`SELECT count(*)::integer n FROM terminal_log t WHERE ${condition}`),
    db.execute(sql`SELECT DISTINCT terminal_id FROM terminal_log WHERE ts>=now()-INTERVAL '7 days' ORDER BY terminal_id`),
    db.execute(sql`WITH durations AS (
      SELECT (ts AT TIME ZONE 'Europe/Luxembourg')::date::text datum,terminal_id,type,
        CASE WHEN jsonb_typeof(data->'durationMs')='number' THEN (data->>'durationMs')::double precision END duration
      FROM terminal_log t WHERE ${condition} AND type IN ('fire_trace','gateway_trace'))
      SELECT datum,terminal_id "terminalId",type,count(*)::integer count,
        percentile_cont(0.5) WITHIN GROUP (ORDER BY duration) median,
        percentile_cont(0.95) WITHIN GROUP (ORDER BY duration) p95,max(duration) max
      FROM durations WHERE duration>=0 AND duration<60000 GROUP BY datum,terminal_id,type ORDER BY datum,terminal_id,type`),
  ]);
   return res.json({ entries: entries.rows, total: (total.rows[0] as any).n,
    terminals: terminals.rows.map((r:any) => r.terminal_id), summaries: summaries.rows });
});
router.get("/admin/terminal-logs/export", authenticate, requireAdmin, async (req,res) => {
  const parsed = filters.safeParse(req.query);
  if (!parsed.success) return res.status(400).json({ error: "Ungültige Filter" });
  const header = ["terminal_id","entry_id","ts","type","level","msg","data"];
  res.set("Cache-Control","no-store").set("Content-Disposition",'attachment; filename="terminal-logs.csv"').type("text/csv");
  async function write(chunk:string) {
    if(res.destroyed) throw new Error("Export connection closed");
    if(res.write(chunk)) return;
    await new Promise<void>((resolve,reject)=>{
      const cleanup=()=>{res.off("drain",drained);res.off("close",closed);};
      const drained=()=>{cleanup();resolve();};
      const closed=()=>{cleanup();reject(new Error("Export connection closed"));};
      res.once("drain",drained);res.once("close",closed);
    });
  }
  try {
    // A stable read-only snapshot avoids duplicates/missing rows while devices
    // upload. Stream all matching terminals rather than silently cutting at 50k.
    await db.transaction(async tx=>{
      await write("\ufeff"+header.map(csvCell).join(",")+"\r\n");
      for(let offset=0;;offset+=1000) {
        const rows=await tx.execute(sql`SELECT terminal_id,entry_id,ts,type,level,msg,data
          FROM terminal_log t WHERE ${where(parsed.data)}
          ORDER BY (type='data_loss_risk') DESC,ts DESC,t.id DESC LIMIT 1000 OFFSET ${offset}`);
        if(!rows.rows.length) break;
        await write(rows.rows.map((r:any)=>header.map(k=>csvCell(r[k])).join(",")).join("\r\n")+"\r\n");
        if(rows.rows.length<1000) break;
      }
    },{isolationLevel:"repeatable read",accessMode:"read only"});
    return res.end();
  } catch(error) {
    res.destroy(error instanceof Error?error:new Error("Export failed"));
    return res;
  }
});

router.post("/sync/game-manifest", requireApiKey, rateLimit, async (req,res) => {
  const parsed = z.object({ terminalId: terminalIdentity.refine(isAttributableTerminal), datum: z.string().regex(/^\d{4}-\d{2}-\d{2}$/),
    externalIds: z.array(terminalIdentity).max(2000) }).safeParse(req.body);
  if (!parsed.success) return res.status(400).json({ error: "Ungültiger Spiele-Abgleich" });
  const {terminalId,datum,externalIds} = parsed.data;
  // Union with previous IDs: a history ring eviction must not hide a missing game.
  await db.execute(sql`INSERT INTO terminal_game_manifest(terminal_id,datum,external_ids)
    VALUES(${terminalId},${datum}::date,${JSON.stringify([...new Set(externalIds)])}::jsonb)
    ON CONFLICT(terminal_id,datum) DO UPDATE SET external_ids=(
      SELECT coalesce(jsonb_agg(DISTINCT value),'[]'::jsonb) FROM jsonb_array_elements(
        terminal_game_manifest.external_ids || EXCLUDED.external_ids)),updated_at=now()`);
  return res.json({ accepted: true });
});
router.get("/admin/game-reconciliation", authenticate, requireAdmin, async (req,res) => {
  const parsed = z.object({ terminal: terminalIdentity.optional(), datum: z.string().regex(/^\d{4}-\d{2}-\d{2}$/).optional() }).safeParse(req.query);
  if (!parsed.success) return res.status(400).json({ error: "Ungültige Filter" });
  const f=parsed.data, conditions=[sql`m.datum >= (now() AT TIME ZONE 'Europe/Luxembourg')::date-7`];
  if(f.terminal) conditions.push(sql`m.terminal_id=${f.terminal}`);
  if(f.datum) conditions.push(sql`m.datum=${f.datum}::date`);
  const rows=await db.execute(sql`SELECT m.terminal_id "terminalId",m.datum::text datum,
    jsonb_array_length(m.external_ids)::integer total,
    (SELECT count(*)::integer FROM jsonb_array_elements_text(m.external_ids) x
      WHERE EXISTS(SELECT 1 FROM spiele s WHERE s.external_id=x.value)) present,
    (SELECT coalesce(jsonb_agg(x.value),'[]'::jsonb) FROM jsonb_array_elements_text(m.external_ids) x
      WHERE NOT EXISTS(SELECT 1 FROM spiele s WHERE s.external_id=x.value)) "missingIds",
    m.updated_at "updatedAt" FROM terminal_game_manifest m WHERE ${sql.join(conditions,sql` AND `)}
    ORDER BY m.datum DESC,m.terminal_id`);
  return res.json({items:rows.rows});
});
export default router;
