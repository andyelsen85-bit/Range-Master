ALTER TABLE "spieler" ADD COLUMN "aktiv" boolean DEFAULT true NOT NULL;

CREATE OR REPLACE FUNCTION reject_inactive_player_activity()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
  PERFORM 1
  FROM spieler
  WHERE id = NEW.spieler_id AND aktiv = true
  FOR SHARE;

  IF NOT FOUND THEN
    RAISE EXCEPTION 'player % is inactive or unknown', NEW.spieler_id
      USING ERRCODE = '23503';
  END IF;
  RETURN NEW;
END;
$$;

CREATE TRIGGER spiel_teilnahmen_active_player
  BEFORE INSERT OR UPDATE OF spieler_id ON spiel_teilnahmen
  FOR EACH ROW EXECUTE FUNCTION reject_inactive_player_activity();

CREATE TRIGGER ergebnisse_active_player
  BEFORE INSERT OR UPDATE OF spieler_id ON ergebnisse
  FOR EACH ROW EXECUTE FUNCTION reject_inactive_player_activity();

CREATE TRIGGER kredit_events_active_player
  BEFORE INSERT OR UPDATE OF spieler_id ON kredit_events
  FOR EACH ROW EXECUTE FUNCTION reject_inactive_player_activity();

CREATE TRIGGER sale_events_active_player
  BEFORE INSERT OR UPDATE OF spieler_id ON sale_events
  FOR EACH ROW EXECUTE FUNCTION reject_inactive_player_activity();

CREATE TRIGGER bill_payments_active_player
  BEFORE INSERT OR UPDATE OF spieler_id ON bill_payments
  FOR EACH ROW EXECUTE FUNCTION reject_inactive_player_activity();

CREATE TRIGGER spieler_updates_active_player
  BEFORE INSERT OR UPDATE OF spieler_id ON spieler_updates
  FOR EACH ROW EXECUTE FUNCTION reject_inactive_player_activity();