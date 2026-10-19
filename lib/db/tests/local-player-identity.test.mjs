import test from "node:test";
import assert from "node:assert/strict";
import {createRequire} from "node:module";
import {randomUUID} from "node:crypto";
import path from "node:path";
import {fileURLToPath} from "node:url";
import {closeIsolatedDatabase} from "./helpers/close-isolated-database.mjs";
const require=createRequire(import.meta.url),pg=require("pg"),OriginalPool=pg.Pool;
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),"../../..");

test("terminal player identity: sequential/concurrent retries, response loss, no name merge",async()=>{
 const owner=new OriginalPool({connectionString:process.env.DATABASE_URL});
 const name=`rm_local_player_${randomUUID().replaceAll("-","")}`,opened=new Set();
 await owner.query(`CREATE DATABASE "${name}"`);
 const url=new URL(process.env.DATABASE_URL);url.pathname=`/${name}`;
 process.env.DATABASE_URL=url.href;
 pg.Pool=class extends OriginalPool{constructor(...args){super(...args);opened.add(this);}};
 const originalConnect=OriginalPool.prototype.connect;
 OriginalPool.prototype.connect=function(...args){
  if(this!==owner){
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
  const migrationsFolder=path.join(root,"lib/db/drizzle");
  await migrate(database.db,{migrationsFolder});
  const count=(await pool.query("SELECT count(*) FROM drizzle.__drizzle_migrations")).rows[0].count;
  await migrate(database.db,{migrationsFolder});
  assert.equal((await pool.query("SELECT count(*) FROM drizzle.__drizzle_migrations")).rows[0].count,count);
  const {getTableConfig}=await import("drizzle-orm/pg-core");
  const config=getTableConfig(database.spielerTable);
  assert.ok(config.columns.some(c=>c.name==="terminal_external_id"));
  const indexes=(await pool.query("SELECT indexdef FROM pg_indexes WHERE tablename='spieler'")).rows;
  assert.ok(indexes.some(r=>/UNIQUE.*terminal_external_id.*WHERE.*IS NOT NULL/i.test(r.indexdef)));
  await pool.query("INSERT INTO api_keys(name,key,type,active) VALUES('local identity test','local-identity-test-only','TERMINAL',true)");
  const {default:app}=await import(path.join(root,"artifacts/api-server/src/app.ts"));
  server=app.listen(0,"127.0.0.1");await new Promise(r=>server.once("listening",r));
  const base=`http://127.0.0.1:${server.address().port}`;
  const create=async(externalId,name,email)=> {
   const response=await fetch(base+"/api/sync/spieler-neu",{method:"POST",
    headers:{"Content-Type":"application/json","X-API-Key":"local-identity-test-only"},
    body:JSON.stringify({externalId,name,...(email?{email}:{})})});
   return {status:response.status,body:await response.json()};
  };
  const first=await create("terminal-one-identity","  Local Player  ");
  assert.equal(first.status,201);assert.equal(first.body.name,"Local Player");
  const again=await create("terminal-one-identity","Changed retry name");
  assert.equal(again.status,200);assert.equal(again.body.id,first.body.id);
  assert.equal(again.body.name,"Local Player","idempotent retry preserves operator name");
  const parallel=await Promise.all(Array.from({length:8},()=>create("terminal-parallel-identity","Concurrent Player")));
  assert.equal(new Set(parallel.map(r=>r.body.id)).size,1);
  assert.equal(parallel.filter(r=>r.status===201).length,1);
  assert.ok(parallel.every(r=>r.status===201||r.status===200));
  const duplicateName=await create("terminal-other-identity","Local Player");
  assert.equal(duplicateName.status,201);assert.notEqual(duplicateName.body.id,first.body.id);
  // Simulate a server-committed response the terminal never receives/records.
  await fetch(base+"/sync/spieler-neu",{method:"POST",
   headers:{"Content-Type":"application/json","X-API-Key":"local-identity-test-only"},
   body:JSON.stringify({externalId:"terminal-response-lost",name:"Lost response player"})});
  const recovered=await create("terminal-response-lost","Lost response player");
  assert.equal(recovered.status,200);
  const rows=(await pool.query("SELECT * FROM spieler WHERE terminal_external_id IS NOT NULL")).rows;
  assert.equal(rows.length,4);
  assert.equal(rows.filter(r=>r.terminal_external_id==="terminal-response-lost").length,1);
  assert.ok(rows.every(r=>r.portal_aktiv===false));
  assert.equal((await create("terminal-empty-name","   ")).status,400);
  assert.equal((await create("terminal-long-name","x".repeat(101))).status,400);
 } finally {
  if(server){server.closeAllConnections?.();await new Promise(r=>server.close(r));}
  if(pool)opened.add(pool);
   try{await closeIsolatedDatabase(owner,name,opened);}
   finally{
    OriginalPool.prototype.connect=originalConnect;
    pg.Pool=OriginalPool;await owner.end();
   }
 }
});
