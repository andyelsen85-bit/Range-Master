import test from "node:test";
import assert from "node:assert/strict";
import pg from "pg";
import jwt from "jsonwebtoken";
import { fileURLToPath } from "node:url";
import { migrate } from "drizzle-orm/node-postgres/migrator";
import { randomUUID } from "node:crypto";

test("diagnostics, game acknowledgements, historical guards and retention on an isolated database", async () => {
  const control = new pg.Pool({connectionString:process.env.DATABASE_URL});
  const name=`diagnostics_test_${process.pid}_${Date.now()}`;
  const url=new URL(process.env.DATABASE_URL!);url.pathname="/"+name;
  let fixturePool: pg.Pool | undefined;
  let server: ReturnType<import("express").Express["listen"]> | undefined;
  try {
    await control.query(`CREATE DATABASE "${name}"`);
    // Set only this isolated test process; no workspace/production settings.
    process.env.DATABASE_URL=url.toString();
    const {db,pool,spielerTable,apiKeysTable}=await import("@workspace/db");
    fixturePool=pool;
    assert.equal((await pool.query("SELECT current_database() n")).rows[0].n,name);
    await migrate(db,{migrationsFolder:fileURLToPath(new URL("../../../../lib/db/drizzle",import.meta.url))});
    const [admin,target]=await db.insert(spielerTable).values([
      {name:"Diagnose-Testadmin",isAdmin:true,aktiv:true,portalAktiv:true},
      {name:"Historischer Testspieler",aktiv:true,portalAktiv:true},
    ]).returning();
    const key="isolated-test-terminal-key";
    await db.insert(apiKeysTable).values({name:"Isolated diagnostics test",key,type:"TERMINAL",active:true});
    const {default:app}=await import("../app");
    await new Promise<void>((resolve,reject)=>{server=app.listen(0,"127.0.0.1",error=>error?reject(error):resolve());});
    assert.ok(server);
    const address=server.address();assert.ok(address && typeof address!=="string");
    const base=`http://127.0.0.1:${address.port}/api`;
    const token=jwt.sign({id:admin.id},process.env.JWT_SECRET??"trapmaster-dev-secret-change-in-prod",{expiresIn:"10m"});
    const headers={"Content-Type":"application/json","X-API-Key":key};
    const ids=Object.fromEntries(["unknown","historical","valid","new-inactive","still-valid","missing-game"]
      .map(name=>[name,randomUUID()]));
    const adminHeaders={Authorization:"Bearer "+token};
    const post=(path:string,body:unknown)=>fetch(base+path,{method:"POST",headers,body:JSON.stringify(body)});
    const stamp=new Date().toISOString();
    const entries=[
      {id:"risk",ts:stamp,type:"data_loss_risk",level:"ERROR",msg:"Spiel fehlt",data:{externalId:"missing-game"}},
      {id:"fire",ts:stamp,type:"fire_trace",level:"INFO",msg:"Schuss",data:{durationMs:10}},
      {id:"formula",ts:stamp,type:"wifi",level:"INFO",msg:"=2+2",data:{}},
      {id:"gateway",ts:stamp,type:"gateway_trace",level:"INFO",msg:"Gateway",data:{gatewayId:"gw-test",seq:1,durationMs:20}},
    ];
    let response=await post("/sync/terminal-logs",{terminalId:"T1",entries});
    assert.equal(response.status,200);assert.deepEqual((await response.json() as {acceptedIds:string[]}).acceptedIds,entries.map(e=>e.id));
    assert.equal((await post("/sync/terminal-logs",{terminalId:"T1",entries})).status,200);
    response=await post("/sync/terminal-logs",{terminalId:"T2",entries:[{...entries[3],id:"gateway-copy"}]});
    assert.deepEqual((await response.json() as {acceptedIds:string[]}).acceptedIds,["gateway-copy"]);
    assert.equal((await pool.query("SELECT count(*)::int n FROM terminal_log")).rows[0].n,4);
    assert.equal((await post("/sync/terminal-logs",{terminalId:"T1",entries:[{...entries[1],data:{nonce:"forbidden"}}]})).status,400);
    assert.equal((await post("/sync/terminal-logs",{terminalId:"T1",entries:Array(101).fill(entries[1])})).status,400);
    assert.equal((await fetch(base+"/admin/terminal-logs")).status,401);
    response=await fetch(base+"/admin/terminal-logs?terminal=T1&limit=2",{headers:adminHeaders});
    assert.equal(response.status,200);
    const page=await response.json() as {total:number;entries:{type:string}[];summaries:{type:string;median:number}[]};
    assert.equal(page.total,4);assert.equal(page.entries.length,2);
    assert.equal(page.entries[0].type,"data_loss_risk");
    assert.equal(page.summaries.find(s=>s.type==="fire_trace")?.median,10);
    response=await fetch(base+"/admin/terminal-logs/export?terminal=T1&type=wifi",{headers:adminHeaders});
    assert.equal(response.status,200);assert.match(await response.text(),/'=2\+2/);
    const finished=new Date(Date.now()-3600_000).toISOString();
    const game=(externalId:string,player:number,datum=finished)=>({
      externalId:ids[externalId],datum,modus:"NORMAL",lauf:1,taubenProLauf:25,abgeschlossen:true,confirmedLaunches:0,
      teilnahmen:[{spielerId:player,startPosten:1,punkte:0,lauf:1}],ergebnisse:[],
    });
    await pool.query("UPDATE spieler SET aktiv=false WHERE id=$1",[target.id]);
    response=await post("/sync/spiele",{spiele:[game("unknown",999999),game("historical",target.id),game("valid",admin.id)]});
    assert.equal(response.status,200);
    const gameResults=async(response:Response)=>(await response.json() as {results:{externalId:string;status:string}[]}).results;
    let results=await gameResults(response);
    assert.deepEqual(results.map((r:any)=>r.status),["rejected","created","created"]);
    response=await post("/sync/spiele",{spiele:[game("historical",target.id)]});
    assert.equal((await gameResults(response))[0].status,"skipped");
    response=await post("/sync/spiele",{spiele:[game("new-inactive",target.id,new Date(Date.now()+1000).toISOString()),game("still-valid",admin.id)]});
    results=await gameResults(response);
    assert.deepEqual(results.map((r:any)=>r.status),["rejected","created"]);
    response=await post("/sync/spiele",{spiele:[{...game("valid",admin.id),confirmedLaunches:1}]});
    assert.equal((await gameResults(response))[0].status,"conflict");
    const day=stamp.slice(0,10);
    assert.equal((await post("/sync/game-manifest",{terminalId:"T1",datum:day,externalIds:[ids.historical,ids["missing-game"]]})).status,200);
    assert.equal((await post("/sync/game-manifest",{terminalId:"T1",datum:day,externalIds:[ids.valid]})).status,200);
    response=await fetch(base+"/admin/game-reconciliation?terminal=T1",{headers:adminHeaders});
    const reconciliation=(await response.json() as {items:{missingIds:string[];present:number;total:number}[]}).items[0];
    assert.equal(reconciliation.total,3);assert.equal(reconciliation.present,2);
    assert.deepEqual(reconciliation.missingIds,[ids["missing-game"]]);
    await pool.query(`INSERT INTO terminal_log(terminal_id,entry_id,ts,type,level,msg,data)
      SELECT 'bulk','bulk-'||n,now(),'wifi','INFO','fixture','{}'::jsonb FROM generate_series(1,50002) n`);
    await pool.query(`INSERT INTO terminal_log(terminal_id,entry_id,ts,type,level,msg,data)
      VALUES('expired','old',now()-interval '8 days','wifi','INFO','old','{}')`);
    const {pruneTerminalLogs,logEntry,csvCell}=await import("../lib/terminal-logs");
    assert.equal(logEntry.safeParse({...entries[1],msg:"x".repeat(201)}).success,false);
    assert.equal(logEntry.safeParse({...entries[1],msg:"Bearer secret-example"}).success,false);
    assert.equal(csvCell("=1+1"),"\"'=1+1\"");
    await pruneTerminalLogs();
    assert.equal((await pool.query("SELECT count(*)::int n FROM terminal_log WHERE terminal_id='bulk'")).rows[0].n,50000);
    assert.equal((await pool.query("SELECT count(*)::int n FROM terminal_log WHERE terminal_id='expired'")).rows[0].n,0);
    for(let i=0;i<31;i++) response=await post("/sync/terminal-logs",{terminalId:"T1",entries:[]});
    assert.equal(response.status,429);
  } finally {
    if(server) {server.closeAllConnections();await new Promise<void>(resolve=>server!.close(()=>resolve()));}
    await fixturePool?.end();
    await control.query(`DROP DATABASE IF EXISTS "${name}" WITH (FORCE)`);
    await control.end();
  }
});
