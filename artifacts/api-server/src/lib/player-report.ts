import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { z } from "zod";
import { gameScorecard } from "./game-scorecard";
import { computePlayerStats } from "./player-stats";
import { daySchema,localDay } from "./local-day";

export const reportQuery=z.object({
  tag:daySchema.optional(),
  zeitraum:z.enum(["tag","jahr"]).default("tag"),
  jahr:z.coerce.number().int().min(1970).max(9998).optional(),
}).refine(v=>v.zeitraum!=="jahr"||v.jahr!==undefined,{message:"Für den Jahresbericht ist ein Jahr erforderlich."});

export async function playerReportDays(playerId:number) {
  const days=(await db.execute(sql`SELECT DISTINCT (g.datum AT TIME ZONE 'Europe/Luxembourg')::date::text datum
    FROM v_spiele_visible g JOIN v_teilnahmen_visible t ON t.spiel_id=g.id
    WHERE t.spieler_id=${playerId} ORDER BY datum DESC`)).rows.map((r:any)=>r.datum as string);
  return {days,years:[...new Set(days.map(d=>Number(d.slice(0,4))))]};
}

/** SQL-only annual aggregation: no individual annual games or shots in JS memory. */
export async function annualPlayerStats(playerId:number,year:number) {
  const from=`${year}-01-01`,until=`${year+1}-01-01`;
  const range=sql`g.datum>=${from}::timestamp AT TIME ZONE 'Europe/Luxembourg'
    AND g.datum<${until}::timestamp AT TIME ZONE 'Europe/Luxembourg'`;
  const [totals]=(await db.execute(sql`
    WITH games AS (
      SELECT g.id,(g.datum AT TIME ZONE 'Europe/Luxembourg')::date AS game_day,
        sum(t.punkte)::numeric points
      FROM v_spiele_visible g JOIN v_teilnahmen_visible t ON t.spiel_id=g.id
      WHERE t.spieler_id=${playerId} AND ${range}
      GROUP BY g.id,g.datum
    ), days AS (SELECT game_day,sum(points) points FROM games GROUP BY game_day),
    shots AS (
      SELECT e.schuss1 OR e.schuss2 hit FROM v_ergebnisse_visible e
      JOIN v_spiele_visible g ON g.id=e.spiel_id
      WHERE e.spieler_id=${playerId} AND NOT e.wiederholt AND ${range}
    )
    SELECT (SELECT count(*)::int FROM games) "gesamtSpiele",
      (SELECT count(*)::int FROM shots) "gesamtSchuesse",
      (SELECT count(*)::int FROM shots WHERE hit) "gesamtTreffer",
      COALESCE((SELECT round(avg(points),1)::float8 FROM games),0) durchschnitt,
      COALESCE((SELECT max(points)::int FROM games),0) "bestPunkte",
      (SELECT count(*)::int FROM days) "anzahlSpieltage",
      (SELECT game_day::text FROM days ORDER BY points DESC,game_day LIMIT 1) "besterTag",
      COALESCE((SELECT max(points)::int FROM days),0) "besterTagPunkte"`)).rows as any[];
  const buckets=(await db.execute(sql`
    SELECT 'maschine' kind,e.maschine::text key,count(*)::int versuche,
      count(*) FILTER(WHERE e.schuss1 OR e.schuss2)::int treffer
    FROM v_ergebnisse_visible e JOIN v_spiele_visible g ON g.id=e.spiel_id
    WHERE e.spieler_id=${playerId} AND NOT e.wiederholt AND ${range} GROUP BY e.maschine
    UNION ALL
    SELECT 'taube',e.taube::text,count(*)::int,count(*) FILTER(WHERE e.schuss1 OR e.schuss2)::int
    FROM v_ergebnisse_visible e JOIN v_spiele_visible g ON g.id=e.spiel_id
    WHERE e.spieler_id=${playerId} AND NOT e.wiederholt AND ${range} GROUP BY e.taube`)).rows as any[];
  const monthly=(await db.execute(sql`
    WITH games AS (
      SELECT extract(month FROM g.datum AT TIME ZONE 'Europe/Luxembourg')::int AS month_no,
        g.id,sum(t.punkte)::numeric points
      FROM v_spiele_visible g JOIN v_teilnahmen_visible t ON t.spiel_id=g.id
      WHERE t.spieler_id=${playerId} AND ${range} GROUP BY g.id,g.datum
    ), shots AS (
      SELECT extract(month FROM g.datum AT TIME ZONE 'Europe/Luxembourg')::int AS month_no,
        count(*)::int n,count(*) FILTER(WHERE e.schuss1 OR e.schuss2)::int hits
      FROM v_ergebnisse_visible e JOIN v_spiele_visible g ON g.id=e.spiel_id
      WHERE e.spieler_id=${playerId} AND NOT e.wiederholt AND ${range} GROUP BY 1
    )
    SELECT m.month_no AS "month", count(g.id)::int spiele,COALESCE(s.n,0)::int schuesse,
      COALESCE(s.hits,0)::int treffer,
      COALESCE(round(100.0*s.hits/NULLIF(s.n,0),1)::float8,0) quote,
      COALESCE(round(avg(g.points),1)::float8,0) schnitt
    FROM generate_series(1,12) m(month_no) LEFT JOIN games g ON g.month_no=m.month_no
    LEFT JOIN shots s ON s.month_no=m.month_no GROUP BY m.month_no,s.n,s.hits ORDER BY m.month_no`)).rows as any[];
  const bucket=(kind:string)=>Object.fromEntries(buckets.filter(r=>r.kind===kind).map(r=>[
    r.key,{versuche:r.versuche,treffer:r.treffer,quote:Math.round(r.treffer/r.versuche*1000)/10}]));
  return {spielerId:playerId,...totals,trefferquote:totals.gesamtSchuesse?
    Math.round(totals.gesamtTreffer/totals.gesamtSchuesse*1000)/10:0,
    maschinen:bucket("maschine"),tauben:bucket("taube"),breakdown:[],monate:monthly};
}

export async function playerPrintReport(playerId:number,query:unknown) {
  const request=reportQuery.parse(query),tag=request.tag??localDay();
  const [player]=(await db.execute(sql`SELECT name FROM spieler WHERE id=${playerId} AND aktiv`)).rows as any[];
  if(!player)return null;
  if(request.zeitraum==="jahr")return {name:player.name,tag,club:"Rangemaster",
    zeitraum:"jahr",jahr:request.jahr,games:[],stats:await annualPlayerStats(playerId,request.jahr!)};
  const ids=(await db.execute(sql`SELECT g.id FROM v_spiele_visible g JOIN v_teilnahmen_visible t ON t.spiel_id=g.id
    WHERE t.spieler_id=${playerId} AND (g.datum AT TIME ZONE 'Europe/Luxembourg')::date=${tag}::date
    GROUP BY g.id ORDER BY g.id`)).rows as any[];
  return {name:player.name,tag,club:"Rangemaster",zeitraum:"tag",
    games:(await Promise.all(ids.map(g=>gameScorecard(g.id,{onlySpielerId:playerId})))).filter(Boolean),
    stats:await computePlayerStats(playerId,{from:tag,to:tag})};
}
