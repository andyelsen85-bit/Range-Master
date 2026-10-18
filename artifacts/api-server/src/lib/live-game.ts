import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { z } from "zod";
import { queueLock } from "./queue";
export const liveSnapshotSchema=z.object({
 terminalId:z.string().min(1).max(100).refine(v=>v!=="ESP32-P4-UNKNOWN"),seq:z.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER),
 state:z.enum(["RUNNING","FINISHED","ABORTED"]),spielExternalId:z.string().min(1).max(200),
 modus:z.enum(["NORMAL","HARAKIRI","HARAKIRI_CUSTOM","CUSTOM_1","CUSTOM_2","CUSTOM_3","CUSTOM_4"]),
 lauf:z.number().int().min(1).max(2),taubenProLauf:z.number().int().min(1).max(32),startedAt:z.string().datetime(),
 currentSpielerId:z.number().int(),currentTaube:z.number().int().min(0).max(32),
 players:z.array(z.object({spielerId:z.number().int().positive(),name:z.string().min(1).max(64),posten:z.number().int().min(1).max(6)})).max(6),
 results:z.array(z.object({spielerId:z.number().int().positive(),lauf:z.number().int().min(1).max(2),taube:z.number().int().min(1).max(32),
   maschine:z.enum(["A","B","C","D","E","F","G","H"]),schuss1:z.boolean(),schuss2:z.boolean(),punkte:z.number().int().min(0).max(2)})).max(384),
}).superRefine((v,ctx)=>{
 const ids=new Set(v.players.map(p=>p.spielerId)),seen=new Set<string>();
 if(v.state!=="ABORTED"&&!ids.size)ctx.addIssue({code:"custom",message:"Spieler fehlen"});
 if(ids.size!==v.players.length)ctx.addIssue({code:"custom",message:"Doppelte Spieler"});
 for(const r of v.results){const key=`${r.spielerId}:${r.lauf}:${r.taube}`;
   if(!ids.has(r.spielerId)||seen.has(key)||r.taube>v.taubenProLauf)ctx.addIssue({code:"custom",message:"Ungültiges Ergebnis"});seen.add(key);}
 if(v.state==="RUNNING"&&!ids.has(v.currentSpielerId))ctx.addIssue({code:"custom",message:"Unbekannter Schütze"});
});
export async function storeLiveSnapshot(payload:z.infer<typeof liveSnapshotSchema>,keyId:number) {
  // Key ownership is stored outside the public payload; other terminals cannot overwrite a known ID.
  const data={...payload,_keyId:keyId};
  return db.transaction(async tx=>{
  await queueLock(tx);
  if((await tx.execute(sql`SELECT 1 FROM spiele WHERE external_id=${payload.spielExternalId} LIMIT 1`)).rows.length)return {accepted:false};
  const rows=await tx.execute(sql`INSERT INTO live_game_state(terminal_id,seq,state,payload)
    VALUES(${payload.terminalId},${payload.seq},${payload.state},${JSON.stringify(data)}::jsonb)
    ON CONFLICT(terminal_id) DO UPDATE SET seq=EXCLUDED.seq,state=EXCLUDED.state,payload=EXCLUDED.payload,updated_at=now()
    WHERE (live_game_state.payload->>'_keyId')::int=${keyId} AND
    ((EXCLUDED.payload->>'startedAt')::timestamptz>(live_game_state.payload->>'startedAt')::timestamptz OR
      ((EXCLUDED.payload->>'startedAt')::timestamptz=(live_game_state.payload->>'startedAt')::timestamptz AND EXCLUDED.seq>live_game_state.seq))
    RETURNING terminal_id`);
  return {accepted:rows.rows.length>0};
  });
}
