import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
export async function computePlayerStats(spielerId:number,range:{from?:string;to?:string}={}) {
  const day=sql`(g.datum AT TIME ZONE 'Europe/Luxembourg')::date`;
  const filter=sql`e.spieler_id=${spielerId} AND NOT e.wiederholt ${range.from?sql`AND ${day}>=${range.from}::date`:sql``}
    ${range.to?sql`AND ${day}<=${range.to}::date`:sql``}`;
  const shots=(await db.execute(sql`SELECT e.*,g.modus FROM v_ergebnisse_visible e JOIN v_spiele_visible g ON g.id=e.spiel_id WHERE ${filter}`)).rows as any[];
  const games=(await db.execute(sql`SELECT g.id,g.modus,sum(t.punkte)::int punkte,g.tauben_pro_lauf*2*g.lauf maximum
    FROM v_teilnahmen_visible t JOIN v_spiele_visible g ON g.id=t.spiel_id WHERE t.spieler_id=${spielerId}
    ${range.from?sql`AND ${day}>=${range.from}::date`:sql``} ${range.to?sql`AND ${day}<=${range.to}::date`:sql``}
    GROUP BY g.id,g.modus,g.tauben_pro_lauf,g.lauf`)).rows as any[];
  const buckets=(key:string)=>Object.fromEntries([...new Set(shots.map(s=>String(s[key])))].sort().map(k=>{
    const values=shots.filter(s=>String(s[key])===k),hits=values.filter(s=>s.schuss1||s.schuss2).length;
    return[k,{versuche:values.length,treffer:hits,quote:values.length?Math.round(hits/values.length*1000)/10:0}];
  }));
  const hits=shots.filter(s=>s.schuss1||s.schuss2).length;
  return {spielerId,gesamtSpiele:games.length,gesamtSchuesse:shots.length,gesamtTreffer:hits,
    durchschnitt:games.length?Math.round(games.reduce((n,g)=>n+g.punkte,0)/games.length*10)/10:0,
    bestPunkte:games.length?Math.max(...games.map(g=>g.punkte)):0,trefferquote:shots.length?Math.round(hits/shots.length*1000)/10:0,
    maschinen:buckets("maschine"),tauben:buckets("taube"),breakdown:[...new Set(games.map(g=>g.modus))].map(modus=>{
      const rows=games.filter(g=>g.modus===modus),points=rows.reduce((n,g)=>n+g.punkte,0),maximum=rows.reduce((n,g)=>n+g.maximum,0);
      return {modus,anzahlSpiele:rows.length,durchschnitt:Math.round(points/rows.length*10)/10,durchschnittProzent:maximum?Math.round(points/maximum*1000)/10:0};
    })};
}
