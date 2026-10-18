-- Runs in the existing Drizzle/initContainer transaction. No financial writes.
LOCK TABLE spiel_teilnahmen IN SHARE ROW EXCLUSIVE MODE;
--> statement-breakpoint
LOCK TABLE ergebnisse IN SHARE MODE;
--> statement-breakpoint
CREATE TEMPORARY TABLE rm_round_expected ON COMMIT DROP AS
WITH keys AS (
 SELECT spiel_id,spieler_id,lauf FROM ergebnisse
 UNION
 SELECT t.spiel_id,t.spieler_id,t.lauf FROM spiel_teilnahmen t
 WHERE EXISTS(SELECT 1 FROM ergebnisse e WHERE e.spiel_id=t.spiel_id)
 UNION
 SELECT p.spiel_id,p.spieler_id,r.lauf FROM (
   SELECT spiel_id,spieler_id FROM ergebnisse UNION
   SELECT t.spiel_id,t.spieler_id FROM spiel_teilnahmen t
   WHERE EXISTS(SELECT 1 FROM ergebnisse e WHERE e.spiel_id=t.spiel_id)
 ) p JOIN spiele g ON g.id=p.spiel_id CROSS JOIN LATERAL generate_series(1,g.lauf) r(lauf)
), totals AS (
 SELECT spiel_id,spieler_id,lauf,
   sum(CASE WHEN wiederholt THEN 0 ELSE punkte END)::int punkte
 FROM ergebnisse GROUP BY spiel_id,spieler_id,lauf
), first_clay AS (
 SELECT DISTINCT ON(spiel_id,spieler_id,lauf)
   spiel_id,spieler_id,lauf,posten
 FROM ergebnisse ORDER BY spiel_id,spieler_id,lauf,taube,wiederholt,posten,maschine,id
)
SELECT k.*,COALESCE(f.posten,t.start_posten,(SELECT x.start_posten FROM spiel_teilnahmen x
 WHERE x.spiel_id=k.spiel_id AND x.spieler_id=k.spieler_id ORDER BY x.lauf,x.id LIMIT 1),1)::int start_posten,
 COALESCE(s.punkte,0)::int punkte,t.id existing_id,
 t.punkte old_punkte,t.start_posten old_start_posten
FROM keys k LEFT JOIN totals s USING(spiel_id,spieler_id,lauf)
LEFT JOIN first_clay f USING(spiel_id,spieler_id,lauf)
LEFT JOIN spiel_teilnahmen t USING(spiel_id,spieler_id,lauf);
--> statement-breakpoint
DO $$
DECLARE games integer; players integer; player_games integer; rounds integer;
BEGIN
 -- Retain the existing inactive-player guard's narrowly scoped history rule.
 PERFORM set_config('rangemaster.historical_game_import','1',true);
 WITH logged AS (
   INSERT INTO admin_game_edits(admin_user_id,action,spiel_id,spieler_id,before,after)
   SELECT 0,'REPAIR_ROUND_TOTALS',r.spiel_id,r.spieler_id,
    jsonb_build_object('source','0015_round_participations','total',
      (SELECT COALESCE(sum(t.punkte),0) FROM spiel_teilnahmen t
       WHERE t.spiel_id=r.spiel_id AND t.spieler_id=r.spieler_id),
      'rounds',(SELECT jsonb_agg(jsonb_build_object('lauf',t.lauf,'punkte',t.punkte,'startPosten',t.start_posten) ORDER BY t.lauf)
       FROM spiel_teilnahmen t WHERE t.spiel_id=r.spiel_id AND t.spieler_id=r.spieler_id)),
    jsonb_build_object('total',
      (SELECT sum(x.punkte) FROM rm_round_expected x WHERE x.spiel_id=r.spiel_id AND x.spieler_id=r.spieler_id),
      'rounds',(SELECT jsonb_agg(jsonb_build_object('lauf',x.lauf,'punkte',x.punkte,'startPosten',x.start_posten) ORDER BY x.lauf)
       FROM rm_round_expected x WHERE x.spiel_id=r.spiel_id AND x.spieler_id=r.spieler_id))
   FROM rm_round_expected r
   WHERE existing_id IS NULL OR old_punkte IS DISTINCT FROM punkte OR old_start_posten IS DISTINCT FROM start_posten
   GROUP BY r.spiel_id,r.spieler_id
   RETURNING spiel_id,spieler_id
 ) SELECT count(DISTINCT spiel_id),count(DISTINCT spieler_id),count(*)
 INTO games,players,player_games FROM logged;
 SELECT count(*) INTO rounds FROM rm_round_expected
 WHERE existing_id IS NULL OR old_punkte IS DISTINCT FROM punkte OR old_start_posten IS DISTINCT FROM start_posten;
 INSERT INTO spiel_teilnahmen(spiel_id,spieler_id,lauf,start_posten,punkte)
 SELECT spiel_id,spieler_id,lauf,start_posten,punkte FROM rm_round_expected
 WHERE existing_id IS NULL OR old_punkte IS DISTINCT FROM punkte OR old_start_posten IS DISTINCT FROM start_posten
 ON CONFLICT(spiel_id,spieler_id,lauf) DO UPDATE
 SET punkte=excluded.punkte,start_posten=excluded.start_posten
 WHERE spiel_teilnahmen.punkte IS DISTINCT FROM excluded.punkte
    OR spiel_teilnahmen.start_posten IS DISTINCT FROM excluded.start_posten;
 RAISE NOTICE 'ROUND_TOTAL_REPAIR games=% players=% player_games=% round_rows=%',games,players,player_games,rounds;
END $$;
