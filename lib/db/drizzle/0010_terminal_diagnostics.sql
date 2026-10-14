CREATE TABLE IF NOT EXISTS terminal_log (
  id bigserial PRIMARY KEY,
  terminal_id text NOT NULL,
  entry_id text NOT NULL,
  ts timestamptz NOT NULL,
  type text NOT NULL,
  level text NOT NULL,
  msg text NOT NULL,
  data jsonb NOT NULL DEFAULT '{}'::jsonb,
  received_at timestamptz NOT NULL DEFAULT now(),
  gateway_id text,
  gateway_seq bigint,
  CONSTRAINT terminal_log_terminal_entry_unique UNIQUE(terminal_id,entry_id)
);
--> statement-breakpoint
CREATE UNIQUE INDEX IF NOT EXISTS terminal_log_gateway_seq_unique ON terminal_log(gateway_id,gateway_seq) WHERE gateway_id IS NOT NULL;
--> statement-breakpoint
CREATE INDEX IF NOT EXISTS terminal_log_ts_idx ON terminal_log(ts);
--> statement-breakpoint
CREATE INDEX IF NOT EXISTS terminal_log_terminal_ts_idx ON terminal_log(terminal_id,ts);
--> statement-breakpoint
CREATE TABLE IF NOT EXISTS terminal_game_manifest (
  terminal_id text NOT NULL,
  datum date NOT NULL,
  external_ids jsonb NOT NULL DEFAULT '[]'::jsonb,
  updated_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY(terminal_id,datum)
);
