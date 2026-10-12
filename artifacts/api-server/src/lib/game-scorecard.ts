import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { buildScorecard, type SavedResult } from "@workspace/scorecard";
export async function gameScorecard(id: number) {
  const [game] = (await db.execute(sql`SELECT datum,completed_at "completedAt",modus,lauf,tauben_pro_lauf "taubenProLauf" FROM spiele WHERE id=${id}`)).rows as any[];
  if (!game) return null;
  const players = (await db.execute(sql`SELECT s.id,s.name,sum(t.punkte)::int total FROM spiel_teilnahmen t
    JOIN spieler s ON s.id=t.spieler_id WHERE t.spiel_id=${id} GROUP BY s.id,s.name ORDER BY min(t.id)`)).rows as { id: number; name: string; total: number }[];
  const results = (await db.execute(sql`SELECT spieler_id "spielerId",lauf,taube,maschine,posten,punkte,wiederholt
    FROM ergebnisse WHERE spiel_id=${id} ORDER BY id`)).rows as unknown as SavedResult[];
  return buildScorecard(game, players, results);
}
