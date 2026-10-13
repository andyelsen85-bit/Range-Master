-- Development migration; apply as one transaction. Never run startup-time DDL.
ALTER TABLE spieler ADD COLUMN IF NOT EXISTS is_maitre_de_tir boolean NOT NULL DEFAULT false;
ALTER TABLE spiele ADD COLUMN IF NOT EXISTS completed_at timestamptz;
UPDATE spiele SET completed_at=datum WHERE abgeschlossen AND completed_at IS NULL;
ALTER TABLE terminal_config_backups ADD COLUMN IF NOT EXISTS backup_date date;
UPDATE terminal_config_backups SET backup_date=(created_at AT TIME ZONE 'Europe/Luxembourg')::date WHERE backup_date IS NULL;
ALTER TABLE terminal_config_backups ALTER COLUMN backup_date SET NOT NULL;
ALTER TABLE terminal_config_backups ALTER COLUMN backup_date SET DEFAULT (now() AT TIME ZONE 'Europe/Luxembourg')::date;
LOCK TABLE terminal_config_backups, terminal_restore_authorizations IN SHARE ROW EXCLUSIVE MODE;
-- Strict daily limit: never revoke, overwrite or delete a protected snapshot.
-- Multiple protected legacy snapshots on one day require explicit admin resolution.
DO $$ BEGIN
  IF EXISTS (
    SELECT 1 FROM terminal_config_backups b
    WHERE b.revoked_at IS NULL AND EXISTS(
      SELECT 1 FROM terminal_restore_authorizations a WHERE a.backup_id=b.id
      AND a.revoked_at IS NULL AND (a.used_at IS NULL OR a.expires_at>now()))
    GROUP BY b.terminal_id,b.backup_date HAVING count(*)>1
  ) THEN RAISE EXCEPTION 'Multiple restore-protected backups on one terminal/day: resolve approvals before strict daily cleanup'; END IF;
END $$;
DO $$ DECLARE removed integer; BEGIN
  WITH ranked AS (
    SELECT b.id, row_number() OVER (PARTITION BY terminal_id,backup_date ORDER BY
      EXISTS(SELECT 1 FROM terminal_restore_authorizations a WHERE a.backup_id=b.id
        AND a.revoked_at IS NULL AND (a.used_at IS NULL OR a.expires_at>now())) DESC,
      created_at DESC,id DESC) AS rank
    FROM terminal_config_backups b WHERE revoked_at IS NULL
  ) DELETE FROM terminal_config_backups b USING ranked r WHERE b.id=r.id AND r.rank>1
    AND NOT EXISTS(SELECT 1 FROM terminal_restore_authorizations a WHERE a.backup_id=b.id
      AND a.revoked_at IS NULL AND (a.used_at IS NULL OR a.expires_at>now()))
    AND b.id <> (SELECT x.id FROM terminal_config_backups x WHERE x.terminal_id=b.terminal_id
      AND x.revoked_at IS NULL ORDER BY x.created_at DESC,x.id DESC LIMIT 1);
  GET DIAGNOSTICS removed=ROW_COUNT; RAISE NOTICE 'Daily backup cleanup removed % snapshots',removed;
END $$;
-- The latest snapshot may share a day with a protected older snapshot. Keeping
-- both would violate the user-selected strict limit: fail closed rather than delete either.
CREATE UNIQUE INDEX IF NOT EXISTS terminal_config_backups_daily_unique
  ON terminal_config_backups(terminal_id,backup_date) WHERE revoked_at IS NULL;
CREATE TABLE IF NOT EXISTS ammo_inventory_day (
 id serial PRIMARY KEY, datum date NOT NULL, caliber integer NOT NULL CHECK(caliber IN(12,20)),
 opening_count integer CHECK(opening_count BETWEEN 0 AND 1000000), opening_at timestamptz,
 opening_source_terminal text, opening_taken_over boolean DEFAULT false,
 closing_count integer CHECK(closing_count BETWEEN 0 AND 1000000), closing_at timestamptz,
 closing_source_terminal text, closing_note text, UNIQUE(datum,caliber));
CREATE TABLE IF NOT EXISTS gameday_maitre (
 id serial PRIMARY KEY, datum date NOT NULL, spieler_id integer NOT NULL REFERENCES spieler(id), UNIQUE(datum,spieler_id));
CREATE TABLE IF NOT EXISTS gameday_event (
 external_id text PRIMARY KEY, terminal_id text NOT NULL, payload_hash text NOT NULL, created_at timestamptz NOT NULL DEFAULT now());
CREATE TABLE IF NOT EXISTS daily_report_settings (id integer PRIMARY KEY DEFAULT 1 CHECK(id=1), enabled boolean NOT NULL DEFAULT false);
CREATE TABLE IF NOT EXISTS daily_report_recipient (id serial PRIMARY KEY,email text NOT NULL UNIQUE, active boolean NOT NULL DEFAULT true,created_at timestamptz NOT NULL DEFAULT now());
CREATE TABLE IF NOT EXISTS daily_report_log (
 datum date PRIMARY KEY,status text NOT NULL,sent_at timestamptz,attempts integer NOT NULL DEFAULT 0,
 error text,recipients_snapshot jsonb,data_hash text,first_attempt_at timestamptz NOT NULL DEFAULT now(),last_attempt_at timestamptz NOT NULL DEFAULT now());
CREATE TABLE IF NOT EXISTS public_page_settings (id integer PRIMARY KEY DEFAULT 1 CHECK(id=1),
 enabled boolean NOT NULL DEFAULT false,name_mode text NOT NULL DEFAULT 'INITIAL' CHECK(name_mode IN('FULL','INITIAL')));
INSERT INTO daily_report_settings(id) VALUES(1) ON CONFLICT DO NOTHING;
INSERT INTO public_page_settings(id) VALUES(1) ON CONFLICT DO NOTHING;
