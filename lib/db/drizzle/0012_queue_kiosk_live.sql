CREATE TABLE IF NOT EXISTS kiosk_tokens (
 id serial PRIMARY KEY, name text NOT NULL, token_hash text NOT NULL UNIQUE,
 created_at timestamptz NOT NULL DEFAULT now(), created_by integer NOT NULL,
 last_used_at timestamptz, last_used_ip_hash text, revoked_at timestamptz
);
--> statement-breakpoint
ALTER TABLE public_page_settings ADD COLUMN IF NOT EXISTS queue_enabled boolean NOT NULL DEFAULT false;
ALTER TABLE public_page_settings ADD COLUMN IF NOT EXISTS groups_enabled boolean NOT NULL DEFAULT true;
ALTER TABLE public_page_settings ADD COLUMN IF NOT EXISTS show_ranking boolean NOT NULL DEFAULT false;
ALTER TABLE public_page_settings ADD COLUMN IF NOT EXISTS max_queue_games integer NOT NULL DEFAULT 6;
ALTER TABLE public_page_settings ADD COLUMN IF NOT EXISTS enabled_modes text[] NOT NULL DEFAULT ARRAY['NORMAL']::text[];
--> statement-breakpoint
CREATE TABLE IF NOT EXISTS spieler_gruppen (
 id serial PRIMARY KEY, name text NOT NULL, created_at timestamptz NOT NULL DEFAULT now(),
 updated_at timestamptz NOT NULL DEFAULT now(), deleted_at timestamptz
);
CREATE TABLE IF NOT EXISTS spieler_gruppen_mitglieder (
 gruppe_id integer NOT NULL REFERENCES spieler_gruppen(id) ON DELETE CASCADE,
 spieler_id integer NOT NULL REFERENCES spieler(id), PRIMARY KEY(gruppe_id,spieler_id)
);
CREATE TABLE IF NOT EXISTS queue_games (
 id serial PRIMARY KEY, modus modus NOT NULL DEFAULT 'NORMAL', position integer NOT NULL,
 status text NOT NULL DEFAULT 'OPEN' CHECK(status IN ('OPEN','LOCKED','STARTED','DONE','CANCELLED')),
 created_at timestamptz NOT NULL DEFAULT now(), locked_at timestamptz, started_at timestamptz,
 terminal_id text, started_external_id text,
 started_spiel_id integer REFERENCES spiele(id) ON DELETE SET NULL,
 source text NOT NULL DEFAULT 'WEB' CHECK(source IN ('WEB','TERMINAL','ADMIN'))
);
CREATE UNIQUE INDEX IF NOT EXISTS queue_started_external ON queue_games(started_external_id) WHERE started_external_id IS NOT NULL;
CREATE TABLE IF NOT EXISTS queue_entries (
 id serial PRIMARY KEY, queue_game_id integer NOT NULL REFERENCES queue_games(id) ON DELETE CASCADE,
 spieler_id integer NOT NULL REFERENCES spieler(id), group_id integer REFERENCES spieler_gruppen(id) ON DELETE SET NULL,
 modus text NOT NULL, signup_at timestamptz NOT NULL DEFAULT now(), signed_by_ip_hash text,
 status text NOT NULL DEFAULT 'OPEN' CHECK(status IN ('OPEN','TAKEN','DONE','CANCELLED')),
 cancel_reason text, spiel_id integer REFERENCES spiele(id) ON DELETE SET NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS queue_one_active_player ON queue_entries(spieler_id) WHERE status IN ('OPEN','TAKEN');
CREATE TABLE IF NOT EXISTS queue_audit (
 id serial PRIMARY KEY, at timestamptz NOT NULL DEFAULT now(), action text NOT NULL,
 spieler_id integer, entry_id integer, group_id integer, ip_hash text, token_id integer,
 details jsonb NOT NULL DEFAULT '{}'::jsonb
);
--> statement-breakpoint
ALTER TABLE spiele ADD COLUMN IF NOT EXISTS hidden_at timestamptz;
ALTER TABLE spiele ADD COLUMN IF NOT EXISTS hidden_by integer;
ALTER TABLE spiele ADD COLUMN IF NOT EXISTS edited_at timestamptz;
ALTER TABLE spiele ADD COLUMN IF NOT EXISTS edited_by integer;
CREATE TABLE IF NOT EXISTS spiel_spieler_hidden (
 spiel_id integer NOT NULL REFERENCES spiele(id) ON DELETE CASCADE,
 spieler_id integer NOT NULL REFERENCES spieler(id), hidden_at timestamptz NOT NULL DEFAULT now(),
 hidden_by integer NOT NULL, PRIMARY KEY(spiel_id,spieler_id)
);
CREATE TABLE IF NOT EXISTS admin_game_edits (
 id serial PRIMARY KEY, at timestamptz NOT NULL DEFAULT now(), admin_user_id integer NOT NULL,
 action text NOT NULL, spiel_id integer REFERENCES spiele(id), spieler_id integer,
 before jsonb, after jsonb
);
CREATE TABLE IF NOT EXISTS live_game_state (
 terminal_id text PRIMARY KEY, seq bigint NOT NULL CHECK(seq>=0),
 state text NOT NULL CHECK(state IN ('RUNNING','FINISHED','ABORTED')),
 payload jsonb NOT NULL, updated_at timestamptz NOT NULL DEFAULT now()
);
--> statement-breakpoint
CREATE OR REPLACE VIEW v_teilnahmen_visible AS SELECT t.* FROM spiel_teilnahmen t
 JOIN spiele g ON g.id=t.spiel_id WHERE g.hidden_at IS NULL
 AND NOT EXISTS(SELECT 1 FROM spiel_spieler_hidden h WHERE h.spiel_id=t.spiel_id AND h.spieler_id=t.spieler_id);
CREATE OR REPLACE VIEW v_ergebnisse_visible AS SELECT e.* FROM ergebnisse e
 JOIN spiele g ON g.id=e.spiel_id WHERE g.hidden_at IS NULL
 AND NOT EXISTS(SELECT 1 FROM spiel_spieler_hidden h WHERE h.spiel_id=e.spiel_id AND h.spieler_id=e.spieler_id);
CREATE OR REPLACE VIEW v_spiele_visible AS SELECT g.* FROM spiele g WHERE g.hidden_at IS NULL
 AND EXISTS(SELECT 1 FROM v_teilnahmen_visible t WHERE t.spiel_id=g.id);
