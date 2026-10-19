import test from "node:test";
import assert from "node:assert/strict";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { randomUUID } from "node:crypto";
import {readFile} from "node:fs/promises";
import {closeIsolatedDatabase} from "./helpers/close-isolated-database.mjs";
const require=createRequire(import.meta.url),{Pool}=require("pg");
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),"../../..");
test("real PostgreSQL: kiosk, concurrent queue, groups, live isolation, privacy and audited corrections",async()=>{
 const admin=new Pool({connectionString:process.env.DATABASE_URL});
 const name=`rm_portal_spec_${randomUUID().replaceAll("-","")}`;
 await admin.query(`CREATE DATABASE "${name}"`);
 const url=new URL(process.env.DATABASE_URL);url.pathname=`/${name}`;
 process.env.DATABASE_URL=url.href;
 const opened=new Set(),originalConnect=Pool.prototype.connect;
 Pool.prototype.connect=function(...args){
  if(this!==admin){
   const database=this.options.connectionString?new URL(this.options.connectionString).pathname.slice(1):this.options.database;
   assert.equal(database,name,"Application query escaped the disposable test database");
   opened.add(this);
  }
  return originalConnect.apply(this,args);
 };
 let pool,server;
 try {
  const database=await import("@workspace/db");pool=database.pool;
  const {migrate}=await import("drizzle-orm/node-postgres/migrator");
  await migrate(database.db,{migrationsFolder:path.join(root,"lib/db/drizzle")});
  const before=(await pool.query("SELECT count(*) n FROM drizzle.__drizzle_migrations")).rows[0].n;
  await migrate(database.db,{migrationsFolder:path.join(root,"lib/db/drizzle")});
  assert.equal((await pool.query("SELECT count(*) n FROM drizzle.__drizzle_migrations")).rows[0].n,before);
   const journal=JSON.parse(await readFile(path.join(root,"lib/db/drizzle/meta/_journal.json"),"utf8"));
   assert.equal(Number(before),journal.entries.length,"Every registered migration must be applied");
  const {getTableConfig}=await import("drizzle-orm/pg-core");
  const expectedTables=["kiosk_tokens","spieler_gruppen","spieler_gruppen_mitglieder","queue_games","queue_entries",
   "queue_audit","spiel_spieler_hidden","admin_game_edits","live_game_state","spiele","public_page_settings"];
  for(const value of Object.values(database)) {
   let config;try{config=getTableConfig(value);}catch{continue;}
   if(!expectedTables.includes(config.name))continue;
   const actual=(await pool.query("SELECT column_name FROM information_schema.columns WHERE table_schema='public' AND table_name=$1",[config.name])).rows.map(r=>r.column_name).sort();
   assert.deepEqual(actual,config.columns.map(c=>c.name).sort(),`${config.name}: migration and Drizzle schema columns agree`);
  }
  assert.equal((await pool.query("SELECT count(*)::int n FROM pg_views WHERE schemaname='public' AND viewname IN ('v_spiele_visible','v_teilnahmen_visible','v_ergebnisse_visible')")).rows[0].n,3);
  const {default:app}=await import(path.join(root,"artifacts/api-server/src/app.ts"));
  const jwt=require(path.join(root,"artifacts/api-server/node_modules/jsonwebtoken"));
  const players=[];
  for(let i=0;i<18;i++) {
   const [p]=(await pool.query(`INSERT INTO spieler(id,name,email,passwort_hash,aktiv,portal_aktiv,is_admin)
    VALUES($1,$2,$3,'test-only',true,true,$4) RETURNING id`,[1900000000+i,`Spec Player ${i}`,`p${i}@spec.invalid`,i===0])).rows;players.push(p.id);
  }
  for(const player of players) {
   await pool.query(`INSERT INTO gameday_players(datum,spieler_id,added_at,terminal_id,occurred_at,external_id)
    VALUES((now() AT TIME ZONE 'Europe/Luxembourg')::date,$1,now(),'SPEC',now(),$2)`,[player,`day-${player}`]);
   await pool.query(`INSERT INTO kredit_events(external_id,spieler_id,datum,typ,anzahl)
    VALUES($1,$2,(now() AT TIME ZONE 'Europe/Luxembourg')::date,'GRANT',50)`,[`credit-${player}`,player]);
  }
  const auth=id=>`Bearer ${jwt.sign({id},process.env.JWT_SECRET??"trapmaster-dev-secret-change-in-prod")}`;
  await pool.query("UPDATE public_page_settings SET enabled=true,queue_enabled=true,groups_enabled=true,max_queue_games=40,name_mode='FULL' WHERE id=1");
  const [key]=(await pool.query("INSERT INTO api_keys(name,key,type,active) VALUES('spec','queue-test-key','TERMINAL',true) RETURNING id")).rows;
  server=app.listen(0,"127.0.0.1");await new Promise(r=>server.once("listening",r));
  const base=`http://127.0.0.1:${server.address().port}/api`;
  let kiosk="";
  const request=async(route,{method="GET",body,admin=false,user,terminal=false,noCookie=false}={})=>{
   const response=await fetch(base+route,{method,headers:{"Content-Type":"application/json",
    ...(admin?{Authorization:auth(players[0])}:user?{Authorization:auth(user)}:{}),
    ...(terminal?{"X-Api-Key":"queue-test-key"}:{}),
    ...(!noCookie&&kiosk?{Cookie:`rm_kiosk=${kiosk}`}:{})},body:body===undefined?undefined:JSON.stringify(body)});
   const text=await response.text();let data;try{data=JSON.parse(text);}catch{data=text;}
    return {status:response.status,data,headers:response.headers};
  };
   assert.equal((await request("/public/queue",{noCookie:true})).status,401,"enabled page without a PIN remains locked");
   await pool.query("DELETE FROM public_page_settings");
   const firstSettings=await request("/admin/public-page",{method:"PUT",admin:true,body:{enabled:true,nameMode:"FULL",queueEnabled:true,groupsEnabled:true,showRanking:true,maxQueueGames:40,enabledModes:["NORMAL"]}});
   assert.equal(firstSettings.status,200);assert.equal(firstSettings.data.showRanking,true);
   assert.equal((await pool.query("SELECT show_ranking FROM public_page_settings WHERE id=1")).rows[0].show_ranking,true,"first INSERT saves every new setting, not only updates");
  assert.equal((await request("/admin/public-page/pin",{method:"PUT",admin:true,body:{pin:"628491"}})).status,200);
  const created=await request("/public/unlock",{method:"POST",body:{pin:"628491"}});
  assert.equal(created.status,200);kiosk=created.headers.get("set-cookie").split(";")[0].slice("rm_kiosk=".length);assert.equal(kiosk.length,43);
  assert.ok(!(JSON.stringify((await request("/admin/public-page/devices",{admin:true})).data).includes(kiosk)));
   for(const route of ["/public/last-game","/public/queue","/public/groups","/public/queue/players","/public/live-game","/public/ranking",`/public/player/${players[1]}/days`,`/public/player/${players[1]}/print?tag=2026-10-18`])
   assert.equal((await request(route,{noCookie:true})).status,401,route);
  assert.equal((await request("/admin/spiele",{user:players[1]})).status,403);
  // Real terminal day events are the only source of public queue eligibility.
   const originalKiosk=kiosk;
   const dayDevice=await request("/public/unlock",{method:"POST",noCookie:true,body:{pin:"628491"}});
   assert.equal(dayDevice.status,200);kiosk=dayDevice.headers.get("set-cookie").split(";")[0].slice("rm_kiosk=".length);
  const addedId=1700999001,emptyId=1700999002,missingId=1700999003;
  for(const [id,label] of [[addedId,"Terminal Full Name"],[emptyId,"Registered Empty Credits"],[missingId,"Not Registered Today"]])
   await pool.query("INSERT INTO spieler(id,name,email,passwort_hash,aktiv,portal_aktiv) VALUES($1,$2,$3,'test-only',true,true)",[id,label,`${id}@spec.invalid`]);
  const day=(await pool.query("SELECT (now() AT TIME ZONE 'Europe/Luxembourg')::date::text AS today")).rows[0].today;
  const event=(spielerId,action,offset=0)=>({externalId:randomUUID(),terminalId:"ESP32-P4-001122334455",datum:day,spielerId,action,occurredAt:new Date(Date.now()+offset).toISOString()});
  assert.equal((await request("/public/queue/signup",{method:"POST",body:{spielerId:missingId,modus:"NORMAL"}})).status,422);
  const add=event(addedId,"ADD"),zero=event(emptyId,"ADD");
  const firstDay=await request("/sync/gameday-players",{method:"POST",terminal:true,body:{events:[add,zero]}});
  assert.equal(firstDay.status,200);assert.deepEqual(firstDay.data.results.map(r=>r.status),["accepted","accepted"]);
  await pool.query("INSERT INTO kredit_events(external_id,spieler_id,datum,typ,anzahl) VALUES($1,$2,$3,'GRANT',2)",[randomUUID(),addedId,day]);
  const eligiblePicker=(await request("/public/queue/players")).data.players;
  assert.ok(!eligiblePicker.some(p=>p.id===missingId));
  assert.equal(eligiblePicker.find(p=>p.id===addedId).displayName,"Terminal Full Name");
   assert.ok(eligiblePicker.every(p=>!("available" in p)&&!("credits" in p)),"public queue exposes eligibility, never credit balances");
  assert.equal(eligiblePicker.find(p=>p.id===emptyId).canSignup,false);
  assert.equal(eligiblePicker.find(p=>p.id===emptyId).eligibilityHint,"Credits am Stand buchen");
  assert.equal((await request("/public/queue/signup",{method:"POST",body:{spielerId:addedId,modus:"NORMAL"}})).status,201);
  const remove=event(addedId,"REMOVE",5);
  assert.equal((await request("/sync/gameday-players",{method:"POST",terminal:true,body:{events:[remove]}})).status,200);
  assert.equal((await pool.query("SELECT status FROM queue_entries WHERE spieler_id=$1 ORDER BY id DESC LIMIT 1",[addedId])).rows[0].status,"CANCELLED");
  const replayDay=await request("/sync/gameday-players",{method:"POST",terminal:true,body:{events:[add,zero,remove]}});
  assert.deepEqual(replayDay.data.results.map(r=>r.status),["skipped","skipped","skipped"]);
  assert.ok(!(await request("/public/queue/players")).data.players.some(p=>p.id===addedId),"replayed old ADD cannot undo newer REMOVE");
  const conflictDay=await request("/sync/gameday-players",{method:"POST",terminal:true,body:{events:[{...add,action:"REMOVE"}]}});
  assert.equal(conflictDay.status,409);
   const rolledBack=event(missingId,"ADD");
   assert.equal((await request("/sync/gameday-players",{method:"POST",terminal:true,body:{events:[rolledBack,{...add,action:"REMOVE"}]}})).status,409);
   assert.equal((await pool.query("SELECT count(*)::int n FROM gameday_player_events WHERE external_id=$1",[rolledBack.externalId])).rows[0].n,0,"conflict rolls back earlier events in the same batch");
   kiosk=originalKiosk; // Do not consume the concurrency scenario's device rate budget.
  const races=await Promise.all([1,2].map(()=>request("/public/queue/signup",{method:"POST",body:{spielerId:players[1],modus:"NORMAL"}})));
  assert.deepEqual(races.map(r=>r.status).sort(),[201,409]);
  let queue=(await request("/public/queue")).data;assert.equal(queue.games[0].players.length,1);
  const original=queue.games[0],entryIds=original.players.map(p=>p.entryId);
  assert.equal((await request("/sync/queue/take",{method:"POST",terminal:true,body:{queueGameId:original.queueGameId,terminalId:"SPEC-A",entryIds:[9999]}})).status,409);
  const take={queueGameId:original.queueGameId,terminalId:"SPEC-A",entryIds};
  for(let i=0;i<2;i++)assert.equal((await request("/sync/queue/take",{method:"POST",terminal:true,body:take})).status,200);
   assert.equal((await request("/sync/queue",{terminal:true})).data.games.length,0,"other terminals only see OPEN games");
   assert.equal((await request("/sync/queue?terminalId=SPEC-A",{terminal:true})).data.games[0].status,"LOCKED","owner can recover its own reservation");
  assert.equal((await request("/public/queue/cancel",{method:"POST",body:{entryId:entryIds[0]}})).status,409);
  assert.equal((await request("/sync/queue/release",{method:"POST",terminal:true,body:{...take,terminalId:"SPEC-B"}})).status,403);
  assert.equal((await request("/sync/queue/release",{method:"POST",terminal:true,body:take})).status,200);
   assert.equal((await request("/sync/queue/release",{method:"POST",terminal:true,body:take})).status,200,"lost release ACK is safe to replay");
  const groupA=(await request("/public/groups",{method:"POST",body:{name:"A",spielerIds:players.slice(2,6)}})).data.id;
  const groupB=(await request("/public/groups",{method:"POST",body:{name:"B",spielerIds:players.slice(6,10)}})).data.id;
  assert.ok(groupA&&groupB);
  for(const r of await Promise.all([groupA,groupB].map(g=>request(`/public/groups/${g}/signup`,{method:"POST",body:{modus:"NORMAL"}}))))assert.equal(r.status,200);
  queue=(await request("/public/queue")).data;assert.ok(queue.games.every(g=>g.players.length<=6));
  for(const group of [groupA,groupB])assert.equal(queue.games.filter(g=>g.players.some(p=>p.groupId===group)).length,1);
  await request("/public/queue/signup",{method:"POST",body:{spielerId:players[10],modus:"NORMAL"}});
  queue=(await request("/public/queue")).data;assert.equal(queue.games[0].players.length,6,"single overtakes into first free game");
  // Start removes card immediately, yet running entries still block signup.
  const game=queue.games[0],command={queueGameId:game.queueGameId,terminalId:"SPEC-A",entryIds:game.players.map(p=>p.entryId)};
  assert.equal((await request("/sync/queue/take",{method:"POST",terminal:true,body:command})).status,200);
  assert.equal((await request("/sync/queue/started",{method:"POST",terminal:true,body:{...command,spielExternalId:"11111111-1111-4111-8111-111111111111"}})).status,200);
  assert.ok(!(await request("/public/queue")).data.games.some(g=>g.queueGameId===game.queueGameId));
  assert.equal((await request("/public/queue/signup",{method:"POST",body:{spielerId:players[1],modus:"NORMAL"}})).status,409);
  const live={terminalId:"SPEC-A",seq:2,state:"RUNNING",spielExternalId:"11111111-1111-4111-8111-111111111111",modus:"NORMAL",lauf:1,taubenProLauf:1,
   startedAt:"2026-10-18T10:00:00.000Z",currentSpielerId:players[1],currentTaube:1,players:[{spielerId:players[1],name:"Spec Player 1",posten:1}],results:[]};
  assert.equal((await request("/sync/live",{method:"POST",terminal:true,body:live})).data.accepted,true);
  assert.equal((await request("/sync/live",{method:"POST",terminal:true,body:{...live,seq:1}})).data.accepted,false);
  assert.equal((await pool.query("SELECT count(*)::int n FROM spiele")).rows[0].n,0);
  assert.equal((await request("/rangliste?jahr=2026")).data.rangliste.length,0);
  assert.equal((await request(`/statistik/${players[1]}`,{user:players[1]})).data.gesamtSpiele,0);
  const {buildDailyReport}=await import(path.join(root,"artifacts/api-server/src/lib/daily-report.ts"));
  const emptyReport=await buildDailyReport("2026-10-18");assert.equal(emptyReport.games,0);
  assert.equal((await request("/public/live-game")).data.seq,2);
  await pool.query("UPDATE live_game_state SET updated_at=now()-interval '121 seconds'");
  assert.equal((await request("/public/live-game")).data,null);
  await request("/sync/live",{method:"POST",terminal:true,body:{...live,seq:3,state:"FINISHED"}});
  assert.equal((await request("/public/live-game")).data,null);
  const gamePayload={externalId:"11111111-1111-4111-8111-111111111111",datum:"2026-10-18T10:00:00.000Z",modus:"NORMAL",lauf:1,taubenProLauf:1,abgeschlossen:true,confirmedLaunches:4,
   teilnahmen:players.slice(1,5).map((id,i)=>({spielerId:id,startPosten:i+1,punkte:2,lauf:1})),
   ergebnisse:players.slice(1,5).map((id,i)=>({spielerId:id,lauf:1,taube:1,maschine:"A",posten:i+1,schuss1:true,schuss2:false,punkte:2,wiederholt:false}))};
  const synced=await request("/sync/spiele",{method:"POST",terminal:true,body:{spiele:[gamePayload]}});
  assert.equal(synced.data.results[0].status,"created",JSON.stringify(synced.data));
  assert.equal((await pool.query("SELECT count(*)::int n FROM live_game_state")).rows[0].n,0);
  assert.equal((await pool.query("SELECT status FROM queue_entries WHERE spieler_id=$1",[players[1]])).rows[0].status,"DONE");
  const gameId=(await pool.query("SELECT id FROM spiele WHERE external_id='11111111-1111-4111-8111-111111111111'")).rows[0].id;
  const own=await request(`/spiele/${gameId}/scorecard`,{user:players[1]});
  assert.equal(own.status,200);assert.equal(own.data.players[0].name,"Spec Player 1");
  for(const forbidden of ["Spec Player 2","Spec Player 3","Spec Player 4"])assert.ok(!JSON.stringify(own.data).includes(forbidden));
   for(const id of players.slice(2,5))assert.ok(!JSON.stringify(own.data).includes(String(id)),"other player identifiers must not leak");
  assert.deepEqual(own.data.players.slice(1).map(p=>p.name),["Spieler 2","Spieler 3","Spieler 4"]);
  const print=await request(`/public/player/${players[1]}/print?tag=2026-10-18`);
  assert.deepEqual(print.data.stats,await (await import(path.join(root,"artifacts/api-server/src/lib/player-stats.ts"))).computePlayerStats(players[1],{from:"2026-10-18",to:"2026-10-18"}));
  const baseline=await buildDailyReport("2026-10-18");
  const hidden=await request(`/admin/spiele/${gameId}/spieler/${players[1]}/hide`,{method:"POST",admin:true});assert.equal(hidden.status,200);
  assert.equal((await request(`/statistik/${players[1]}`,{user:players[1]})).data.gesamtSpiele,0);
   assert.equal((await request(`/spiele/${gameId}/scorecard`,{user:players[1]})).status,404,"hidden participation is not an own-results card");
  assert.equal((await request(`/spiele/${gameId}/scorecard`,{user:players[2]})).data.players.length,3);
  assert.deepEqual(await buildDailyReport("2026-10-18"),baseline);
  await request(`/admin/spiele/${gameId}/spieler/${players[1]}/unhide`,{method:"POST",admin:true});
  const shot=(await pool.query("SELECT id FROM ergebnisse WHERE spiel_id=$1 AND spieler_id=$2",[gameId,players[1]])).rows[0].id;
  assert.equal((await request(`/admin/spiele/${gameId}/ergebnisse/${shot}`,{method:"PATCH",admin:true,body:{schuss1:false,schuss2:true}})).data.punkte,1);
  assert.deepEqual(await buildDailyReport("2026-10-18"),baseline,"edit leaves every operational report field and hash unchanged");
  const replay=await request("/sync/spiele",{method:"POST",terminal:true,body:{spiele:[gamePayload]}});
  assert.equal(replay.data.results[0].status,"skipped");assert.equal(replay.data.results[0].reason,"EDITED_BY_ADMIN");
  assert.equal((await pool.query("SELECT punkte FROM ergebnisse WHERE id=$1",[shot])).rows[0].punkte,1);
  await request(`/admin/spiele/${gameId}/hide`,{method:"POST",admin:true});
  assert.equal((await request("/public/last-game")).data,null);
  assert.deepEqual(await buildDailyReport("2026-10-18"),baseline);
  await request(`/admin/spiele/${gameId}/unhide`,{method:"POST",admin:true});
   // Exactly three complete fixture games and four players: compare the visible
   // ranking with an independently filtered fixture, while operational output is identical.
   const extraGames=[1,2].map(i=>({...gamePayload,externalId:randomUUID(),datum:`2026-10-18T1${i}:00:00.000Z`}));
   for(const payload of extraGames)assert.equal((await request("/sync/spiele",{method:"POST",terminal:true,body:{spiele:[payload]}})).data.results[0].status,"created");
   const fixtureGames=(await pool.query('SELECT id,modus,tauben_pro_lauf "taubenProLauf",lauf,datum FROM spiele ORDER BY id')).rows;
   const fixtureParts=(await pool.query('SELECT spiel_id "spielId",spieler_id "spielerId",punkte FROM spiel_teilnahmen')).rows;
   const fixturePlayers=players.slice(1,5).map((id,i)=>({id,name:`Spec Player ${i+1}`}));
   const {computeRangliste}=await import(path.join(root,"artifacts/api-server/src/routes/rangliste-compute.ts"));
   const secondId=fixtureGames[1].id,operational3=await buildDailyReport("2026-10-18");
   const originalRanking=(await request("/rangliste?jahr=2026")).data.rangliste;
   await request(`/admin/spiele/${secondId}/spieler/${players[1]}/hide`,{method:"POST",admin:true});
   const visibleParts=fixtureParts.filter(p=>!(p.spielId===secondId&&p.spielerId===players[1]));
   assert.deepEqual((await request("/rangliste?jahr=2026")).data.rangliste,computeRangliste(fixtureGames,visibleParts,fixturePlayers));
   assert.equal((await request(`/statistik/${players[1]}`,{user:players[1]})).data.gesamtSpiele,2);
   assert.equal((await request(`/spiele/${secondId}/scorecard`,{user:players[2]})).data.players.length,3);
   assert.equal((await request(`/spiele/${secondId}/scorecard`,{admin:true})).data.players.length,4,"admin keeps full names and hidden rows");
   await request(`/admin/spiele/${gameId}/hide`,{method:"POST",admin:true});
   assert.deepEqual((await request("/rangliste?jahr=2026")).data.rangliste,computeRangliste(fixtureGames.filter(g=>g.id!==gameId),visibleParts.filter(p=>p.spielId!==gameId),fixturePlayers));
   assert.equal((await request(`/statistik/${players[1]}`,{user:players[1]})).data.gesamtSpiele,1);
   assert.deepEqual(await buildDailyReport("2026-10-18"),operational3);
   await request(`/admin/spiele/${gameId}/unhide`,{method:"POST",admin:true});
   await request(`/admin/spiele/${secondId}/spieler/${players[1]}/unhide`,{method:"POST",admin:true});
   assert.deepEqual((await request("/rangliste?jahr=2026")).data.rangliste,originalRanking);
   assert.equal((await request("/sync/live",{method:"POST",terminal:true,body:{...live,seq:999}})).data.accepted,false,"late live packet cannot resurrect a completed game");
   const runningOther=(await request("/public/queue")).data.games.find(g=>g.players.some(p=>p.groupId===groupB));
   assert.ok(runningOther,JSON.stringify((await pool.query(`SELECT e.status,e.cancel_reason,e.group_id,g.status game_status,
    v.registered,v.available FROM queue_entries e JOIN queue_games g ON g.id=e.queue_game_id
    LEFT JOIN v_queue_eligibility v ON v.id=e.spieler_id WHERE e.group_id=$1`,[groupB])).rows));
   const lateCommand={queueGameId:runningOther.queueGameId,terminalId:"SPEC-B",entryIds:runningOther.players.map(p=>p.entryId)};
   assert.equal((await request("/sync/queue/take",{method:"POST",terminal:true,body:lateCommand})).status,200);
   const lateGame={...gamePayload,externalId:randomUUID(),
    teilnahmen:players.slice(6,10).map((id,i)=>({...gamePayload.teilnahmen[i],spielerId:id})),
    ergebnisse:players.slice(6,10).map((id,i)=>({...gamePayload.ergebnisse[i],spielerId:id}))};
   assert.equal((await request("/sync/spiele",{method:"POST",terminal:true,body:{spiele:[lateGame]}})).data.results[0].status,"created");
   assert.equal((await pool.query("SELECT status FROM queue_entries WHERE spieler_id=$1",[players[6]])).rows[0].status,"TAKEN");
   for(let i=0;i<2;i++)assert.equal((await request("/sync/queue/started",{method:"POST",terminal:true,body:{...lateCommand,spielExternalId:lateGame.externalId}})).status,200);
   assert.equal((await pool.query("SELECT status FROM queue_entries WHERE spieler_id=$1",[players[6]])).rows[0].status,"DONE","game sync arriving before outbox STARTED is reconciled");
   for(let i=0;i<5;i++)assert.equal((await request(`/public/player/${players[1]}/print?tag=2026-10-18`)).status,200);
   assert.equal((await request(`/public/player/${players[1]}/print?tag=2026-10-18`)).status,429);
  assert.equal((await request("/rangliste?jahr=2026&limit=1")).data.rangliste.length,1);
   const reset=await request("/admin/public-page/devices/reset",{method:"POST",admin:true});
   assert.equal(reset.status,200);
   assert.equal((await request("/public/queue")).status,401,"reset invalidates old device");
   const renewal=await request("/public/unlock",{method:"POST",body:{pin:"628491"}});
   assert.equal(renewal.status,200);
   kiosk=renewal.headers.get("set-cookie").split(";")[0].slice("rm_kiosk=".length);
   await pool.query("UPDATE public_page_settings SET name_mode='INITIAL' WHERE id=1");
   const broadPicker=(await request("/public/queue/players?q=Spec")).data.players;
   const narrowPicker=(await request("/public/queue/players?q=Player%2017")).data.players;
   assert.equal(narrowPicker.length,1);
   assert.equal(narrowPicker[0].displayName,broadPicker.find(p=>p.id===players[17]).displayName,"initial-name disambiguation stays stable when search narrows");
   const devices=(await request("/admin/public-page/devices",{admin:true})).data.devices;
   assert.equal(devices.length,1);assert.ok(devices[0].lastUsedAt);
   const noStore=await request("/public/queue");assert.equal(noStore.headers.get("cache-control"),"no-store");
   assert.equal(noStore.headers.get("access-control-allow-origin"),null);
   let limited=false;for(let i=0;i<61;i++)if((await request("/public/queue")).status===429){limited=true;break;}
   assert.equal(limited,true);
   await request(`/admin/public-page/devices/${devices[0].id}`,{method:"DELETE",admin:true});
  assert.ok([401,404].includes((await request("/public/queue")).status));
 } finally {
  if(server){server.closeAllConnections?.();await new Promise(r=>server.close(r));}
  if(pool)opened.add(pool);
  try{await closeIsolatedDatabase(admin,name,opened);}
  finally{Pool.prototype.connect=originalConnect;await admin.end();}
 }
});
