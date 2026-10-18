import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { buildScorecard, type SavedResult } from "@workspace/scorecard";
export async function gameScorecard(id: number,options:{includeHidden?:boolean;viewerSpielerId?:number;anonymise?:boolean}={}) {
  const games=sql.raw(options.includeHidden?"spiele":"v_spiele_visible");
  const participations=sql.raw(options.includeHidden?"spiel_teilnahmen":"v_teilnahmen_visible");
  const shots=sql.raw(options.includeHidden?"ergebnisse":"v_ergebnisse_visible");
  const [game] = (await db.execute(sql`SELECT datum,completed_at "completedAt",modus,lauf,tauben_pro_lauf "taubenProLauf" FROM ${games} WHERE id=${id}`)).rows as any[];
  if (!game) return null;
  const players = (await db.execute(sql`SELECT s.id,s.name,sum(t.punkte)::int total FROM ${participations} t
    JOIN spieler s ON s.id=t.spieler_id WHERE t.spiel_id=${id} GROUP BY s.id,s.name ORDER BY min(t.start_posten),min(t.id)`)).rows as { id: number; name: string; total: number }[];
  if(options.anonymise){let other=2;players.forEach(p=>{if(p.id!==options.viewerSpielerId)p.name=`Spieler ${other++}`;});}
  const results = (await db.execute(sql`SELECT spieler_id "spielerId",lauf,taube,maschine,posten,punkte,wiederholt
    FROM ${shots} WHERE spiel_id=${id} ORDER BY id`)).rows as unknown as SavedResult[];
  return buildScorecard(game, players, results);
}
