import { Router, type ErrorRequestHandler } from "express";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { z } from "zod";
import { requireApiKey } from "./auth";
import { rateLimit } from "../lib/rate-limit";
import { queueSnapshot,signupQueue,cancelQueue,mutateQueue,queueLock,queueAudit,PortalConflict } from "../lib/queue";
import { liveSnapshotSchema,storeLiveSnapshot } from "../lib/live-game";
import { publicName } from "@workspace/scorecard";
import { computePlayerStats } from "../lib/player-stats";
import { gameScorecard } from "../lib/game-scorecard";
import { daySchema,localDay } from "../lib/local-day";
import { computeRangliste } from "./rangliste-compute";
const router=Router(),id=z.coerce.number().int().positive(),mode=z.enum(["NORMAL","HARAKIRI","HARAKIRI_CUSTOM","CUSTOM_1","CUSTOM_2","CUSTOM_3","CUSTOM_4"]);
router.use("/public",(req,res,next)=>rateLimit(req.method==="GET"?"read":"write",req.method==="GET"?60:10)(req,res,next));
router.get("/public/queue",async(_req,res)=>res.json(await queueSnapshot()));
router.get("/public/queue/players",async(req,res)=>{
  const q=z.string().max(80).parse(req.query.q??"");
  const rows=(await db.execute(sql`SELECT s.id,s.name,e.status,g.status "gameStatus",v.available::int available
    FROM spieler s JOIN v_queue_eligibility v ON v.id=s.id AND v.registered
    LEFT JOIN queue_entries e ON e.spieler_id=s.id AND e.status IN('OPEN','TAKEN')
    LEFT JOIN queue_games g ON g.id=e.queue_game_id
    WHERE s.aktiv AND s.name ILIKE ${`%${q.replace(/[\\%_]/g,"\\$&")}%`} ORDER BY s.name,s.id LIMIT 20`)).rows as any[];
  res.json({players:rows.map(r=>{
    return {id:r.id,displayName:r.name,available:r.available,canSignup:r.available>=1,
       eligibilityHint:r.available<1?"Credits am Stand buchen":null,
      activeSignup:!!r.status,running:r.gameStatus==="STARTED"};
  })});
});
router.post("/public/queue/signup",async(req,res)=>{
  const body=z.object({spielerId:id,modus:mode.default("NORMAL")}).parse(req.body);
  res.status(201).json(await signupQueue([body.spielerId],body.modus,res.locals.kiosk));
});
router.post("/public/queue/cancel",async(req,res)=>res.json(await cancelQueue(z.object({entryId:id}).parse(req.body).entryId,res.locals.kiosk)));
router.get("/public/groups",async(_req,res)=>{
  if(!res.locals.kiosk.settings.groups_enabled)return void res.json({groups:[]});
  const rows=(await db.execute(sql`SELECT g.id,g.name,m.spieler_id,s.name "playerName" FROM spieler_gruppen g
    JOIN spieler_gruppen_mitglieder m ON m.gruppe_id=g.id JOIN spieler s ON s.id=m.spieler_id
    JOIN v_queue_eligibility v ON v.id=s.id AND v.registered
    WHERE g.deleted_at IS NULL ORDER BY g.id,m.spieler_id`)).rows as any[];
  const groups=new Map<number,any>();
  for(const r of rows){if(!groups.has(r.id))groups.set(r.id,{id:r.id,name:r.name,members:[]});
    groups.get(r.id).members.push({id:r.spieler_id,displayName:r.playerName});}
  res.json({groups:[...groups.values()]});
});
const groupInput=z.object({name:z.string().trim().min(1).max(80),spielerIds:z.array(id).min(2).max(6).refine(a=>new Set(a).size===a.length)});
for(const method of ["post","put"] as const)router[method](method==="post"?"/public/groups":"/public/groups/:id",async(req,res)=>{
  if(!res.locals.kiosk.settings.groups_enabled)return void res.status(422).json({code:"groups_disabled",message:"Gruppen sind deaktiviert.",error:"Gruppen sind deaktiviert."});
  const body=groupInput.parse(req.body);
  const result=await db.transaction(async tx=>{
    await queueLock(tx);let groupId=method==="put"?id.parse((req.params as any).id):0;
    const active=(await tx.execute(sql`SELECT id FROM v_queue_eligibility WHERE registered AND id IN(${sql.join(body.spielerIds.map(n=>sql`${n}`),sql`,`)})`)).rows;
    if(active.length!==body.spielerIds.length)throw new PortalConflict("Ein Spieler ist heute nicht eingetragen.",422,"queue_ineligible");
    if(groupId) {
      const updated=(await tx.execute(sql`UPDATE spieler_gruppen SET name=${body.name},updated_at=now() WHERE id=${groupId} AND deleted_at IS NULL RETURNING id`)).rows;
      if(!updated.length)throw new PortalConflict("Gruppe nicht gefunden.",404);
      await tx.execute(sql`DELETE FROM spieler_gruppen_mitglieder WHERE gruppe_id=${groupId}`);
    } else {
      const [count]=(await tx.execute(sql`SELECT count(*)::int n FROM spieler_gruppen WHERE deleted_at IS NULL`)).rows as any[];
      if(count.n>=20)throw new PortalConflict("Höchstens 20 Gruppen sind möglich.");
      const [created]=(await tx.execute(sql`INSERT INTO spieler_gruppen(name) VALUES(${body.name}) RETURNING id`)).rows as any[];groupId=created.id;
    }
    for(const p of body.spielerIds)await tx.execute(sql`INSERT INTO spieler_gruppen_mitglieder VALUES(${groupId},${p})`);
    await queueAudit(tx,"GROUP_SAVED",{groupId,...body},res.locals.kiosk);return {id:groupId};
  });res.json(result);
});
router.delete("/public/groups/:id",async(req,res)=>{
  if(!res.locals.kiosk.settings.groups_enabled)throw new PortalConflict("Gruppen sind deaktiviert.",422,"groups_disabled");
  await db.transaction(async tx=>{await queueLock(tx);const groupId=id.parse(req.params.id);
    await tx.execute(sql`UPDATE spieler_gruppen SET deleted_at=now() WHERE id=${groupId}`);
    await queueAudit(tx,"GROUP_DELETED",{groupId},res.locals.kiosk);});res.json({success:true});
});
router.post("/public/groups/:id/signup",async(req,res)=>{
  if(!res.locals.kiosk.settings.groups_enabled)throw new PortalConflict("Gruppen sind deaktiviert.",422,"groups_disabled");
  const groupId=id.parse(req.params.id),body=z.object({modus:mode.default("NORMAL")}).parse(req.body);
  const members=(await db.execute(sql`SELECT m.spieler_id FROM spieler_gruppen_mitglieder m JOIN spieler_gruppen g ON g.id=m.gruppe_id WHERE g.id=${groupId} AND g.deleted_at IS NULL`)).rows as any[];
  res.json(await signupQueue(members.map(p=>p.spieler_id),body.modus,res.locals.kiosk,groupId));
});
router.get("/public/player/:id/days",async(req,res)=>{
  const days=(await db.execute(sql`SELECT DISTINCT (g.datum AT TIME ZONE 'Europe/Luxembourg')::date::text datum
    FROM v_spiele_visible g JOIN v_teilnahmen_visible t ON t.spiel_id=g.id WHERE t.spieler_id=${id.parse(req.params.id)} ORDER BY datum DESC`)).rows.map((r:any)=>r.datum);
  res.json({days});
});
router.get("/public/player/:id/print",rateLimit("print",6),async(req,res)=>{
  const playerId=id.parse(req.params.id),tag=daySchema.parse(req.query.tag??localDay());
  const [player]=(await db.execute(sql`SELECT name FROM spieler WHERE id=${playerId} AND aktiv`)).rows as any[];
  if(!player)return void res.sendStatus(404);
  const ids=(await db.execute(sql`SELECT g.id FROM v_spiele_visible g JOIN v_teilnahmen_visible t ON t.spiel_id=g.id
    WHERE t.spieler_id=${playerId} AND (g.datum AT TIME ZONE 'Europe/Luxembourg')::date=${tag}::date GROUP BY g.id ORDER BY g.id`)).rows as any[];
  res.json({name:player.name,tag,club:"Rangemaster",games:await Promise.all(ids.map(g=>gameScorecard(g.id,{viewerSpielerId:playerId,anonymise:true}))),
    stats:await computePlayerStats(playerId,{from:tag,to:tag})});
});
router.get("/sync/queue",requireApiKey,async(req,res)=>{
  if((req as any).apiKeyType!=="TERMINAL")return void res.sendStatus(403);
  const terminalId=z.string().min(1).max(100).optional().parse(req.query.terminalId);
  res.json(await queueSnapshot(true,terminalId?`key:${(req as any).apiKeyId}:${terminalId}`:undefined));
});
const queueCommand=z.object({queueGameId:id,terminalId:z.string().min(1).max(100).refine(v=>v!=="ESP32-P4-UNKNOWN"),
  entryIds:z.array(id).min(1).max(6).optional(),spielExternalId:z.string().min(1).max(200).optional()});
