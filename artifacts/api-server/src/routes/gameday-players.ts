import {Router} from "express";
import {db} from "@workspace/db";
import {sql} from "drizzle-orm";
import {createHash} from "node:crypto";
import {z} from "zod";
import {requireApiKey} from "./auth";
import {daySchema,localDay} from "../lib/local-day";
import {queueLock,compactQueue} from "../lib/queue";
import {cancelIneligibleQueue} from "../lib/queue-eligibility";
const router=Router();
const event=z.object({externalId:z.string().min(1).max(120),
 terminalId:z.string().min(1).max(100).refine(s=>s!=="ESP32-P4-UNKNOWN"),
 datum:daySchema,spielerId:z.number().int().positive(),action:z.enum(["ADD","REMOVE"]),
 occurredAt:z.string().datetime()}).refine(e=>localDay(new Date(e.occurredAt))===e.datum,"Ereignis und Kalendertag passen nicht zusammen.");
router.use("/sync/gameday-players",requireApiKey,(req,res,next)=>{
 res.setHeader("Cache-Control","no-store");
 if((req as any).apiKeyType!=="TERMINAL")return void res.sendStatus(403);
 next();
});
router.get("/sync/gameday-players",async(req,res)=>{
 const datum=daySchema.parse(req.query.datum??localDay());
 res.json({datum,players:(await db.execute(sql`SELECT spieler_id "spielerId",terminal_id "terminalId",added_at "addedAt"
  FROM gameday_players WHERE datum=${datum}::date AND removed_at IS NULL ORDER BY spieler_id`)).rows});
});
router.post("/sync/gameday-players",async(req,res)=>{
 const {events}=z.object({events:z.array(event).max(100)}).parse(req.body);
 const results=await db.transaction(async tx=>{
  await queueLock(tx);const results=[];
  for(const e of events) {
   const hash=createHash("sha256").update(JSON.stringify(e)).digest("hex");
   const inserted=(await tx.execute(sql`INSERT INTO gameday_player_events(external_id,payload_hash,payload)
    VALUES(${e.externalId},${hash},${JSON.stringify(e)}::jsonb) ON CONFLICT DO NOTHING RETURNING external_id`)).rows;
   const [prior]=(await tx.execute(sql`SELECT payload_hash FROM gameday_player_events WHERE external_id=${e.externalId}`)).rows as any[];
   if(prior.payload_hash!==hash){const error:any=new Error("Ereignis-ID mit geändertem Inhalt.");error.status=409;throw error;}
   if(inserted.length)await tx.execute(sql`INSERT INTO gameday_players(datum,spieler_id,added_at,removed_at,terminal_id,occurred_at,external_id)
    VALUES(${e.datum}::date,${e.spielerId},${e.occurredAt}::timestamptz,
      ${e.action==="REMOVE"?e.occurredAt:null}::timestamptz,${e.terminalId},${e.occurredAt}::timestamptz,${e.externalId})
    ON CONFLICT(datum,spieler_id) DO UPDATE SET
     added_at=CASE WHEN ${e.action}='ADD' THEN EXCLUDED.added_at ELSE gameday_players.added_at END,
     removed_at=EXCLUDED.removed_at,terminal_id=EXCLUDED.terminal_id,occurred_at=EXCLUDED.occurred_at,external_id=EXCLUDED.external_id
    WHERE (EXCLUDED.occurred_at,EXCLUDED.external_id)>(gameday_players.occurred_at,gameday_players.external_id)`);
   results.push({externalId:e.externalId,status:inserted.length?"accepted":"skipped"});
  }
  await cancelIneligibleQueue(tx);await compactQueue(tx);return results;
 });
 res.json({results});
});
export default router;
