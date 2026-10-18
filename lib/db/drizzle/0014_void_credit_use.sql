ALTER TYPE kredit_typ ADD VALUE IF NOT EXISTS 'VOID_USE';
--> statement-breakpoint
ALTER TABLE kredit_events ADD COLUMN IF NOT EXISTS voids_external_id text;
--> statement-breakpoint
ALTER TABLE kredit_events ADD CONSTRAINT kredit_events_voids_external_id_unique UNIQUE(voids_external_id);
--> statement-breakpoint
-- Text comparisons deliberately avoid using a newly added enum literal before
-- the migrator's enclosing transaction commits.
ALTER TABLE kredit_events ADD CONSTRAINT kredit_events_void_shape CHECK (
 (typ::text='VOID_USE' AND anzahl=1 AND voids_external_id IS NOT NULL
  AND voids_external_id<>external_id AND occurred_at IS NULL
  AND price_revision_id IS NULL AND unit_price_cents IS NULL)
 OR (typ::text IN('GRANT','USE') AND voids_external_id IS NULL)
);
--> statement-breakpoint
CREATE OR REPLACE FUNCTION validate_credit_void() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE ref kredit_events%ROWTYPE; lock_id text;
BEGIN
 FOR lock_id IN SELECT x FROM unnest(ARRAY[NEW.external_id,NEW.voids_external_id]) x
   WHERE x IS NOT NULL ORDER BY x LOOP
  PERFORM pg_advisory_xact_lock(hashtextextended('credit-event:'||lock_id,0));
 END LOOP;
 IF NEW.typ::text='VOID_USE' THEN
  SELECT * INTO ref FROM kredit_events WHERE external_id=NEW.voids_external_id;
  IF FOUND AND (ref.typ::text<>'USE' OR ref.spieler_id<>NEW.spieler_id
    OR ref.datum<>NEW.datum OR ref.anzahl<>1) THEN
   RAISE EXCEPTION 'VOID_USE must reference one USE of the same player and date' USING ERRCODE='23514';
  END IF;
 ELSE
  IF EXISTS(SELECT 1 FROM kredit_events v WHERE v.voids_external_id=NEW.external_id
   AND (NEW.typ::text<>'USE' OR v.spieler_id<>NEW.spieler_id OR v.datum<>NEW.datum OR NEW.anzahl<>1)) THEN
   RAISE EXCEPTION 'USE does not match its previously received VOID_USE' USING ERRCODE='23514';
  END IF;
 END IF;
 RETURN NEW;
END $$;
--> statement-breakpoint
CREATE TRIGGER kredit_events_validate_void BEFORE INSERT OR UPDATE ON kredit_events
 FOR EACH ROW EXECUTE FUNCTION validate_credit_void();
--> statement-breakpoint
CREATE OR REPLACE VIEW v_effective_kredit_events AS
 SELECT k.* FROM kredit_events k
 WHERE k.typ::text IN('GRANT','USE') AND NOT EXISTS(
  SELECT 1 FROM kredit_events v WHERE v.typ::text='VOID_USE'
   AND v.voids_external_id=k.external_id AND v.spieler_id=k.spieler_id
   AND v.datum=k.datum AND k.typ::text='USE'
 );
--> statement-breakpoint
CREATE OR REPLACE VIEW v_queue_eligibility AS
 SELECT s.id,s.name,(d.spieler_id IS NOT NULL) AS registered,
 COALESCE(c.available,0)::int AS available
 FROM spieler s
 LEFT JOIN gameday_players d ON d.spieler_id=s.id AND d.removed_at IS NULL
   AND d.datum=(now() AT TIME ZONE 'Europe/Luxembourg')::date
 LEFT JOIN LATERAL (
  SELECT sum(CASE WHEN e.typ::text='GRANT' THEN e.anzahl ELSE -e.anzahl END) AS available
  FROM v_effective_kredit_events e WHERE e.spieler_id=s.id
   AND e.datum=(now() AT TIME ZONE 'Europe/Luxembourg')::date
   AND e.created_at>COALESCE((SELECT max(p.paid_at) FROM bill_payments p
     WHERE p.spieler_id=s.id AND p.datum=e.datum),'-infinity'::timestamptz)
 ) c ON true WHERE s.aktiv;
