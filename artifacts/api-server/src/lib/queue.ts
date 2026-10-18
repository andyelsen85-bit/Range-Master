import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { createHash } from "node:crypto";
import { publicName } from "@workspace/scorecard";
import {queueEligibility,cancelIneligibleQueue} from "./queue-eligibility";
export class PortalConflict extends Error { constructor(message:string,public status=409){super(message);} }
export type Tx=Parameters<Parameters<typeof db.transaction>[0]>[0];
export const queueLock=async(tx:Tx)=>{await tx.execute(sql`SELECT pg_advisory_xact_lock(hashtextextended('portal-queue',0))`);};
export async function queueAudit(tx:Tx,action:string,details:unknown={},context:any={}) {
  await tx.execute(sql`INSERT INTO queue_audit(action,details,token_id,ip_hash,group_id,spieler_id)
    VALUES(${action},${JSON.stringify(details)}::jsonb,${context.id??null},${context.ipHash??null},${context.groupId??null},${context.spielerId??null})`);
}
export async function compactQueue(tx:Tx) {
  await tx.execute(sql`UPDATE queue_games SET status='CANCELLED' WHERE status='OPEN'
    AND NOT EXISTS(SELECT 1 FROM queue_entries e WHERE e.queue_game_id=queue_games.id AND e.status='OPEN')`);
  await tx.execute(sql`WITH p AS(SELECT id,row_number() OVER(ORDER BY position,id) n FROM queue_games WHERE status IN('OPEN','LOCKED'))
    UPDATE queue_games g SET position=p.n FROM p WHERE g.id=p.id`);
}
export async function queueSnapshot(full=false,owner?:string) {
  const [settings]=(await db.execute(sql`SELECT queue_enabled,groups_enabled,show_ranking,max_queue_games,enabled_modes,name_mode FROM public_page_settings WHERE id=1`)).rows as any[];
  const rows=(await db.execute(sql`SELECT g.id,g.position,g.modus,g.status,e.id "entryId",e.spieler_id "spielerId",s.name,e.group_id "groupId"
    FROM queue_games g LEFT JOIN queue_entries e ON e.queue_game_id=g.id AND e.status IN('OPEN','TAKEN')
     LEFT JOIN spieler s ON s.id=e.spieler_id
     WHERE ${full?sql`(g.status='OPEN' OR (g.status='LOCKED' AND g.terminal_id=${owner??""}))`:sql`g.status IN('OPEN','LOCKED')`}
     ORDER BY g.position,g.id,e.id`)).rows as any[];
  const map=new Map<number,any>();
  for(const r of rows) {
    if(!map.has(r.id))map.set(r.id,{queueGameId:r.id,position:r.position,modus:r.modus,status:r.status,players:[]});
    if(r.entryId)map.get(r.id).players.push({entryId:r.entryId,spielerId:r.spielerId,name:r.name,groupId:r.groupId});
  }
  const games=settings?.queue_enabled?[...map.values()].map(g=>({...g,free:6-g.players.length})):[];
  return {version:createHash("sha256").update(JSON.stringify({games,settings})).digest("hex"),games,
    settings:{queueEnabled:settings?.queue_enabled??false,groupsEnabled:settings?.groups_enabled??true,
      showRanking:settings?.show_ranking??false,maxQueueGames:settings?.max_queue_games??6,enabledModes:settings?.enabled_modes??["NORMAL"]}};
}
export async function signupQueue(ids:number[],modus:string,context:any={},groupId:number|null=null) {
  if(!ids.length||ids.length>6||new Set(ids).size!==ids.length)throw new PortalConflict("Eine Gruppe muss 2 bis 6 verschiedene Spieler enthalten.",400);
  return db.transaction(async tx=>{
    await queueLock(tx);
    const [settings]=(await tx.execute(sql`SELECT * FROM public_page_settings WHERE id=1`)).rows as any[];
    if(!settings?.queue_enabled)throw new PortalConflict("Die Warteschlange ist ausgeschaltet.",404);
    if(!settings.enabled_modes.includes(modus))throw new PortalConflict("Dieser Spielmodus ist nicht freigegeben.",400);
    if(groupId&&!settings.groups_enabled)throw new PortalConflict("Gruppen sind ausgeschaltet.",403);
    if(groupId) {
      ids=(await tx.execute(sql`SELECT m.spieler_id FROM spieler_gruppen_mitglieder m JOIN spieler_gruppen g ON g.id=m.gruppe_id
        WHERE g.id=${groupId} AND g.deleted_at IS NULL ORDER BY m.spieler_id`)).rows.map((r:any)=>r.spieler_id);
      if(ids.length<2||ids.length>6)throw new PortalConflict("Die Gruppe wurde inzwischen geändert.");
    }
    const list=sql.join(ids.map(id=>sql`${id}`),sql`,`);
    const players=(await tx.execute(sql`SELECT id,name FROM spieler WHERE id IN(${list}) AND aktiv ORDER BY id`)).rows as any[];
    if(players.length!==ids.length)throw new PortalConflict("Ein Spieler ist nicht mehr aktiv.");
     const eligibility=await queueEligibility(tx,ids);
     const ineligible=eligibility.filter(p=>!p.registered||p.available<1);
     if(ineligible.length)throw new PortalConflict(ineligible.map(p=>!p.registered
       ?`${p.name} ist heute nicht eingetragen.`:`${p.name} hat keine Credits gebucht.`).join(" "),403);
    const blockers=(await tx.execute(sql`SELECT s.name,g.status FROM queue_entries e JOIN spieler s ON s.id=e.spieler_id
      JOIN queue_games g ON g.id=e.queue_game_id WHERE e.spieler_id IN(${list}) AND e.status IN('OPEN','TAKEN')`)).rows as any[];
    if(blockers.length)throw new PortalConflict(blockers.map(p=>p.status==="STARTED"
       ?`${p.name} schießt gerade. Erneut einschreiben ist nach dem Ergebnis möglich.`
       :`${p.name} hat noch einen offenen Eintrag.`).join(" "));
    const [count]=(await tx.execute(sql`SELECT count(*)::int n FROM queue_entries WHERE status IN('OPEN','TAKEN')`)).rows as any[];
    if(count.n+ids.length>40)throw new PortalConflict("Die Warteschlange ist voll.");
    const [available]=(await tx.execute(sql`SELECT g.id FROM queue_games g WHERE g.status='OPEN' AND g.modus::text=${modus}
      AND (SELECT count(*) FROM queue_entries e WHERE e.queue_game_id=g.id AND e.status='OPEN')<=${6-ids.length}
      ORDER BY g.position,g.id LIMIT 1 FOR UPDATE`)).rows as any[];
    let gameId=available?.id;
    if(!gameId) {
      const [countGames]=(await tx.execute(sql`SELECT count(*)::int n,COALESCE(max(position),0)+1 p FROM queue_games WHERE status IN('OPEN','LOCKED')`)).rows as any[];
      if(countGames.n>=settings.max_queue_games)throw new PortalConflict("Alle Warteschlangenplätze sind belegt.");
      const [created]=(await tx.execute(sql`INSERT INTO queue_games(modus,position) VALUES(${modus}::modus,${countGames.p}) RETURNING id`)).rows as any[];
      gameId=created.id;
    }
    const entries=[];
    for(const id of ids)entries.push((await tx.execute(sql`INSERT INTO queue_entries(queue_game_id,spieler_id,group_id,modus,signed_by_ip_hash)
      VALUES(${gameId},${id},${groupId},${modus},${context.ipHash??null}) RETURNING id`)).rows[0]);
    await queueAudit(tx,"SIGNUP",{gameId,entries},{...context,groupId});
    return {success:true,queueGameId:gameId};
  });
}
export async function cancelQueue(entryId:number,context:any={}) {
  return db.transaction(async tx=>{
    await queueLock(tx);
    const rows=(await tx.execute(sql`UPDATE queue_entries e SET status='CANCELLED',cancel_reason='USER'
      FROM queue_games g WHERE e.id=${entryId} AND e.queue_game_id=g.id AND g.status='OPEN' AND e.status='OPEN' RETURNING e.id`)).rows;
    if(!rows.length)throw new PortalConflict("Dieser Eintrag kann nicht mehr ausgetragen werden.");
    await compactQueue(tx);await queueAudit(tx,"CANCEL",{entryId},context);return {success:true};
  });
}
export async function mutateQueue(kind:"take"|"release"|"started",body:any,keyId:number) {
  return db.transaction(async tx=>{
    await queueLock(tx);
    const [game]=(await tx.execute(sql`SELECT * FROM queue_games WHERE id=${body.queueGameId} FOR UPDATE`)).rows as any[];
    if(!game)throw new PortalConflict("Warteschlange nicht verfügbar.");
    const owner=`key:${keyId}:${body.terminalId}`;
    if(kind==="take") {
      const [settings]=(await tx.execute(sql`SELECT queue_enabled FROM public_page_settings WHERE id=1`)).rows as any[];
      if(!settings?.queue_enabled)throw new PortalConflict("Die Warteschlange ist ausgeschaltet.",404);
      const ids=(await tx.execute(sql`SELECT id FROM queue_entries WHERE queue_game_id=${game.id} AND status IN('OPEN','TAKEN') ORDER BY id`)).rows.map((r:any)=>r.id);
      if(JSON.stringify(ids)!==JSON.stringify([...body.entryIds].sort((a:number,b:number)=>a-b)))throw new PortalConflict("Die Spieler wurden inzwischen geändert.");
      if(game.status==="LOCKED"&&game.terminal_id===owner)return {success:true};
      if(game.status!=="OPEN"||!ids.length)throw new PortalConflict("Dieses Spiel wird bereits aufgesetzt.");
       const playerIds=(await tx.execute(sql`SELECT spieler_id FROM queue_entries WHERE queue_game_id=${game.id} AND status='OPEN'`)).rows.map((r:any)=>r.spieler_id);
       const eligible=await queueEligibility(tx,playerIds);
       if(eligible.length!==playerIds.length||eligible.some(p=>!p.registered||p.available<1))
         throw new PortalConflict("Die Berechtigung wurde geändert. Tagesliste und Credits erneut synchronisieren.");
      await tx.execute(sql`UPDATE queue_games SET status='LOCKED',locked_at=now(),terminal_id=${owner} WHERE id=${game.id}`);
      await tx.execute(sql`UPDATE queue_entries SET status='TAKEN' WHERE queue_game_id=${game.id} AND status='OPEN'`);
    } else {
      if(kind==="release"&&game.status==="OPEN"&&!game.terminal_id)return {success:true};
      if(game.terminal_id!==owner)throw new PortalConflict("Dieses Spiel gehört zu einem anderen Terminal.",403);
      if(kind==="release") {
        if(["STARTED","DONE"].includes(game.status))throw new PortalConflict("Das Spiel wurde bereits gestartet.");
        await tx.execute(sql`UPDATE queue_games SET status='OPEN',locked_at=NULL WHERE id=${game.id}`);
        await tx.execute(sql`UPDATE queue_entries SET status='OPEN' WHERE queue_game_id=${game.id} AND status='TAKEN'`);
      } else {
        if(["STARTED","DONE"].includes(game.status)&&game.started_external_id===body.spielExternalId)return {success:true};
        if(game.status!=="LOCKED")throw new PortalConflict("Das Spiel ist nicht reserviert.");
        await tx.execute(sql`UPDATE queue_games SET status='STARTED',started_at=now(),started_external_id=${body.spielExternalId},
          started_spiel_id=(SELECT id FROM spiele WHERE external_id=${body.spielExternalId}) WHERE id=${game.id}`);
        // A delayed started event may arrive after the completed game.
        const [stored]=(await tx.execute(sql`SELECT id,abgeschlossen FROM spiele WHERE external_id=${body.spielExternalId}`)).rows as any[];
        if(stored)await closeQueueEntries(tx,stored.id,body.spielExternalId,stored.abgeschlossen);
      }
    }
    await compactQueue(tx);await queueAudit(tx,kind.toUpperCase(),{queueGameId:game.id,terminalId:body.terminalId});
    return {success:true};
  });
}
export async function closeQueueEntries(tx:Tx,spielId:number,externalId:string,completed:boolean) {
  await queueLock(tx);
     await cancelIneligibleQueue(tx);
  // Result-bearing manual games close OPEN entries, never another terminal's reservation.
  await tx.execute(sql`UPDATE queue_entries e SET status='DONE',spiel_id=${spielId}
    WHERE e.status IN('OPEN','TAKEN') AND (
      (e.status='OPEN' AND EXISTS(SELECT 1 FROM ergebnisse r WHERE r.spiel_id=${spielId} AND r.spieler_id=e.spieler_id))
      OR EXISTS(SELECT 1 FROM queue_games q WHERE q.id=e.queue_game_id AND q.started_external_id=${externalId}
        AND (${completed} OR EXISTS(SELECT 1 FROM ergebnisse r WHERE r.spiel_id=${spielId} AND r.spieler_id=e.spieler_id))))`);
  await tx.execute(sql`UPDATE queue_games SET started_spiel_id=${spielId},status=CASE WHEN ${completed} THEN 'DONE' ELSE status END
    WHERE started_external_id=${externalId}`);
  await compactQueue(tx);
}
export async function pruneQueue() {
  await db.transaction(async tx=>{
    await queueLock(tx);
    await tx.execute(sql`UPDATE queue_entries SET status='CANCELLED',cancel_reason='EXPIRED' WHERE status IN('OPEN','TAKEN') AND signup_at<now()-interval '12 hours'`);
    await tx.execute(sql`UPDATE queue_entries e SET status='OPEN' FROM queue_games g WHERE e.queue_game_id=g.id
      AND g.status='LOCKED' AND g.locked_at<now()-interval '15 minutes' AND e.status='TAKEN'`);
    await tx.execute(sql`UPDATE queue_games SET status='OPEN',terminal_id=NULL,locked_at=NULL WHERE status='LOCKED' AND locked_at<now()-interval '15 minutes'`);
    await compactQueue(tx);
    await tx.execute(sql`DELETE FROM queue_audit WHERE at<now()-interval '90 days'`);
    await tx.execute(sql`DELETE FROM admin_game_edits WHERE at<now()-interval '365 days'`);
    await tx.execute(sql`DELETE FROM live_game_state WHERE updated_at<now()-interval '24 hours'`);
  });
}
