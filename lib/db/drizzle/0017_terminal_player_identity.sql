ALTER TABLE spieler ADD COLUMN IF NOT EXISTS terminal_external_id text;
--> statement-breakpoint
CREATE UNIQUE INDEX IF NOT EXISTS spieler_terminal_external_id_unique
  ON spieler (terminal_external_id) WHERE terminal_external_id IS NOT NULL;
