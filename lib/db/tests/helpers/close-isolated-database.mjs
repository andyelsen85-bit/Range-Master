import assert from "node:assert/strict";
import {setTimeout} from "node:timers/promises";

/** Never force-terminate clients: wait for graceful closes to reach PostgreSQL. */
export async function closeIsolatedDatabase(owner,name,pools) {
 assert.match(name,/^rm_[a-z_]+_[a-f0-9]{32}$/,"Only disposable test databases may be dropped");
 await Promise.all([...new Set(pools)].map(pool=>pool.end()));
 const deadline=Date.now()+5_000;
 for(;;){
  const {rows}=await owner.query("SELECT pid,state FROM pg_stat_activity WHERE datname=$1",[name]);
  if(rows.length===0)break;
  assert.ok(Date.now()<deadline,`Test connections did not close: ${JSON.stringify(rows)}`);
  // Pool.end resolves on local socket shutdown, before the remote backend has
  // necessarily processed it. Poll briefly; a persistent leaked pool still fails.
  await setTimeout(25);
 }
 await owner.query(`DROP DATABASE "${name}"`);
}
