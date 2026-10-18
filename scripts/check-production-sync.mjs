// Read-only deployment verification. Never print an API key or per-player data.
import {execFileSync} from "node:child_process";
const origin=process.env.SYNC_PROBE_ORIGIN??"https://rangemaster.hostzone.lu";
const key=process.env.PRODUCTION_TERMINAL_API_KEY;
if(!key)throw new Error("Set PRODUCTION_TERMINAL_API_KEY through workspace Secrets; do not put it in command arguments.");
const endpoint=new URL(origin);
if(endpoint.protocol!=="https:")throw new Error("Production probe requires HTTPS");
const datum=process.env.SYNC_PROBE_DATUM??new Intl.DateTimeFormat("sv-SE",{timeZone:"Europe/Luxembourg"}).format(new Date());
if(!/^\d{4}-\d{2}-\d{2}$/.test(datum))throw new Error("Invalid probe date");
const expected=process.env.EXPECTED_BUILD_COMMIT??execFileSync("git",["rev-parse","HEAD"],{encoding:"utf8"}).trim();
const results=[];let failed=false;
for(const [path,body] of [
 ["/api/health",null],
 ["/api/sync/kredite/state",{datum,pendingEventIds:[],useIds:[]}],
 ["/api/sync/kredite",{}],
 ["/api/sync/spiele",{}],
]) {
 const r=await fetch(new URL(path,endpoint),{method:body?"POST":"GET",
  headers:{"X-API-Key":key,"Content-Type":"application/json"},body:body?JSON.stringify(body):undefined,
  signal:AbortSignal.timeout(20_000)});
 const text=await r.text();let payload;try{payload=JSON.parse(text);}catch{}
 const result={path,httpStatus:r.status};
 if(path==="/api/health") {
  result.build=payload?.build;
  result.expectedCommit=expected;
  result.matchesExpectedCommit=payload?.build?.commit===expected&&!payload?.build?.dirty;
  result.creditStateListed=payload?.syncRoutes?.some(row=>row.method==="POST"&&row.path==="/api/sync/kredite/state")??false;
  if(r.status!==200||!result.matchesExpectedCommit||!result.creditStateListed)failed=true;
 } else if(path.endsWith("/state")) {
  result.baselineAvailable=r.status===200&&Array.isArray(payload?.kredite)&&payload.datum===datum&&
   ["knownEventIds","voidedUseIds","countedUseIds"].every(field=>Array.isArray(payload[field]));
  result.baselinePlayerCount=payload?.kredite?.length;
  if(!result.baselineAvailable)failed=true;
 } else if(r.status!==400)failed=true; // deliberately invalid bodies; no writes
 results.push(result);
}
console.log(JSON.stringify({origin:endpoint.origin,datum,results,verified:!failed},null,2));
if(failed)process.exitCode=1;
