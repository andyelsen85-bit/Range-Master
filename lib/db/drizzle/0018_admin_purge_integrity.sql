-- Preserve the identity of existing games before allowing their FK to be nulled.
UPDATE admin_game_edits a
SET before=jsonb_build_object('game',jsonb_build_object('id',g.id,'externalId',g.external_id,'date',g.datum))
             || CASE WHEN jsonb_typeof(a.before)='object' THEN a.before ELSE jsonb_build_object('state',a.before) END,
    after=jsonb_build_object('game',jsonb_build_object('id',g.id,'externalId',g.external_id,'date',g.datum))
             || CASE WHEN jsonb_typeof(a.after)='object' THEN a.after ELSE jsonb_build_object('state',a.after) END
FROM spiele g WHERE a.spiel_id=g.id;
--> statement-breakpoint
ALTER TABLE admin_game_edits
  DROP CONSTRAINT IF EXISTS admin_game_edits_spiel_id_fkey;
--> statement-breakpoint
ALTER TABLE admin_game_edits
  ADD CONSTRAINT admin_game_edits_spiel_id_fkey
  FOREIGN KEY (spiel_id) REFERENCES spiele(id) ON DELETE SET NULL;
