import test from "node:test";
import assert from "node:assert/strict";
import {createRequire} from "node:module";
import {randomUUID} from "node:crypto";
import {mkdtemp,mkdir,readFile,writeFile,copyFile,rm} from "node:fs/promises";
import {tmpdir} from "node:os";
import path from "node:path";
const require=createRequire(import.meta.url),pg=require("pg"),OriginalPool=pg.Pool;
const root=path.resolve(import.meta.dirname,"../../..");

test("real PostgreSQL purge: migration, retained audit, day isolation, rollback and complete reset",async(t)=>{
 const owner=new OriginalPool({connectionString:process.env.DATABASE_URL});
 const name=`rm_purge_${randomUUID().replaceAll("-","")}`,pools=new Set();
 await owner.query(`CREATE DATABASE "${name}"`);
 const url=new URL(process.env.DATABASE_URL);url.pathname=`/${name}`;process.env.DATABASE_URL=url.href;
 process.env.JWT_SECRET="isolated-purge-test-signing-only";
 pg.Pool=class extends OriginalPool{constructor(...args){super(...args);pools.add(this);}};
 // A loader may have cached the original constructor. Track actual use as well,
 // and refuse any application query outside this disposable database.
 const originalConnect=OriginalPool.prototype.connect;
 OriginalPool.prototype.connect=function(...args){
  if(this!==owner){
   const database=this.options.connectionString?new URL(this.options.connectionString).pathname.slice(1):this.options.database;
   assert.equal(database,name,"Application query escaped the disposable test database");
   pools.add(this);
  }
  return originalConnect.apply(this,args);
 };
 const oldFolder=await mkdtemp(path.join(tmpdir(),"rm-purge-migrations-"));
 let pool,server;
 try{
  const {db,pool:connection}=await import("@workspace/db");pool=connection;
  assert.equal((await pool.query("SELECT current_database() name")).rows[0].name,name);
  const {migrate}=await import("drizzle-orm/node-postgres/migrator");
  const folder=path.join(root,"lib/db/drizzle");
  const journal=JSON.parse(await readFile(path.join(folder,"meta/_journal.json"),"utf8"));
  await mkdir(path.join(oldFolder,"meta"));
  const oldEntries=journal.entries.filter(e=>e.idx<18);
  await writeFile(path.join(oldFolder,"meta/_journal.json"),JSON.stringify({...journal,entries:oldEntries}));
  for(const entry of oldEntries)await copyFile(path.join(folder,`${entry.tag}.sql`),path.join(oldFolder,`${entry.tag}.sql`));
  await migrate(db,{migrationsFolder:oldFolder});
  const insertPlayer=async(admin=false,active=true)=>{
   const r=await pool.query(`INSERT INTO spieler(name,aktiv,portal_aktiv,is_admin,passwort_hash)
     VALUES($1,$2,true,$3,'synthetic-test-only-hash') RETURNING id`,
     [`Purge fixture ${randomUUID()}`,active,admin]);return r.rows[0].id;
  };
  const admin=await insertPlayer(true),otherAdmin=await insertPlayer(true,false);
  const legacy=(await pool.query(`INSERT INTO spiele(external_id,datum,modus,lauf,tauben_pro_lauf,abgeschlossen)
    VALUES('purge-legacy','2026-10-18T22:30:00Z','NORMAL',1,1,true) RETURNING id`)).rows[0].id;
  const legacyAudit=(await pool.query(`INSERT INTO admin_game_edits(admin_user_id,action,spiel_id,before,after)
    VALUES($1,'HIDE_GAME',$2,'{"hidden":false}','{"hidden":true}') RETURNING id`,[admin,legacy])).rows[0].id;
  await assert.rejects(pool.query("DELETE FROM spiele WHERE id=$1",[legacy]),e=>e.code==="23503");
  await migrate(db,{migrationsFolder:folder});
  const migrated=(await pool.query("SELECT * FROM admin_game_edits WHERE id=$1",[legacyAudit])).rows[0];
  assert.equal(migrated.before.game.externalId,"purge-legacy");
  assert.ok(migrated.before.game.date.startsWith("2026-10-18"));
  assert.equal(migrated.after.hidden,true);
  assert.equal((await pool.query(`SELECT confdeltype FROM pg_constraint WHERE conname='admin_game_edits_spiel_id_fkey'`)).rows[0].confdeltype,"n");
  await migrate(db,{migrationsFolder:folder});
  assert.deepEqual((await pool.query("SELECT * FROM admin_game_edits WHERE id=$1",[legacyAudit])).rows[0],migrated);

  const jwt=createRequire(path.join(root,"artifacts/api-server/package.json"))("jsonwebtoken");
  const {default:app}=await import(path.join(root,"artifacts/api-server/src/app.ts"));
  server=app.listen(0,"127.0.0.1");await new Promise(r=>server.once("listening",r));
  const base=`http://127.0.0.1:${server.address().port}`;
  const request=async(route,body,player=admin,method="POST")=>{
   const response=await fetch(base+route,{method,headers:{"Content-Type":"application/json",
    ...(player?{Authorization:`Bearer ${jwt.sign({id:player},process.env.JWT_SECRET)}`}:{})},
    body:JSON.stringify(body)});
   return {status:response.status,body:await response.json()};
  };
  const product=(await pool.query("INSERT INTO products(name,category) VALUES('Purge fixture','FOOD') RETURNING id")).rows[0].id;
  const price=(await pool.query("INSERT INTO product_price_revisions(product_id,unit_price_cents) VALUES($1,100) RETURNING id",[product])).rows[0].id;
  const seed=async(day,gameTime,existing)=>{
   const player=await insertPlayer(),prefix=randomUUID(),stamp=`${day}T12:00:00Z`;
   const game=existing??(await pool.query(`INSERT INTO spiele(external_id,datum,modus,lauf,tauben_pro_lauf,abgeschlossen)
     VALUES($1,$2,'NORMAL',1,1,true) RETURNING id`,[prefix,gameTime])).rows[0].id;
   const external=(await pool.query("SELECT external_id FROM spiele WHERE id=$1",[game])).rows[0].external_id;
   await pool.query("INSERT INTO spiel_teilnahmen(spiel_id,spieler_id,start_posten,punkte,lauf) VALUES($1,$2,1,2,1)",[game,player]);
   const shot=(await pool.query(`INSERT INTO ergebnisse(spiel_id,spieler_id,lauf,taube,maschine,posten,schuss1,schuss2,punkte)
     VALUES($1,$2,1,1,'A',1,true,false,2) RETURNING id`,[game,player])).rows[0].id;
   const queue=(await pool.query(`INSERT INTO queue_games(modus,position,status,created_at,started_at,started_spiel_id)
     VALUES('NORMAL',1,'STARTED',$1,$1,$2) RETURNING id`,[stamp,game])).rows[0].id;
   await pool.query(`INSERT INTO queue_entries(queue_game_id,spieler_id,modus,signup_at,status,spiel_id)
     VALUES($1,$2,'NORMAL',$3,'DONE',$4)`,[queue,player,stamp,game]);
   const group=(await pool.query("INSERT INTO spieler_gruppen(name) VALUES($1) RETURNING id",[prefix])).rows[0].id;
   await pool.query("INSERT INTO spieler_gruppen_mitglieder(gruppe_id,spieler_id) VALUES($1,$2)",[group,player]);
   await pool.query(`INSERT INTO gameday_players(datum,spieler_id,added_at,terminal_id,occurred_at,external_id)
     VALUES($1,$2,$3,$4,$3,$4)`,[day,player,stamp,prefix]);
   await pool.query("INSERT INTO gameday_maitre(datum,spieler_id) VALUES($1,$2)",[day,player]);
   await pool.query("INSERT INTO gameday_player_events(external_id,payload_hash,payload) VALUES($1,'hash',$2)",[prefix,JSON.stringify({datum:day,spielerId:player})]);
   await pool.query("INSERT INTO gameday_event(external_id,terminal_id,payload_hash,created_at) VALUES($1,$1,'hash',$2)",[prefix,stamp]);
   await pool.query("INSERT INTO ammo_inventory_day(datum,caliber,opening_count,closing_count) VALUES($1,12,100,99)",[day]);
   await pool.query("INSERT INTO daily_report_log(datum,status) VALUES($1,'SENT')",[day]);
   await pool.query("INSERT INTO terminal_game_manifest(terminal_id,datum) VALUES($1,$2)",[prefix,day]);
   await pool.query(`INSERT INTO kredit_events(external_id,spieler_id,datum,typ,anzahl) VALUES($1,$2,$3,'GRANT',3)`,[prefix,player,day]);
   await pool.query(`INSERT INTO sale_events(external_id,spieler_id,datum,product_id,price_revision_id,unit_price_cents,quantity,product_name,product_category)
     VALUES($1,$2,$3,$4,$5,100,1,'Purge fixture','FOOD')`,[prefix,player,day,product,price]);
   await pool.query("INSERT INTO bill_payments(external_id,spieler_id,datum,marked_by_admin_id) VALUES($1,$2,$3,$4)",[prefix,player,day,admin]);
   await pool.query("INSERT INTO spieler_updates(external_id,spieler_id,typ,created_at) VALUES($1,$2,'UPDATE',$3)",[prefix,player,stamp]);
   await pool.query("INSERT INTO live_game_state(terminal_id,seq,state,payload,updated_at) VALUES($1,1,'FINISHED',$2,$3)",
    [prefix,JSON.stringify({spielExternalId:external,startedAt:gameTime,players:[{spielerId:player}]}),stamp]);
   assert.equal((await request(`/api/admin/spiele/${game}/hide`,{})).status,200);
   assert.equal((await request(`/api/admin/spiele/${game}/spieler/${player}/hide`,{})).status,200);
   assert.equal((await request(`/api/admin/spiele/${game}/ergebnisse/${shot}`,{schuss1:false,schuss2:true},admin,"PATCH")).status,200);
   const audits=(await pool.query("SELECT before,after FROM admin_game_edits WHERE spiel_id=$1",[game])).rows;
   for(const audit of audits){
    assert.equal(audit.before.game.externalId,external);assert.ok(audit.after.game.date);
   }
   return {day,game,player,queue,group,external,prefix};
  };
  const day=await seed("2026-10-19","2026-10-18T22:30:00Z",legacy);
  const other=await seed("2026-10-20","2026-10-19T22:30:00Z");
  const tracked=["spiele","spiel_teilnahmen","ergebnisse","admin_game_edits","queue_games","queue_entries",
   "gameday_players","gameday_maitre","gameday_player_events","gameday_event","ammo_inventory_day",
   "daily_report_log","terminal_game_manifest","bill_payments","sale_events","kredit_events","spieler",
   "spieler_gruppen_mitglieder","spieler_updates","live_game_state","spiel_spieler_hidden"];
  const snapshot=async()=>Object.fromEntries(await Promise.all(tracked.map(async table=>[
   table,(await pool.query(`SELECT to_jsonb(t) AS row FROM "${table}" t ORDER BY to_jsonb(t)::text`)).rows.map(r=>r.row)])));
  await t.test("confirmations and authentication prevent any deletion",async()=>{
   const before=await snapshot();
   for(const body of [{mode:"all"},{mode:"day",datum:day.day,confirmation:"PURGE_ALL"},
    {mode:"day",datum:"2026-02-30",confirmation:"PURGE_DAY"}])
    assert.equal((await request("/api/admin/purge",body)).status,400);
   assert.equal((await request("/api/admin/purge",{mode:"all",confirmation:"PURGE_ALL"},day.player)).status,403);
   assert.equal((await request("/api/admin/purge",{mode:"all",confirmation:"PURGE_ALL"},null)).status,401);
   assert.deepEqual(await snapshot(),before);
  });
  await t.test("unknown FK returns admin-only readable JSON and rolls back every change",async()=>{
   await pool.query("CREATE TABLE purge_test_blocker(id serial PRIMARY KEY,spiel_id integer CONSTRAINT purge_test_game_fk REFERENCES spiele(id))");
   await pool.query("INSERT INTO purge_test_blocker(spiel_id) VALUES($1)",[legacy]);
   const before=await snapshot();
   const response=await request("/api/admin/purge",{mode:"day",datum:day.day,confirmation:"PURGE_DAY"});
   assert.equal(response.status,409);assert.equal(response.body.table,"purge_test_blocker");
   assert.equal(response.body.constraint,"purge_test_game_fk");
   assert.match(response.body.error,/Es wurde nichts gelöscht/);assert.ok(response.body.requestId);
   assert.deepEqual(await snapshot(),before);
   await pool.query("DROP TABLE purge_test_blocker");
  });
  await t.test("day purge clears all day activity, retaining audit identity and every other day's rows",async()=>{
   const before=await snapshot();
   const response=await request("/api/admin/purge",{mode:"day",datum:day.day,confirmation:"PURGE_DAY"});
   assert.equal(response.status,200,JSON.stringify(response.body));
   for(const field of ["games","results","participations","queueGames","queueEntries","gamedayPlayers",
    "gamedayMaitre","gamedayPlayerEvents","inventoryDays","dailyReports","terminalManifests",
    "billPayments","sales","credits","hiddenPlayers","liveGames"])assert.equal(response.body.counts[field],1,field);
   assert.equal(response.body.counts.players,0);assert.equal(response.body.counts.groupMembers,0);
   assert.equal(response.body.counts.gamedayEvents,0,"Undated closing receipts must not be guessed from receipt time");
   const oldAudit=(await pool.query("SELECT * FROM admin_game_edits WHERE id=$1",[legacyAudit])).rows[0];
   assert.equal(oldAudit.spiel_id,null);assert.equal(oldAudit.before.game.externalId,"purge-legacy");
   const purgeAudit=(await pool.query("SELECT * FROM admin_game_edits WHERE id=$1",[response.body.auditId])).rows[0];
   assert.equal(purgeAudit.action,"PURGE");assert.equal(purgeAudit.admin_user_id,admin);
   assert.deepEqual(purgeAudit.after,{mode:"day",datum:day.day,counts:response.body.counts});
   const after=await snapshot();
   assert.deepEqual(after.gameday_event,before.gameday_event,"Retain legacy receipt dedupe for every business day");
   // Every row which should remain is byte-for-byte unchanged, not just counted.
   for(const table of tracked){
    if(table==="admin_game_edits")continue; // selected audits intentionally detach
    const otherRows=before[table].filter(row=>
     row.datum===other.day || row.id===other.game&&table==="spiele" ||
     row.spiel_id===other.game || row.queue_game_id===other.queue ||
     row.terminal_id===other.prefix || row.external_id===other.prefix ||
     table==="spieler" || table==="spieler_updates" || table==="spieler_gruppen_mitglieder");
    for(const row of otherRows)assert.ok(after[table].some(r=>JSON.stringify(r)===JSON.stringify(row)),`${table}: other-day row changed`);
   }
  });
  await t.test("all purge handles all FKs, keeps all admins/credentials and every audit",async()=>{
   const admins=(await pool.query("SELECT * FROM spieler WHERE is_admin ORDER BY id")).rows;
   const count=(await pool.query("SELECT count(*)::int n FROM spieler WHERE NOT is_admin")).rows[0].n;
   const auditCount=(await pool.query("SELECT count(*)::int n FROM admin_game_edits")).rows[0].n;
   const response=await request("/api/admin/purge",{mode:"all",confirmation:"PURGE_ALL"});
   assert.equal(response.status,200,JSON.stringify(response.body));assert.equal(response.body.counts.players,count);
   assert.deepEqual((await pool.query("SELECT * FROM spieler WHERE is_admin ORDER BY id")).rows,admins);
   assert.ok(admins.some(a=>a.id===otherAdmin&&!a.aktiv));
   assert.equal((await pool.query("SELECT count(*)::int n FROM admin_game_edits")).rows[0].n,auditCount+1);
   for(const table of tracked.filter(t=>!["spieler","admin_game_edits"].includes(t)))
    assert.equal((await pool.query(`SELECT count(*)::int n FROM "${table}"`)).rows[0].n,0,table);
   assert.equal((await pool.query("SELECT count(*)::int n FROM products WHERE id=$1",[product])).rows[0].n,1);
  });
 }finally{
  if(server){server.closeAllConnections?.();await new Promise(r=>server.close(r));}
  if(pool)pools.add(pool);await Promise.all([...pools].map(p=>p.end()));
  const remaining=await owner.query("SELECT pid,state,query FROM pg_stat_activity WHERE datname=$1",[name]);
  assert.deepEqual(remaining.rows,[],"All test-owned PostgreSQL connections must close before dropping the database");
  OriginalPool.prototype.connect=originalConnect;
  pg.Pool=OriginalPool;await owner.query(`DROP DATABASE "${name}"`);await owner.end();
  await rm(oldFolder,{recursive:true,force:true});
 }
});
