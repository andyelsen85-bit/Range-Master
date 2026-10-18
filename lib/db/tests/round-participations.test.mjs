import test from "node:test";
import assert from "node:assert/strict";
import {createRequire} from "node:module";
import {randomUUID} from "node:crypto";
import {readFile} from "node:fs/promises";
import path from "node:path";
import {fileURLToPath} from "node:url";
const require=createRequire(import.meta.url),pg=require("pg"),OriginalPool=pg.Pool;
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),"../../..");

test("round totals: real PG migration, legacy/new HTTP replay, all consumers and financial isolation",async()=>{
 const owner=new OriginalPool({connectionString:process.env.DATABASE_URL});
 const name=`rm_rounds_${randomUUID().replaceAll("-","")}`,opened=new Set();
 await owner.query(`CREATE DATABASE "${name}"`);
 const url=new URL(process.env.DATABASE_URL);url.pathname=`/${name}`;process.env.DATABASE_URL=url.href;
 pg.Pool=class extends OriginalPool{constructor(...args){super(...args);opened.add(this);}};
 let pool,server;
 try {
  const database=await import("@workspace/db");pool=database.pool;
  const notices=[];pool.on("connect",client=>client.on("notice",notice=>notices.push(notice.message)));
  const {migrate}=await import("drizzle-orm/node-postgres/migrator");
  const config={migrationsFolder:path.join(root,"lib/db/drizzle")};
  await migrate(database.db,config);
  const journal=JSON.parse(await readFile(path.join(config.migrationsFolder,"meta/_journal.json"),"utf8"));
  assert.equal((await pool.query("SELECT count(*)::int n FROM drizzle.__drizzle_migrations")).rows[0].n,journal.entries.length);
  const players=[];
  for(let i=0;i<2;i++)players.push((await pool.query("INSERT INTO spieler(name,aktiv,portal_aktiv,is_admin) VALUES($1,true,true,$2) RETURNING id",[`Rounds ${i}`,i===0])).rows[0].id);
  await pool.query("INSERT INTO api_keys(name,key,type,active) VALUES('rounds','rounds-test-only','TERMINAL',true)");
  const product=(await pool.query("INSERT INTO products(code,name,category,is_system) VALUES('GAME_CREDIT','Spiel','GAME_CREDIT',true) RETURNING id")).rows[0].id;
  const revision=(await pool.query("INSERT INTO product_price_revisions(product_id,unit_price_cents) VALUES($1,500) RETURNING id",[product])).rows[0].id;
  for(const typ of ["GRANT","USE"])await pool.query(`INSERT INTO kredit_events(external_id,spieler_id,datum,typ,anzahl,occurred_at,price_revision_id,unit_price_cents)
   VALUES($1,$2,'2026-10-18',$3,$4,$5,$6,$7)`,[randomUUID(),players[0],typ,typ==="GRANT"?5:1,
    typ==="USE"?"2026-10-18T09:00:00Z":null,typ==="USE"?revision:null,typ==="USE"?500:null]);
  function payload(runs,ids=players,mode="NORMAL") {
   const shots=[];
   for(const player of ids)for(let run=1;run<=runs;run++)for(let clay=1;clay<=9;clay++)shots.push({
    spielerId:player,lauf:run,taube:clay,maschine:mode==="NORMAL"?"A":
     (mode==="HARAKIRI"?"ABCDEFGHH":"ABCDEFGAA")[clay-1],
    posten:((player===players[0]?0:1)+(run-1)*(mode==="NORMAL"?9:8)+(mode==="NORMAL"?clay-1:Math.min(clay-1,7)))%5+1,
    schuss1:true,schuss2:false,punkte:2,wiederholt:false});
   return {externalId:randomUUID(),datum:"2026-10-18T12:00:00.000Z",modus:mode,lauf:runs,
    taubenProLauf:9,abgeschlossen:true,confirmedLaunches:shots.length,ergebnisse:shots,
    teilnahmen:ids.map(player=>({spielerId:player,lauf:runs,punkte:runs*18,startPosten:5}))};
  }
  async function legacy(p,hidden=false,exclude=null) {
   const id=(await pool.query("INSERT INTO spiele(external_id,datum,modus,lauf,tauben_pro_lauf,abgeschlossen,confirmed_launches,hidden_at) VALUES($1,$2,$3,$4,9,true,$5,$6) RETURNING id",
    [p.externalId,p.datum,p.modus,p.lauf,p.confirmedLaunches,hidden?new Date():null])).rows[0].id;
   for(const t of p.teilnahmen)await pool.query("INSERT INTO spiel_teilnahmen(spiel_id,spieler_id,start_posten,punkte,lauf) VALUES($1,$2,$3,18,$4)",[id,t.spielerId,t.startPosten,t.lauf]);
   for(const e of p.ergebnisse)await pool.query("INSERT INTO ergebnisse(spiel_id,spieler_id,lauf,taube,maschine,posten,schuss1,schuss2,punkte,wiederholt) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10)",
    [id,e.spielerId,e.lauf,e.taube,e.maschine,e.posten,e.schuss1,e.schuss2,e.punkte,e.wiederholt]);
   if(exclude)await pool.query("INSERT INTO spiel_spieler_hidden(spiel_id,spieler_id,hidden_by) VALUES($1,$2,$3)",[id,exclude,players[0]]);
   return id;
  }
  const two=payload(2),twoId=await legacy(two);
  const one=payload(1,[players[0]]),oneId=await legacy(one);
  const hiddenId=await legacy(payload(2,[players[0]]),true);
  const excludedId=await legacy(payload(2,[players[1]]),false,players[1]);
  const {buildDailyReport}=await import(path.join(root,"artifacts/api-server/src/lib/daily-report.ts"));
  const baseline=await buildDailyReport("2026-10-18");
  const untouched=async()=>JSON.stringify((await pool.query(`SELECT
   (SELECT jsonb_agg(to_jsonb(k) ORDER BY id) FROM kredit_events k) credits,
   (SELECT jsonb_agg(to_jsonb(s) ORDER BY id) FROM sale_events s) sales,
   (SELECT jsonb_agg(to_jsonb(p) ORDER BY id) FROM bill_payments p) payments,
   (SELECT jsonb_agg(jsonb_build_array(id,confirmed_launches,hidden_at,edited_at) ORDER BY id) FROM spiele) games,
   (SELECT jsonb_agg(to_jsonb(h) ORDER BY spiel_id,spieler_id) FROM spiel_spieler_hidden h) hidden`)).rows[0]);
  const before=await untouched();
  // Simulate an upgrade from 0014 in this disposable database, not manual prod SQL.
  await pool.query("DELETE FROM drizzle.__drizzle_migrations WHERE created_at=$1",[journal.entries.at(-1).when]);
  await migrate(database.db,config);
  assert.equal(await untouched(),before);
  assert.deepEqual(await buildDailyReport("2026-10-18"),baseline);
  assert.deepEqual((await pool.query("SELECT lauf,punkte,start_posten FROM spiel_teilnahmen WHERE spiel_id=$1 AND spieler_id=$2 ORDER BY lauf",[twoId,players[0]])).rows,
   [{lauf:1,punkte:18,start_posten:1},{lauf:2,punkte:18,start_posten:5}]);
  const repairs=await pool.query("SELECT * FROM admin_game_edits WHERE action='REPAIR_ROUND_TOTALS'");
  assert.equal(repairs.rowCount,5); // both two-run players, one start-post fix, hidden game and excluded player
  assert.ok(notices.includes("ROUND_TOTAL_REPAIR games=4 players=2 player_games=5 round_rows=7"),JSON.stringify(notices));
  assert.equal((await pool.query(`SELECT count(DISTINCT spiel_id)::int n FROM admin_game_edits
   WHERE action='REPAIR_ROUND_TOTALS' AND before->'total' IS DISTINCT FROM after->'total'`)).rows[0].n,3);
  assert.equal(repairs.rows.find(r=>r.spiel_id===twoId&&r.spieler_id===players[0]).after.total,36);
  const snapshot=(await pool.query("SELECT * FROM spiel_teilnahmen ORDER BY id")).rows;
  await migrate(database.db,config);
  assert.deepEqual((await pool.query("SELECT * FROM spiel_teilnahmen ORDER BY id")).rows,snapshot);
  await pool.query("BEGIN;\n"+await readFile(path.join(config.migrationsFolder,"0015_round_participations.sql"),"utf8")+"\nCOMMIT;");
  assert.equal((await pool.query("SELECT count(*)::int n FROM admin_game_edits WHERE action='REPAIR_ROUND_TOTALS'")).rows[0].n,5);
  assert.deepEqual((await pool.query("SELECT * FROM spiel_teilnahmen ORDER BY id")).rows,snapshot);
  const {default:app}=await import(path.join(root,"artifacts/api-server/src/app.ts"));
  server=app.listen(0,"127.0.0.1");await new Promise(r=>server.once("listening",r));
  const jwt=require(path.join(root,"artifacts/api-server/node_modules/jsonwebtoken"));
  const token=jwt.sign({id:players[0]},process.env.JWT_SECRET??"trapmaster-dev-secret-change-in-prod");
  const base=`http://127.0.0.1:${server.address().port}/api`;
  async function request(route,body,extraHeaders={}) {
   const r=await fetch(base+route,{method:body?"POST":"GET",headers:{"Content-Type":"application/json",
    ...(body?{"X-Api-Key":"rounds-test-only"}:{Authorization:`Bearer ${token}`}),...extraHeaders},body:body?JSON.stringify(body):undefined});
   assert.equal(r.status,200,route);return r.json();
  }
  assert.equal((await request("/sync/spiele",{spiele:[two]})).results[0].status,"skipped");
  const modern={...two,teilnahmen:players.flatMap(player=>[1,2].map(run=>({
   spielerId:player,lauf:run,punkte:999,startPosten:6})))};
  modern.ergebnisse=[...two.ergebnisse].reverse();
  assert.equal((await request("/sync/spiele",{spiele:[modern]})).results[0].status,"skipped");
  const conflict=structuredClone(two);conflict.ergebnisse[0].schuss1=false;
  assert.equal((await request("/sync/spiele",{spiele:[conflict]})).results[0].status,"conflict");
  const card=(await request(`/admin/spiele/${twoId}`)).scorecard;
  assert.deepEqual(card.players.map(p=>p.total),[36,36]);
  assert.deepEqual(card.players[0].runTotals,[18,18]);
  assert.equal((await request(`/admin/spiele/${oneId}`)).scorecard.players[0].total,18);
  assert.equal((await request(`/admin/spiele/${hiddenId}`)).game.hidden_at!==null,true);
  assert.equal((await request(`/admin/spiele/${excludedId}`)).players[0].hidden,true);
  const rank=(await request("/rangliste?year=2026")).rangliste;
  const own=rank.find(r=>r.spielerId===players[0]);
  assert.equal(own.gesamtPunkte,54);assert.equal(own.anzahlSpiele,2);
  assert.equal(own.bestPunkte,36);assert.equal(own.bestMaximum,36);assert.equal(own.bestProzent,100);
  assert.equal(own.durchschnittProzent,100);
  const stats=await request(`/statistik/${players[0]}`);
  assert.equal(stats.gesamtSpiele,2);assert.equal(stats.bestPunkte,36);assert.equal(stats.durchschnitt,27);
  assert.equal(stats.breakdown[0].durchschnittProzent,100);
  const result=await request(`/spieler/${players[0]}/ergebnisse`);
  assert.equal(result.ergebnisse.filter(e=>e.spielId===twoId).reduce((n,e)=>n+e.punkte,0),36);
  assert.equal(result.ergebnisse.some(e=>e.spielId===hiddenId),false);
  const history=await request(`/statistik/${players[0]}/verlauf`);
  assert.deepEqual(history.verlauf.map(p=>p.punkte).sort((a,b)=>a-b),[18,36]);
  assert.ok(history.verlauf.every(p=>p.punkte===p.maxPunkte));
  for(const mode of ["NORMAL","HARAKIRI","HARAKIRI_CUSTOM","CUSTOM_1","CUSTOM_2","CUSTOM_3","CUSTOM_4"])
   for(const runs of [1,2]) {
    const p=payload(runs,[players[0]],mode);
    if(runs===1)p.teilnahmen=[]; // the ledger alone is enough to derive rows
    assert.equal((await request("/sync/spiele",{spiele:[p]})).results[0].status,"created");
    assert.equal((await request("/sync/spiele",{spiele:[p]})).results[0].status,"skipped");
    const id=(await pool.query("SELECT id FROM spiele WHERE external_id=$1",[p.externalId])).rows[0].id;
    const actual=(await request(`/admin/spiele/${id}`)).scorecard;
    assert.equal(actual.players[0].total,18*runs,mode);
    assert.deepEqual(actual.players[0].runTotals,Array(runs).fill(18),mode);
   }
  await pool.query(`INSERT INTO public_page_settings(id,enabled,name_mode,show_ranking) VALUES(1,true,'FULL',true)
   ON CONFLICT(id) DO UPDATE SET enabled=true,name_mode='FULL',show_ranking=true`);
  const created=await fetch(base+"/admin/kiosk-tokens",{method:"POST",
   headers:{"Content-Type":"application/json",Authorization:`Bearer ${token}`},body:JSON.stringify({name:"round test"})});
  assert.equal(created.status,201);
  const kiosk={"X-Kiosk-Token":(await created.json()).token};
  const last=await request("/public/last-game",undefined,kiosk);
  assert.equal(last.players[0].total,36);assert.deepEqual(last.players[0].runTotals,[18,18]);
  const print=await request(`/public/player/${players[0]}/print?tag=2026-10-18`,undefined,kiosk);
  assert.equal(print.games.length,16,"one scorecard per game, not one per Lauf");
  const liveGame=payload(2,[players[0]]);
  const live={terminalId:"ROUND-TEST-P4",seq:1,state:"RUNNING",spielExternalId:liveGame.externalId,
   modus:"NORMAL",lauf:2,taubenProLauf:9,startedAt:liveGame.datum,
   currentSpielerId:players[0],currentTaube:9,players:[{spielerId:players[0],name:"Rounds",posten:5}],
   results:liveGame.ergebnisse};
  await request("/sync/live",live);
  const published=await request("/public/live-game",undefined,kiosk);
  assert.equal(published.results.reduce((n,e)=>n+e.punkte,0),36);
 } finally {
  if(server)await new Promise(r=>server.close(r));
  if(pool)opened.add(pool);for(const p of opened)await p.end();
  await owner.query(`DROP DATABASE IF EXISTS "${name}"`);await owner.end();
 }
});
