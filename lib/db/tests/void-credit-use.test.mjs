import test from "node:test";
import assert from "node:assert/strict";
import {createRequire} from "node:module";
import {randomUUID} from "node:crypto";
import {readFile} from "node:fs/promises";
import path from "node:path";
import {fileURLToPath} from "node:url";

const require=createRequire(import.meta.url),pg=require("pg"),OriginalPool=pg.Pool;
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),"../../..");
test("VOID_USE: real HTTP/PG, ordering, retries, mixed players, bills, reports and untouched history",async()=>{
 const owner=new OriginalPool({connectionString:process.env.DATABASE_URL});
 const name=`rm_void_use_${randomUUID().replaceAll("-","")}`,opened=new Set();
 await owner.query(`CREATE DATABASE "${name}"`);
 const url=new URL(process.env.DATABASE_URL);url.pathname=`/${name}`;process.env.DATABASE_URL=url.href;
 pg.Pool=class extends OriginalPool{constructor(...args){super(...args);opened.add(this);}};
 let pool,server;
 try {
  const database=await import("@workspace/db");pool=database.pool;
  const {migrate}=await import("drizzle-orm/node-postgres/migrator");
  await migrate(database.db,{migrationsFolder:path.join(root,"lib/db/drizzle")});
  const journal=JSON.parse(await readFile(path.join(root,"lib/db/drizzle/meta/_journal.json"),"utf8"));
  const applied=(await pool.query("SELECT * FROM drizzle.__drizzle_migrations ORDER BY id")).rows;
  assert.equal(applied.length,journal.entries.length);
  await migrate(database.db,{migrationsFolder:path.join(root,"lib/db/drizzle")});
  assert.deepEqual((await pool.query("SELECT * FROM drizzle.__drizzle_migrations ORDER BY id")).rows,applied);
  const players=[];
  for(let i=0;i<3;i++)players.push((await pool.query("INSERT INTO spieler(name,aktiv,portal_aktiv,is_admin) VALUES($1,true,true,$2) RETURNING id",[`VOID fixture ${i}`,i===0])).rows[0].id);
  await pool.query("INSERT INTO api_keys(name,key,type,active) VALUES('VOID fixture','void-use-test-only','TERMINAL',true)");
  const product=(await pool.query("INSERT INTO products(code,name,category,is_system) VALUES('GAME_CREDIT','Spiel','GAME_CREDIT',true) RETURNING id")).rows[0].id;
  const revision=(await pool.query("INSERT INTO product_price_revisions(product_id,unit_price_cents) VALUES($1,500) RETURNING id",[product])).rows[0].id;
  const {default:app}=await import(path.join(root,"artifacts/api-server/src/app.ts"));
  server=app.listen(0,"127.0.0.1");await new Promise(r=>server.once("listening",r));
  const jwt=require(path.join(root,"artifacts/api-server/node_modules/jsonwebtoken"));
  const admin=jwt.sign({id:players[0]},process.env.JWT_SECRET??"trapmaster-dev-secret-change-in-prod");
  const base=`http://127.0.0.1:${server.address().port}/api`;
  async function request(route,body,asAdmin=false) {
   const r=await fetch(base+route,{method:body===undefined?"GET":"POST",headers:{"Content-Type":"application/json",
    ...(asAdmin?{Authorization:`Bearer ${admin}`} :{"X-Api-Key":"void-use-test-only"})},body:body===undefined?undefined:JSON.stringify(body)});
   return {status:r.status,data:await r.json()};
  }
  const day="2026-10-18",event=(typ,player=players[0],datum=day)=>({
   externalId:randomUUID(),spielerId:player,datum,typ,anzahl:1,
   ...(typ==="USE"?{occurredAt:`${datum}T10:00:00.000Z`,priceRevisionId:revision,unitPriceCents:500}:{})
  });
  const voidFor=use=>({...event("VOID_USE",use.spielerId,use.datum),voidsExternalId:use.externalId});
  const push=async events=>{
   const r=await request("/sync/kredite",{events});assert.equal(r.status,200,JSON.stringify(r));return r.data;
  };
  const state=async(events=[],uses=[],datum=day)=>{
   const r=await request("/sync/kredite/state",{datum,pendingEventIds:events.map(e=>e.externalId),useIds:uses.map(e=>e.externalId)});
   assert.equal(r.status,200,JSON.stringify(r));return r.data;
  };
  const balance=async(player=players[0],datum=day)=>{
   const s=await state([],[],datum);return s.kredite.find(r=>r.spielerId===player)??{spielerId:player,gewaehrt:0,verbraucht:0};
  };
  const {dayBillSummary,periodBillSummary}=await import(path.join(root,"artifacts/api-server/src/lib/bills.ts"));
  const {buildDailyReport}=await import(path.join(root,"artifacts/api-server/src/lib/daily-report.ts"));
  const grants=players.map(p=>({...event("GRANT",p),anzahl:10}));await push(grants);
  // a: unsent USE removed locally => no portal debit/refund at all.
  assert.equal((await balance()).gewaehrt,10);
  const baseline=await buildDailyReport(day);
  // b: USE already accepted; cancellation changes used, never grants.
  const used=event("USE"),voided=voidFor(used);await push([used]);
  assert.equal((await balance()).verbraucht,1);
  const beforeBill=await dayBillSummary(day);assert.equal(beforeBill.players.find(p=>p.spielerId===players[0]).totalCents,500);
  await push([voided]);await push([used,voided]); // retries / replay
  assert.deepEqual(await balance(),{spielerId:players[0],gewaehrt:10,verbraucht:0});
  assert.deepEqual(await buildDailyReport(day),baseline);
  assert.equal((await dayBillSummary(day)).players.find(p=>p.spielerId===players[0]).totalCents,0);
  assert.equal((await periodBillSummary(day,day)).players.find(p=>p.spielerId===players[0]).totalCents,0);
  // c: in-flight arrival in either order and same batch, VOID first.
  const first=event("USE"),vf=voidFor(first);await push([vf]);await push([first]);
  const batch=event("USE"),vb=voidFor(batch);await push([vb,batch]);
  const duplicate=voidFor(batch);const skipped=await push([duplicate]);assert.equal(skipped.results[0].status,"skipped");
  assert.equal((await balance()).verbraucht,0);
  const proof=await state([batch,vb],[batch]);
  assert.ok(proof.knownEventIds.includes(batch.externalId));assert.ok(proof.knownEventIds.includes(vb.externalId));
  assert.deepEqual(proof.voidedUseIds,[batch.externalId]);assert.deepEqual(proof.countedUseIds,[]);
  // d: three players with synced, in-flight, never-sent USEs.
  const mixed=players.map(p=>event("USE",p));
  await push([mixed[0]]);await push([voidFor(mixed[0]),voidFor(mixed[1]),mixed[1]]);
  for(const p of players)assert.deepEqual(await balance(p),{spielerId:p,gewaehrt:10,verbraucht:0});
  // e/f: restart/offline outbox delivered repeatedly hours later.
  const late=event("USE"),vl=voidFor(late),durable=JSON.parse(JSON.stringify([late,vl]));
  await push(durable);await push(durable);assert.equal((await balance()).verbraucht,0);
  // Invalid binding, including a reference which arrived before its USE.
  for(const bad of [
   {...voidFor(used),spielerId:players[1]}, {...voidFor(used),datum:"2026-10-17"},
   {...voidFor(used),unitPriceCents:500}, {...voidFor(used),anzahl:2},
   {...used,anzahl:2},
  ])assert.ok((await request("/sync/kredite",{events:[bad]})).status>=400);
  const absent=event("USE"),va=voidFor(absent);await push([va]);
  assert.ok((await request("/sync/kredite",{events:[{...absent,spielerId:players[2]}]})).status>=400);
  // Uncancelled uses still bill normally and are identified for pending VOID overlays.
  const normal=event("USE",players[2]);await push([normal]);
  assert.deepEqual((await state([normal],[normal])).countedUseIds,[normal.externalId]);
  assert.equal((await balance(players[2])).verbraucht,1);
  // Legacy compensation is retained verbatim and exposed only as a heuristic.
  const oldUse=event("USE",players[0],"2026-10-17"),oldGrant=event("GRANT",players[0],"2026-10-17");
  await push([{...event("GRANT",players[0],"2026-10-17"),anzahl:2},oldUse,oldGrant]);
  const oldRows=(await pool.query("SELECT * FROM kredit_events WHERE datum='2026-10-17' ORDER BY id")).rows;
  const candidates=await request("/admin/kredite/legacy-cancel-candidates?from=2026-10-17&to=2026-10-17",undefined,true);
  assert.equal(candidates.status,200,JSON.stringify(candidates));assert.ok(candidates.data.candidates.some(c=>c.useExternalId===oldUse.externalId&&c.grantExternalId===oldGrant.externalId));
  assert.deepEqual((await pool.query("SELECT * FROM kredit_events WHERE datum='2026-10-17' ORDER BY id")).rows,oldRows);
  const overview=await request(`/admin/kredite?datum=${day}`,undefined,true);
  assert.equal(overview.data.kredite.find(r=>r.spielerId===players[0]).gewaehrt,10);
  assert.equal(overview.data.kredite.find(r=>r.spielerId===players[0]).verbraucht,0);
  assert.equal((await pool.query("SELECT count(*)::int n FROM spiele")).rows[0].n,0);
  assert.equal((await pool.query("SELECT count(*)::int n FROM bill_payments")).rows[0].n,0);
  console.log("PASS: fresh/repeated migration, all six cancellation flows, event binding, no grant inflation, bills/reports, legacy retention.");
 } finally {
  if(server)await new Promise(r=>server.close(r));
  await Promise.all([...opened].map(p=>p.end()));pg.Pool=OriginalPool;
  await owner.query(`DROP DATABASE "${name}" WITH (FORCE)`);await owner.end();
 }
});
