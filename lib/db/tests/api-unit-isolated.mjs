// Runs the existing API suite against a fully migrated disposable PostgreSQL
// database, never the developer's queues, credits, bills or other live fixtures.
import {createRequire} from "node:module";
import {randomUUID} from "node:crypto";
import {spawnSync} from "node:child_process";
import path from "node:path";
import {fileURLToPath} from "node:url";
const require=createRequire(import.meta.url),pg=require("pg"),OriginalPool=pg.Pool;
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),"../../..");
const owner=new OriginalPool({connectionString:process.env.DATABASE_URL});
const name=`rm_api_units_${randomUUID().replaceAll("-","")}`,opened=new Set();
let status=1;
try {
 await owner.query(`CREATE DATABASE "${name}"`);
 const url=new URL(process.env.DATABASE_URL);url.pathname=`/${name}`;process.env.DATABASE_URL=url.href;
 pg.Pool=class extends OriginalPool{constructor(...args){super(...args);opened.add(this);}};
 const database=await import("@workspace/db");
 const {migrate}=await import("drizzle-orm/node-postgres/migrator");
 await migrate(database.db,{migrationsFolder:path.join(root,"lib/db/drizzle")});
 // The existing gameday integration expects the standard ammunition catalog.
 // Use explicit test-only fixtures instead of borrowing developer products.
 await database.pool.query(`INSERT INTO products(code,name,category,is_system) VALUES
  ('GAME_CREDIT','Fixture Spiel','GAME_CREDIT',true),
  ('AMMO_CAL12','Fixture Kaliber 12','AMMO_CAL12',true),
  ('AMMO_CAL20','Fixture Kaliber 20','AMMO_CAL20',true)`);
 await database.pool.query(`INSERT INTO product_price_revisions(product_id,unit_price_cents)
  SELECT id,1000 FROM products WHERE is_system`);
 await database.pool.query(`INSERT INTO api_keys(name,key,type,active)
  VALUES('Isolated fixture terminal','isolated-fixture-only','TERMINAL',true)`);
 status=spawnSync("pnpm",["--filter","@workspace/api-server","run","test"],{cwd:root,env:process.env,stdio:"inherit"}).status??1;
}finally{
 await Promise.all([...opened].map(p=>p.end()));pg.Pool=OriginalPool;
 await owner.query(`DROP DATABASE IF EXISTS "${name}" WITH (FORCE)`);await owner.end();
}
process.exitCode=status;
