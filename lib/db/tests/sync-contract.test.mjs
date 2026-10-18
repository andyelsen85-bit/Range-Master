import test from "node:test";
import assert from "node:assert/strict";
import {createRequire} from "node:module";
import {readFile} from "node:fs/promises";
import {randomUUID} from "node:crypto";
import path from "node:path";
import {fileURLToPath} from "node:url";
const require=createRequire(import.meta.url),pg=require("pg"),OriginalPool=pg.Pool;
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),"../../..");

function firmwareRequests(source) {
 const functions=[...source.matchAll(/^(?:static\s+)?(?:esp_err_t|bool|int|void|uint32_t)\s+\w+\([^;{}]*\)\s*\{/gm)];
 const requests=new Map();
 for(const match of source.matchAll(/"(\/api\/sync\/[^"]*)"/g)) {
  const fn=functions.findLastIndex(f=>f.index<=match.index);
  assert.ok(fn>=0,`No containing HTTP function for ${match[1]}`);
  const scope=source.slice(functions[fn].index,functions[fn+1]?.index);
  let methods=[...new Set([...scope.matchAll(/http_(get|post)_json\s*\(/g)].map(m=>m[1].toUpperCase()))];
  if(methods.length>1) {
   // Inventory pushes several events, then GETs its baseline in the same C++
   // function. Resolve the literal's path/url binding rather than assuming the
   // entire function uses one method.
   const line=source.slice(source.lastIndexOf("\n",match.index)+1,match.index);
   const diagnostic=/set_http_error\s*\(\s*"(GET|POST)"/.exec(line);
   const variable=/snprintf\s*\(\s*(\w+)\s*,/.exec(line)?.[1] ??
     /\b(\w+)\s*=(?!=)[^;\n]*$/.exec(line)?.[1];
   if(diagnostic)methods=[diagnostic[1]];
   else if(variable)methods=[...new Set([...scope.matchAll(new RegExp(`http_(get|post)_json\\s*\\(\\s*${variable}\\s*,`,"g"))].map(m=>m[1].toUpperCase()))];
  }
  assert.equal(methods.length,1,`Ambiguous HTTP method for ${match[1]}; extend contract extraction explicitly`);
  const endpoint=match[1].split("?")[0];
  assert.ok(!endpoint.includes("%"),`Dynamic path needs a fixture: ${endpoint}`);
  requests.set(`${methods[0]} ${endpoint}`,{method:methods[0],path:endpoint});
 }
 return [...requests.values()];
}

test("all firmware sync calls resolve with valid auth, both ingress forms; missing route fails; credit baseline proof",async()=>{
 const owner=new OriginalPool({connectionString:process.env.DATABASE_URL});
 const name=`rm_contract_${randomUUID().replaceAll("-","")}`,opened=new Set();
 await owner.query(`CREATE DATABASE "${name}"`);
 const url=new URL(process.env.DATABASE_URL);url.pathname=`/${name}`;process.env.DATABASE_URL=url.href;
 pg.Pool=class extends OriginalPool{constructor(...args){super(...args);opened.add(this);}};
 let pool,server;
 try {
  const database=await import("@workspace/db");pool=database.pool;
  const {migrate}=await import("drizzle-orm/node-postgres/migrator");
  await migrate(database.db,{migrationsFolder:path.join(root,"lib/db/drizzle")});
  await pool.query("INSERT INTO api_keys(name,key,type,active) VALUES('contract fixture','sync-contract-test-only','TERMINAL',true)");
  const {default:app}=await import(path.join(root,"artifacts/api-server/src/app.ts"));
  server=app.listen(0,"127.0.0.1");await new Promise(r=>server.once("listening",r));
  const base=`http://127.0.0.1:${server.address().port}`;
  const headers={"Content-Type":"application/json","X-API-Key":"sync-contract-test-only"};
  const requests=firmwareRequests(await readFile(path.join(root,"artifacts/firmware/main/net/http_sync.cpp"),"utf8"));
  assert.ok(requests.length>=20);
  for(const prefix of ["/api",""]) {
   const health=await fetch(base+prefix+"/health");
   assert.equal(health.status,200);
   assert.equal(health.headers.get("cache-control"),"no-store");
   const info=await health.json();
   assert.ok(info.build.commit);assert.equal(typeof info.build.dirty,"boolean");
   for(const request of requests) {
    assert.ok(info.syncRoutes.some(r=>r.path===request.path&&r.method===request.method),
     `Missing mounted route: ${request.method} ${request.path}`);
    const route=prefix+request.path.slice(4);
    const response=await fetch(base+route+(request.method==="GET"?"?datum=invalid&code=&terminalId=":""),{
     method:request.method,headers,body:request.method==="GET"?undefined:"{}"});
    assert.notEqual(response.status,404,`CONTRACT: ${request.method} ${route} returned 404`);
    assert.notEqual(response.status,405,`CONTRACT: ${request.method} ${route} returned 405`);
    assert.notEqual(response.status,401,`Fixture key did not authenticate ${route}`);
    assert.ok(response.status<500,`${route}: unexpected server error ${await response.text()}`);
   }
  }
  // Prove this check catches the exact regression, rather than just passing
  // because a blanket auth middleware returns 401 for an absent route.
  const {default:syncRouter}=await import(path.join(root,"artifacts/api-server/src/routes/sync.ts"));
  const index=syncRouter.stack.findIndex(layer=>layer.route?.path==="/kredite/state");
  assert.ok(index>=0);const [removed]=syncRouter.stack.splice(index,1);
  try {
   const absent=await fetch(base+"/api/sync/kredite/state",{method:"POST",headers,body:"{}"});
   assert.equal(absent.status,404);
  } finally {syncRouter.stack.splice(index,0,removed);}

  const player=(await pool.query("INSERT INTO spieler(name,aktiv) VALUES('Contract credit fixture',true) RETURNING id")).rows[0].id;
  const product=(await pool.query("INSERT INTO products(code,name,category,is_system) VALUES('GAME_CREDIT','Spiel','GAME_CREDIT',true) ON CONFLICT(code) DO UPDATE SET name=excluded.name RETURNING id")).rows[0].id;
  const revision=(await pool.query("INSERT INTO product_price_revisions(product_id,unit_price_cents) VALUES($1,500) RETURNING id",[product])).rows[0].id;
  const grant=randomUUID(),use=randomUUID(),voidId=randomUUID(),unsent=randomUUID(),datum="2026-10-18";
  const events=[
   {externalId:grant,spielerId:player,datum,typ:"GRANT",anzahl:3},
   {externalId:use,spielerId:player,datum,typ:"USE",anzahl:1,occurredAt:datum+"T12:00:00.000Z",priceRevisionId:revision,unitPriceCents:500},
   {externalId:voidId,spielerId:player,datum,typ:"VOID_USE",anzahl:1,voidsExternalId:use},
  ];
  const pushed=await fetch(base+"/api/sync/kredite",{method:"POST",headers,body:JSON.stringify({events})});
  assert.equal(pushed.status,200);
  const outcomes=(await pushed.json()).results;
  assert.ok(outcomes.every(r=>r.status==="accepted"),JSON.stringify(outcomes));
  const state=await fetch(base+"/api/sync/kredite/state",{method:"POST",headers,
   body:JSON.stringify({datum,pendingEventIds:[grant,use,voidId,unsent],useIds:[use]})});
  assert.equal(state.status,200);
  const baseline=await state.json(),credit=baseline.kredite.find(k=>k.spielerId===player);
  assert.equal(credit.gewaehrt,3);assert.equal(credit.verbraucht,0);
  assert.deepEqual([...baseline.knownEventIds].sort(),[grant,use,voidId].sort());
  assert.deepEqual(baseline.voidedUseIds,[use]);assert.deepEqual(baseline.countedUseIds,[]);
  assert.equal((await pool.query("SELECT count(*)::int n FROM kredit_events WHERE typ='GRANT'")).rows[0].n,1);
  console.log(`PASS ${requests.length} firmware method/path contracts x both ingress forms; missing-state negative control; GRANT/USE/VOID baseline without compensating GRANT`);
 } finally {
  if(server)await new Promise(r=>server.close(r));
  if(pool)opened.add(pool);for(const p of opened)await p.end();
  await owner.query(`DROP DATABASE IF EXISTS "${name}"`);await owner.end();
 }
});
