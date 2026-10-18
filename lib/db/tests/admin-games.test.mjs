import test from "node:test";
import assert from "node:assert/strict";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";

const require=createRequire(import.meta.url),{Pool}=require("pg");
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),"../../..");

test("admin games: migrations, filters, visibility, audited corrections and operational isolation",async()=>{
 const owner=new Pool({connectionString:process.env.DATABASE_URL});
 const name=`rm_admin_games_${randomUUID().replaceAll("-","")}`;
 await owner.query(`CREATE DATABASE "${name}"`);
 const url=new URL(process.env.DATABASE_URL);url.pathname=`/${name}`;process.env.DATABASE_URL=url.href;
 // tsx can load ESM and CommonJS copies of the workspace DB module. Close
 // every test-owned pool before DROP, not just the first exported instance:
 // otherwise FORCE terminates an idle duplicate and raises a late test error.
 const pg=require("pg"),OriginalPool=pg.Pool,opened=new Set();
 pg.Pool=class extends OriginalPool {
  constructor(...args){super(...args);opened.add(this);}
 };
 let pool,server;
 try {
  const database=await import("@workspace/db");pool=database.pool;
  const {migrate}=await import("drizzle-orm/node-postgres/migrator");
  await migrate(database.db,{migrationsFolder:path.join(root,"lib/db/drizzle")});
  const journal=JSON.parse(await readFile(path.join(root,"lib/db/drizzle/meta/_journal.json"),"utf8"));
  const applied=await pool.query("SELECT hash,created_at FROM drizzle.__drizzle_migrations ORDER BY created_at");
  assert.equal(applied.rows.length,journal.entries.length);
  assert.ok(applied.rows.some(r=>Number(r.created_at)===journal.entries.find(e=>e.idx===12).when));
  await migrate(database.db,{migrationsFolder:path.join(root,"lib/db/drizzle")});
  assert.deepEqual((await pool.query("SELECT hash,created_at FROM drizzle.__drizzle_migrations ORDER BY created_at")).rows,applied.rows);
  assert.equal((await pool.query("SELECT count(*)::int n FROM pg_views WHERE schemaname='public' AND viewname IN('v_spiele_visible','v_teilnahmen_visible','v_ergebnisse_visible')")).rows[0].n,3);
  const players=[];
  for(let i=0;i<4;i++)players.push((await pool.query(
   "INSERT INTO spieler(name,email,aktiv,portal_aktiv,is_admin) VALUES($1,$2,true,true,$3) RETURNING id",
   [`Admin Fixture ${i}`,`fixture${i}@admin-test.invalid`,i===0])).rows[0].id);
  await pool.query("INSERT INTO api_keys(name,key,type,active) VALUES('fixture','admin-games-test-only','TERMINAL',true)");
  await pool.query("UPDATE public_page_settings SET enabled=true,name_mode='FULL' WHERE id=1");
  const {default:app}=await import(path.join(root,"artifacts/api-server/src/app.ts"));
  const jwt=require(path.join(root,"artifacts/api-server/node_modules/jsonwebtoken"));
  server=app.listen(0,"127.0.0.1");await new Promise(r=>server.once("listening",r));
  const base=`http://127.0.0.1:${server.address().port}/api`;
  let kiosk="";
  async function request(route,{method="GET",body,user=players[0],terminal=false}={}) {
   const r=await fetch(base+route,{method,headers:{"Content-Type":"application/json",
    ...(terminal?{"X-Api-Key":"admin-games-test-only"}:{Authorization:`Bearer ${jwt.sign({id:user},process.env.JWT_SECRET??"trapmaster-dev-secret-change-in-prod")}`}),
    ...(kiosk?{Cookie:kiosk}:{})},body:body===undefined?undefined:JSON.stringify(body)});
   const data=await r.json();return {status:r.status,data};
  }
  await request("/admin/public-page/pin",{method:"PUT",body:{pin:"628491"}});
  const unlocked=await fetch(base+"/public/unlock",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({pin:"628491"})});
  kiosk=unlocked.headers.get("set-cookie")?.split(";")[0];
  assert.ok(kiosk);
  const days=["2026-10-16","2026-10-17","2026-10-18"],games=[],payloads=[];
  for(let i=0;i<3;i++) {
   const payload={externalId:randomUUID(),datum:`${days[i]}T10:00:00.000Z`,modus:i===2?"HARAKIRI":"NORMAL",
    lauf:1,taubenProLauf:1,abgeschlossen:true,confirmedLaunches:4,
    teilnahmen:players.map((id,j)=>({spielerId:id,startPosten:j+1,punkte:2,lauf:1})),
    ergebnisse:players.map((id,j)=>({spielerId:id,lauf:1,taube:1,maschine:"A",posten:j+1,schuss1:true,schuss2:false,punkte:2,wiederholt:false}))};
   const imported=await request("/sync/spiele",{method:"POST",terminal:true,body:{spiele:[payload]}});
   assert.equal(imported.data.results?.[0]?.status,"created",JSON.stringify(imported));
   games.push((await pool.query("SELECT id FROM spiele WHERE external_id=$1",[payload.externalId])).rows[0].id);payloads.push(payload);
  }
  const {buildDailyReport}=await import(path.join(root,"artifacts/api-server/src/lib/daily-report.ts"));
  const operational=async()=>{
   const tables=(await pool.query("SELECT tablename FROM pg_tables WHERE schemaname='public' AND tablename ~ '(kredit|credit|bill_|verkauf|sales|inventory|gameday_|daily_report)' ORDER BY tablename")).rows;
   const ledgers={};
   for(const {tablename:t} of tables) {
    assert.match(t,/^[a-z_]+$/);
    ledgers[t]=(await pool.query(`SELECT coalesce(jsonb_agg(to_jsonb(t) ORDER BY to_jsonb(t)::text),'[]'::jsonb) rows FROM "${t}" t`)).rows[0].rows;
   }
   return {reports:await Promise.all(days.map(day=>buildDailyReport(day))),ledgers,
    counters:(await pool.query("SELECT count(*)::int games,sum(confirmed_launches)::int launches,sum(tauben_pro_lauf*lauf)::int clay_units FROM spiele")).rows};
  };
  const baseline=await operational(),initialRanking=(await request("/rangliste?jahr=2026")).data;
  const rankingGames=(await pool.query('SELECT id,modus,tauben_pro_lauf "taubenProLauf",lauf,datum FROM spiele')).rows;
  const rankingParts=(await pool.query('SELECT spiel_id "spielId",spieler_id "spielerId",punkte FROM spiel_teilnahmen')).rows;
  const rankingPlayers=(await pool.query("SELECT id,name FROM spieler")).rows;
  const {computeRangliste}=await import(path.join(root,"artifacts/api-server/src/routes/rangliste-compute.ts"));
  assert.equal((await request("/admin/spiele")).data.total,3);
  assert.equal((await request("/admin/spiele?from=2026-10-17&to=2026-10-17")).data.items[0].id,games[1]);
  assert.equal((await request("/admin/spiele?modus=HARAKIRI")).data.total,1);
  assert.equal((await request(`/admin/spiele?spielerId=${players[1]}`)).data.total,3);
  assert.equal((await request("/admin/spiele?q=Admin%20Fixture%201")).data.total,3);
  assert.equal((await request("/admin/spiele?hidden=yes")).data.total,0);
  const mutate=async(route,options={})=>{const result=await request(route,{method:"POST",...options});assert.equal(result.status,200,JSON.stringify(result));assert.deepEqual(await operational(),baseline);return result;};
  const game=games[1],player=players[1];
  await mutate(`/admin/spiele/${game}/spieler/${player}/hide`);
  assert.deepEqual((await request("/rangliste?jahr=2026")).data.rangliste,
   computeRangliste(rankingGames,rankingParts.filter(p=>p.spielId!==game||p.spielerId!==player),rankingPlayers));
  assert.equal((await request(`/statistik/${player}`)).data.gesamtSpiele,2);
  assert.equal((await request(`/statistik/${player}/verlauf`)).data.verlauf.length,2);
  const visibleResults=(await request(`/spieler/${player}/ergebnisse`)).data.ergebnisse;
  assert.equal(visibleResults.length,2);
  assert.ok(visibleResults.every(e=>e.spiel.datum!==payloads[1].datum));
  assert.equal((await request(`/spiele/${game}/scorecard`,{user:players[2]})).data.players.length,3);
  assert.equal((await request(`/public/player/${players[2]}/print?tag=${days[1]}`)).data.games[0].players.length,3);
  assert.equal((await request(`/admin/spiele/${game}`)).data.players.length,4);
  assert.equal((await request(`/admin/spiele/${game}`)).data.players.find(p=>p.id===player).hidden,true);
  await mutate(`/admin/spiele/${game}/spieler/${player}/unhide`);
  assert.deepEqual((await request("/rangliste?jahr=2026")).data,initialRanking);
  await mutate(`/admin/spiele/${games[2]}/hide`);
  assert.deepEqual((await request("/rangliste?jahr=2026")).data.rangliste,
   computeRangliste(rankingGames.filter(g=>g.id!==games[2]),rankingParts.filter(p=>p.spielId!==games[2]),rankingPlayers));
  assert.equal((await request("/admin/spiele?hidden=yes")).data.items[0].id,games[2]);
  assert.equal((await request("/admin/spiele?hidden=no")).data.total,2);
  assert.equal((await request("/public/last-game")).data.date,payloads[1].datum);
  await mutate(`/admin/spiele/${games[2]}/unhide`);
  assert.equal((await request("/public/last-game")).data.date,payloads[2].datum);
  // A game with no visible participants must disappear from last-game too.
  for(const p of players)await mutate(`/admin/spiele/${games[2]}/spieler/${p}/hide`);
  assert.equal((await request("/public/last-game")).data.date,payloads[1].datum);
  for(const p of players)await mutate(`/admin/spiele/${games[2]}/spieler/${p}/unhide`);
  await mutate(`/admin/spiele/${games[0]}/hide`);
  const hiddenReplay=await request("/sync/spiele",{method:"POST",terminal:true,body:{spiele:[payloads[0]]}});
  assert.notEqual(hiddenReplay.data.results[0].status,"created");
  assert.ok((await pool.query("SELECT hidden_at FROM spiele WHERE id=$1",[games[0]])).rows[0].hidden_at);
  await mutate(`/admin/spiele/${games[0]}/unhide`);
  const detail=(await request(`/admin/spiele/${game}`)).data,shot=detail.shots.find(s=>s.spieler_id===player);
  const edited=await mutate(`/admin/spiele/${game}/ergebnisse/${shot.id}`,{method:"PATCH",body:{schuss1:false,schuss2:true}});
  assert.equal(edited.data.punkte,1);
  assert.deepEqual((await request("/rangliste?jahr=2026")).data.rangliste,
   computeRangliste(rankingGames,rankingParts.map(p=>p.spielId===game&&p.spielerId===player?{...p,punkte:1}:p),rankingPlayers));
  assert.equal((await pool.query("SELECT punkte FROM spiel_teilnahmen WHERE spiel_id=$1 AND spieler_id=$2",[game,player])).rows[0].punkte,1);
  const replay=await request("/sync/spiele",{method:"POST",terminal:true,body:{spiele:[payloads[1]]}});
  assert.equal(replay.data.results[0].reason,"EDITED_BY_ADMIN");assert.equal(replay.data.results[0].status,"skipped");
  assert.deepEqual(await operational(),baseline);
  const audited=(await request(`/admin/spiele/${game}`)).data;
  assert.ok(audited.audit.some(a=>a.action==="EDIT_RESULT"&&a.before.punkte===2&&a.after.punkte===1));
  for(const [route,method,body] of [
   ["/admin/spiele","GET"],[`/admin/spiele/${game}`,"GET"],
   ...["hide","unhide"].flatMap(action=>[[`/admin/spiele/${game}/${action}`,"POST"],[`/admin/spiele/${game}/spieler/${player}/${action}`,"POST"]]),
   [`/admin/spiele/${game}/ergebnisse/${shot.id}`,"PATCH",{schuss1:true,schuss2:false}],
  ])assert.equal((await request(route,{method,body,user:players[2]})).status,403);
  console.log("Verified all migration entries incl. 0012 + idempotency; admin filters/actions/audit; display exclusion; operational reports/hashes, ledgers, inventory and counters unchanged; all admin routes reject non-admin.");
 } finally {
  if(server)await new Promise(resolve=>server.close(resolve));
  await Promise.all([...opened].map(p=>p.end()));
  pg.Pool=OriginalPool;
  await owner.query(`DROP DATABASE "${name}" WITH (FORCE)`);await owner.end();
 }
});