for(const kind of ["take","release","started"] as const)router.post(`/sync/queue/${kind}`,requireApiKey,async(req,res)=>{
  if((req as any).apiKeyType!=="TERMINAL")return void res.sendStatus(403);
  const body=queueCommand.parse(req.body);
  if(kind==="take"&&!body.entryIds||kind==="started"&&!body.spielExternalId)return void res.sendStatus(400);
  res.json(await mutateQueue(kind,body,(req as any).apiKeyId));
});
router.post("/sync/live",requireApiKey,async(req,res)=>{
  if((req as any).apiKeyType!=="TERMINAL")return void res.sendStatus(403);
  // Partial snapshots larger than 4 KB are explicitly accepted up to 64 KB:
  // the specified 6x2x32 result objects cannot fit in 4 KB (documented).
  if(Buffer.byteLength(JSON.stringify(req.body))>65536)return void res.sendStatus(413);
  res.json(await storeLiveSnapshot(liveSnapshotSchema.parse(req.body),(req as any).apiKeyId));
});
router.get("/public/ranking",async(_req,res)=>{
  if(!res.locals.kiosk.settings.show_ranking)return void res.json({rangliste:[]});
  const year=new Date().getFullYear();
  const games=(await db.execute(sql`SELECT id,modus,tauben_pro_lauf "taubenProLauf",lauf,datum FROM v_spiele_visible
    WHERE extract(year FROM datum)=${year}`)).rows as any[];
  const parts=(await db.execute(sql`SELECT spieler_id "spielerId",spiel_id "spielId",punkte FROM v_teilnahmen_visible`)).rows as any[];
  const players=(await db.execute(sql`SELECT id,name FROM spieler`)).rows as any[];
  res.json({rangliste:computeRangliste(games,parts,players).slice(0,15)});
});
router.get("/public/live-game",async(_req,res)=>{
  const [row]=(await db.execute(sql`SELECT payload,updated_at FROM live_game_state
    WHERE state='RUNNING' AND updated_at>now()-interval '120 seconds' ORDER BY updated_at DESC,terminal_id LIMIT 1`)).rows as any[];
  if(!row)return void res.json(null);
  const {_keyId,...payload}=row.payload;
  res.json({...payload,updatedAt:row.updated_at,players:payload.players.map((p:any)=>({...p,name:publicName(p.name,res.locals.kiosk.settings.name_mode)}))});
});
const errors:ErrorRequestHandler=(err,_req,res,next)=>{
  if(err?.name==="ZodError")return void res.status(400).json({code:"invalid_request",message:"Die Eingaben sind ungültig.",error:"Die Eingaben sind ungültig."});
  if(err instanceof PortalConflict)return void res.status(err.status).json({code:err.code,message:err.message,error:err.message});
  if(err?.code==="23505")return void res.status(409).json({code:"queue_duplicate",message:"Dieser Spieler hat bereits einen offenen Eintrag.",error:"Dieser Spieler hat bereits einen offenen Eintrag."});
  next(err);
};
router.use(errors);
export default router;
