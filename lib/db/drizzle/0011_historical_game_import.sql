ALTER TABLE spieler ADD COLUMN IF NOT EXISTS deactivated_at timestamptz;
UPDATE spieler SET deactivated_at=updated_at WHERE NOT aktiv AND deactivated_at IS NULL;
--> statement-breakpoint
CREATE OR REPLACE FUNCTION record_player_deactivation() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF NOT NEW.aktiv AND OLD.aktiv THEN NEW.deactivated_at=now(); END IF;
  RETURN NEW;
END $$;
--> statement-breakpoint
DROP TRIGGER IF EXISTS spieler_record_deactivation ON spieler;
CREATE TRIGGER spieler_record_deactivation BEFORE UPDATE OF aktiv ON spieler
  FOR EACH ROW EXECUTE FUNCTION record_player_deactivation();
--> statement-breakpoint
CREATE OR REPLACE FUNCTION reject_inactive_player_activity()
RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE player_active boolean; cutoff timestamptz;
BEGIN
  SELECT aktiv,deactivated_at INTO player_active,cutoff FROM spieler WHERE id=NEW.spieler_id FOR SHARE;
  IF NOT FOUND THEN
    RAISE EXCEPTION 'player % is unknown',NEW.spieler_id USING ERRCODE='23503';
  END IF;
  IF NOT player_active THEN
    -- This exception is confined to completed, pre-deactivation game history.
    -- Credits, sales, payments and player updates retain the existing boundary.
    IF TG_TABLE_NAME NOT IN ('spiel_teilnahmen','ergebnisse') OR
       current_setting('rangemaster.historical_game_import',true) IS DISTINCT FROM '1' THEN
      RAISE EXCEPTION 'player % is inactive',NEW.spieler_id USING ERRCODE='23503';
    END IF;
    IF NOT EXISTS(SELECT 1 FROM spiele WHERE id=NEW.spiel_id AND abgeschlossen
      AND datum<=cutoff) THEN
      RAISE EXCEPTION 'game is not completed pre-deactivation history for player %',NEW.spieler_id USING ERRCODE='23503';
    END IF;
  END IF;
  RETURN NEW;
END $$;
