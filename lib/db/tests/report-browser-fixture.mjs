// Disposable development-only fixture for the real browser/PDF acceptance run.
// No production credentials, existing people, financial events or terminals.
import pg from "pg";
import {createRequire} from "node:module";
import {randomUUID} from "node:crypto";
import {readFile,writeFile,mkdir} from "node:fs/promises";
import path from "node:path";
const root=path.resolve(import.meta.dirname,"../../..");
const file=path.join(root,"generated-artifacts/report-browser-fixture.json");
if(process.env.NODE_ENV==="production")throw new Error("Never run browser fixtures in production");
const pool=new pg.Pool({connectionString:process.env.DATABASE_URL});
const client=await pool.connect();
try {
 await client.query("BEGIN");
 if(process.argv.includes("--cleanup")) {
  const fixture=JSON.parse(await readFile(file,"utf8"));
  const owned=await client.query("SELECT id FROM spieler WHERE id=ANY($1::int[]) AND email LIKE $2",
   [fixture.players,`${fixture.namespace}-%@example.invalid`]);
  if(owned.rows.length!==fixture.players.length)throw new Error("Refusing cleanup: fixture ownership mismatch");
  await client.query("DELETE FROM spiele WHERE id=ANY($1::int[]) AND external_id LIKE $2",[fixture.games,`${fixture.namespace}-%`]);
  await client.query("DELETE FROM spieler WHERE id=ANY($1::int[])",[fixture.players]);
  await client.query("COMMIT");console.log("Report browser fixtures removed");
 } else {
  const bcrypt=createRequire(path.join(root,"artifacts/api-server/package.json"))("bcryptjs");
  // Deliberately public fixture-only login; never reused for a real account.
  const hash=await bcrypt.hash("ReportOnly-2026!Fixture",10);
  const namespace=`rm-report-${randomUUID()}`,players=[],names=[],emails=[];
  for(let i=0;i<6;i++){
   names.push(i===0?"Report Fixture Owner":`Private Fixture Other ${i}`);
   emails.push(`${namespace}-${i}@example.invalid`);
   players.push((await client.query(`INSERT INTO spieler(name,email,passwort_hash,aktiv,portal_aktiv,is_admin)
    VALUES($1,$2,$3,true,true,$4) RETURNING id`,[names[i],emails[i],hash,i===5])).rows[0].id);
  }
  const games=[];
  for(const [i,date] of ["2026-01-10T11:00:00Z","2026-03-15T11:00:00Z","2026-10-18T12:00:00Z"].entries()){
   const game=(await client.query(`INSERT INTO spiele(external_id,datum,modus,lauf,tauben_pro_lauf,abgeschlossen)
    VALUES($1,$2,'NORMAL',2,9,true) RETURNING id`,[`${namespace}-${i}`,date])).rows[0].id;games.push(game);
   for(const player of players)for(let run=1;run<=2;run++){
    await client.query("INSERT INTO spiel_teilnahmen(spiel_id,spieler_id,start_posten,punkte,lauf) VALUES($1,$2,1,9,$3)",[game,player,run]);
    await client.query(`INSERT INTO ergebnisse(spiel_id,spieler_id,lauf,taube,maschine,posten,schuss1,schuss2,punkte,wiederholt)
     SELECT $1,$2,$3,n,CASE WHEN n>=8 THEN 'H' ELSE chr(64+n) END::maschine,
     CASE WHEN n>=8 THEN 6 ELSE n END,n%3>0,false,n%3,false FROM generate_series(1,9) n`,[game,player,run]);
   }
  }
  await mkdir(path.dirname(file),{recursive:true});
  await writeFile(file,JSON.stringify({namespace,players,names,emails,games,tag:"2026-10-18",year:2026},null,2));
  await client.query("COMMIT");
  console.log("Disposable report fixture ready; IDs in generated-artifacts/report-browser-fixture.json");
 }
} catch(error) {await client.query("ROLLBACK");throw error;}
finally {client.release();await pool.end();}
