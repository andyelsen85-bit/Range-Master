import test from "node:test";
import assert from "node:assert/strict";
import {createRequire} from "node:module";
import {randomUUID} from "node:crypto";
import path from "node:path";
import {fileURLToPath} from "node:url";
const require=createRequire(import.meta.url),pg=require("pg"),OriginalPool=pg.Pool;
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),"../../..");

test("owner scorecards and SQL annual reports preserve privacy, visibility and Luxembourg boundaries",async()=>{
 const owner=new OriginalPool({connectionString:process.env.DATABASE_URL});
 const name=`rm_reports_${randomUUID().replaceAll("-","")}`,opened=new Set();
 await owner.query(`CREATE DATABASE "${name}"`);
 const url=new URL(process.env.DATABASE_URL);url.pathname=`/${name}`;process.env.DATABASE_URL=url.href;
 // Isolated fixture signing key, never a workspace credential.
 process.env.JWT_SECRET="reports-test-fixture-signing-only";
 pg.Pool=class extends OriginalPool{constructor(...args){super(...args);opened.add(this);}};
 let pool,server;
 try {
  const database=await import("@workspace/db");pool=database.pool;
  const {migrate}=await import("drizzle-orm/node-postgres/migrator");
  await migrate(database.db,{migrationsFolder:path.join(root,"lib/db/drizzle")});
  const jwt=createRequire(path.join(root,"artifacts/api-server/package.json"))("jsonwebtoken");
  const players=[];
  for(let i=0;i<6;i++) {
   const {rows}=await pool.query("INSERT INTO spieler(name,aktiv,portal_aktiv,is_admin) VALUES($1,true,true,$2) RETURNING id",
     [i===0?"Report Owner":`Private Other ${i}`,i===5]);players.push(rows[0].id);
  }
  const createGame=async(date,hidden=false)=>{
   const {rows}=await pool.query("INSERT INTO spiele(datum,modus,lauf,tauben_pro_lauf,abgeschlossen,hidden_at) VALUES($1,'NORMAL',2,9,true,$2) RETURNING id",
    [date,hidden?new Date():null]);const game=rows[0].id;
   for(const player of players)for(let run=1;run<=2;run++){
    await pool.query("INSERT INTO spiel_teilnahmen(spiel_id,spieler_id,start_posten,punkte,lauf) VALUES($1,$2,1,9,$3)",[game,player,run]);
    await pool.query(`INSERT INTO ergebnisse(spiel_id,spieler_id,lauf,taube,maschine,posten,schuss1,schuss2,punkte,wiederholt)
      SELECT $1,$2,$3,n,CASE WHEN n>=8 THEN 'H' ELSE chr(64+n) END::maschine,
        CASE WHEN n>=8 THEN 6 ELSE n END,n%3>0,false,n%3,false FROM generate_series(1,9) n`,[game,player,run]);
   }return game;
  };
  const game=await createGame("2025-12-31T23:30:00Z"); // Luxembourg 2026-01-01
  await createGame("2026-01-31T23:30:00Z"); // Luxembourg February, not January
  await createGame("2026-03-15T11:00:00Z");
  await createGame("2026-12-31T23:30:00Z"); // Luxembourg 2027: excluded
  await createGame("2026-05-01T12:00:00Z",true);
  const {default:app}=await import(path.join(root,"artifacts/api-server/src/app.ts"));
  server=app.listen(0,"127.0.0.1");await new Promise(r=>server.once("listening",r));
  const base=`http://127.0.0.1:${server.address().port}`;
  const get=async(route,player=players[0])=>{
   const response=await fetch(base+route,{headers:{Authorization:`Bearer ${jwt.sign({id:player},process.env.JWT_SECRET)}`}});
   return {status:response.status,body:response.headers.get("content-type")?.includes("json")?await response.json():null};
  };
  const ownerPath=`/api/spieler/${players[0]}`;
  const own=await get(`${ownerPath}/spiele/${game}/scorecard`);
  assert.equal(own.status,200);assert.equal(own.body.players.length,1);
  assert.equal(own.body.players[0].name,"Report Owner");
  assert.equal(own.body.players[0].cells.length,18);assert.equal(own.body.maxPoints,36);
  assert.ok(!JSON.stringify(own.body).includes("Private Other"));
  assert.equal((await get(`/api/spieler/${players[1]}/spiele/${game}/scorecard`)).status,403);
  assert.equal((await get(`${ownerPath}/spiele/${game}/scorecard`,players[5])).status,200);
  assert.equal((await fetch(base+`${ownerPath}/spiele/${game}/scorecard`)).status,401);
  const annual=await get(`${ownerPath}/print?zeitraum=jahr&jahr=2026`);
  assert.equal(annual.status,200);assert.equal(annual.body.games.length,0);
  const stats=annual.body.stats;
  assert.equal(stats.gesamtSpiele,3);assert.equal(stats.gesamtSchuesse,54);
  assert.equal(stats.gesamtTreffer,36);assert.equal(stats.durchschnitt,18);
  assert.equal(stats.bestPunkte,18);assert.equal(stats.anzahlSpieltage,3);
  assert.equal(stats.besterTag,"2026-01-01");assert.equal(stats.monate.length,12);
  assert.deepEqual(stats.monate.filter(m=>m.spiele).map(m=>m.month),[1,2,3]);
  assert.equal(stats.monate.reduce((n,m)=>n+m.schuesse,0),stats.gesamtSchuesse);
  let sumGames=0,sumShots=0,sumHits=0;
  for(const day of ["2026-01-01","2026-02-01","2026-03-15"]) {
   const daily=await get(`${ownerPath}/print?tag=${day}`);
   assert.equal(daily.status,200);assert.equal(daily.body.games[0].players.length,1);
   assert.ok(!JSON.stringify(daily.body).includes("Private Other"));
   sumGames+=daily.body.stats.gesamtSpiele;sumShots+=daily.body.stats.gesamtSchuesse;sumHits+=daily.body.stats.gesamtTreffer;
  }
  assert.equal(sumGames,stats.gesamtSpiele);assert.equal(sumShots,stats.gesamtSchuesse);assert.equal(sumHits,stats.gesamtTreffer);
  const empty=await get(`${ownerPath}/print?zeitraum=jahr&jahr=2024`);
  assert.equal(empty.body.stats.gesamtSpiele,0);assert.equal(empty.body.stats.monate.length,12);
  assert.equal((await get(`${ownerPath}/print?zeitraum=jahr`)).status,400);
  assert.equal((await get(`/api/spieler/${players[1]}/print?zeitraum=jahr&jahr=2026`)).status,403);
  assert.equal((await get(`/api/spieler/${players[1]}/print-days`)).status,403);
  const days=await get(`${ownerPath}/print-days`);
  assert.deepEqual(days.body.years,[2027,2026]);
  const health=await get("/api/health");
  assert.ok(health.body.reportRoutes.some(r=>r.path==="/api/spieler/:id/spiele/:spielId/scorecard"));
  // A player's statistics exclusion must not remove another player's game,
  // nor modify the immutable scored records used by operational ledgers.
  const rawBefore=(await pool.query("SELECT count(*)::int n,sum(punkte)::int p FROM ergebnisse")).rows[0];
  await pool.query("INSERT INTO spiel_spieler_hidden(spiel_id,spieler_id,hidden_by) VALUES($1,$2,$3)",[game,players[0],players[5]]);
  assert.equal((await get(`${ownerPath}/spiele/${game}/scorecard`)).status,404);
  const reduced=await get(`${ownerPath}/print?zeitraum=jahr&jahr=2026`);
  assert.equal(reduced.body.stats.gesamtSpiele,2);
  assert.equal(reduced.body.stats.gesamtSchuesse,36);
  assert.equal(reduced.body.stats.monate[0].spiele,0);
  const unaffected=await get(`/api/spieler/${players[1]}/print?zeitraum=jahr&jahr=2026`,players[5]);
  assert.equal(unaffected.body.stats.gesamtSpiele,3);
  assert.deepEqual((await pool.query("SELECT count(*)::int n,sum(punkte)::int p FROM ergebnisse")).rows[0],rawBefore);
 } finally {
  if(server){server.closeAllConnections?.();await new Promise(r=>server.close(r));}
  if(pool)opened.add(pool);await Promise.all([...opened].map(p=>p.end()));
  pg.Pool=OriginalPool;await owner.query(`DROP DATABASE "${name}" WITH (FORCE)`);await owner.end();
 }
});
