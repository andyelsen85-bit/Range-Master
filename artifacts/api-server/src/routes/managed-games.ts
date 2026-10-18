import { Router } from "express";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { z } from "zod";
import { authenticate,requireAdmin } from "./auth";
import { gameScorecard } from "../lib/game-scorecard";
import { computeTaubePunkte } from "../lib/scoring";
import { daySchema } from "../lib/local-day";
const router=Router(),id=z.coerce.number().int().positive();
router.use("/admin/spiele",authenticate,requireAdmin);
router.get("/admin/spiele",async(req,res)=>{
 const page=z.coerce.number().int().min(1).max(100000).parse(req.query.page??1),q=z.string().max(100).parse(req.query.q??"");
 const from=req.query.from?daySchema.parse(req.query.from):null,to=req.query.to?daySchema.parse(req.query.to):null;
 const hidden=z.enum(["all","yes","no"]).parse(req.query.hidden??"all");
 const filter=sql`(${from}::date IS NULL OR (g.datum AT TIME ZONE 'Europe/Luxembourg')::date>=${from}::date)
  AND (${to}::date IS NULL OR (g.datum AT TIME ZONE 'Europe/Luxembourg')::date<=${to}::date)
  ${req.query.modus?sql`AND g.modus::text=${String(req.query.modus)}`:sql``}
  ${hidden==="yes"?sql`AND g.hidden_at IS NOT NULL`:hidden==="no"?sql`AND g.hidden_at IS NULL`:sql``}
  ${req.query.spielerId?sql`AND EXISTS(SELECT 1 FROM spiel_teilnahmen t WHERE t.spiel_id=g.id AND t.spieler_id=${id.parse(req.query.spielerId)})`:sql``}
  AND (g.id::text ILIKE ${`%${q}%`} OR g.external_id ILIKE ${`%${q}%`} OR EXISTS(
    SELECT 1 FROM spiel_teilnahmen t JOIN spieler s ON s.id=t.spieler_id WHERE t.spiel_id=g.id AND s.name ILIKE ${`%${q}%`}))`;
 const [count]=(await db.execute(sql`SELECT count(*)::int total FROM spiele g WHERE ${filter}`)).rows as any[];
 const items=(await db.execute(sql`SELECT g.*,(SELECT count(DISTINCT spieler_id)::int FROM spiel_teilnahmen WHERE spiel_id=g.id) "playerCount"
  FROM spiele g WHERE ${filter} ORDER BY g.datum DESC,g.id DESC LIMIT 50 OFFSET ${(page-1)*50}`)).rows;
 res.json({items,total:count.total,page});
});
router.get("/admin/spiele/:id",async(req,res)=>{
 const gameId=id.parse(req.params.id);
 const [game]=(await db.execute(sql`SELECT * FROM spiele WHERE id=${gameId}`)).rows;
 if(!game)return void res.sendStatus(404);
 const players=(await db.execute(sql`SELECT s.id,s.name,bool_or(h.spieler_id IS NOT NULL) hidden FROM spiel_teilnahmen t
  JOIN spieler s ON s.id=t.spieler_id LEFT JOIN spiel_spieler_hidden h ON h.spiel_id=t.spiel_id AND h.spieler_id=t.spieler_id
  WHERE t.spiel_id=${gameId} GROUP BY s.id,s.name ORDER BY min(t.start_posten),s.id`)).rows;
 const shots=(await db.execute(sql`SELECT * FROM ergebnisse WHERE spiel_id=${gameId} ORDER BY spieler_id,lauf,taube,id`)).rows;
 const audit=(await db.execute(sql`SELECT * FROM admin_game_edits WHERE spiel_id=${gameId} ORDER BY at DESC,id DESC`)).rows;
 res.json({game,scorecard:await gameScorecard(gameId,{includeHidden:true}),players,shots,audit});
});
for(const action of ["hide","unhide"] as const) {
 for(const player of [false,true])router.post(player?`/admin/spiele/:id/spieler/:spielerId/${action}`:`/admin/spiele/:id/${action}`,async(req,res)=>{
  const gameId=id.parse(req.params.id),playerId=player?id.parse((req.params as any).spielerId):null,adminId=(req as any).user.id;
  const success=await db.transaction(async tx=>{
   const [before]=(await tx.execute(sql`SELECT * FROM spiele WHERE id=${gameId} FOR UPDATE`)).rows;
   if(!before)return false;
   let previous:unknown=before;
   if(playerId) {
    const participates=(await tx.execute(sql`SELECT 1 FROM spiel_teilnahmen WHERE spiel_id=${gameId} AND spieler_id=${playerId} LIMIT 1`)).rows;
    if(!participates.length)return false;
    previous=(await tx.execute(sql`SELECT * FROM spiel_spieler_hidden WHERE spiel_id=${gameId} AND spieler_id=${playerId}`)).rows[0]??null;
    if(action==="hide")await tx.execute(sql`INSERT INTO spiel_spieler_hidden(spiel_id,spieler_id,hidden_by) VALUES(${gameId},${playerId},${adminId}) ON CONFLICT DO NOTHING`);
    else await tx.execute(sql`DELETE FROM spiel_spieler_hidden WHERE spiel_id=${gameId} AND spieler_id=${playerId}`);
   } else await tx.execute(sql`UPDATE spiele SET hidden_at=${action==="hide"?new Date():null},hidden_by=${action==="hide"?adminId:null} WHERE id=${gameId}`);
   await tx.execute(sql`INSERT INTO admin_game_edits(admin_user_id,action,spiel_id,spieler_id,before,after)
    VALUES(${adminId},${`${action.toUpperCase()}_${player?"PLAYER":"GAME"}`},${gameId},${playerId},
     ${JSON.stringify(previous)}::jsonb,${JSON.stringify({hidden:action==="hide"})}::jsonb)`);
   return true;
  });res.status(success?200:404).json({success});
 });
}
router.patch("/admin/spiele/:id/ergebnisse/:ergebnisId",async(req,res)=>{
 const body=z.object({schuss1:z.boolean(),schuss2:z.boolean(),wiederholt:z.boolean().optional()}).parse(req.body);
 const gameId=id.parse(req.params.id),shotId=id.parse(req.params.ergebnisId),adminId=(req as any).user.id;
 const result=await db.transaction(async tx=>{
  await tx.execute(sql`SELECT id FROM spiele WHERE id=${gameId} FOR UPDATE`);
  const [before]=(await tx.execute(sql`SELECT * FROM ergebnisse WHERE id=${shotId} AND spiel_id=${gameId} FOR UPDATE`)).rows as any[];
  if(!before)return null;
  const neighbor=(await tx.execute(sql`SELECT 1 FROM ergebnisse WHERE spiel_id=${gameId} AND spieler_id=${before.spieler_id}
   AND lauf=${before.lauf} AND posten=${before.posten} AND abs(taube-${before.taube})=1 LIMIT 1`)).rows.length>0;
  const after={...before,...body,punkte:computeTaubePunkte({...before,...body},before.maschine==="H"||neighbor)};
  await tx.execute(sql`UPDATE ergebnisse SET schuss1=${after.schuss1},schuss2=${after.schuss2},wiederholt=${after.wiederholt},punkte=${after.punkte} WHERE id=${shotId}`);
  await tx.execute(sql`UPDATE spiel_teilnahmen t SET punkte=(SELECT COALESCE(sum(e.punkte),0) FROM ergebnisse e WHERE
    e.spiel_id=t.spiel_id AND e.spieler_id=t.spieler_id AND e.lauf=t.lauf AND NOT e.wiederholt) WHERE t.spiel_id=${gameId} AND t.spieler_id=${before.spieler_id} AND t.lauf=${before.lauf}`);
  await tx.execute(sql`UPDATE spiele SET edited_at=now(),edited_by=${adminId} WHERE id=${gameId}`);
  await tx.execute(sql`INSERT INTO admin_game_edits(admin_user_id,action,spiel_id,spieler_id,before,after)
   VALUES(${adminId},'EDIT_RESULT',${gameId},${before.spieler_id},${JSON.stringify(before)}::jsonb,${JSON.stringify(after)}::jsonb)`);
  return after;
 });res.status(result?200:404).json(result??{error:"Ergebnis nicht gefunden."});
});
export default router;
