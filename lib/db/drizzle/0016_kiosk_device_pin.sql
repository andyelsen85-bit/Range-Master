ALTER TABLE public_page_settings ADD COLUMN IF NOT EXISTS pin_hash text;
ALTER TABLE public_page_settings ADD COLUMN IF NOT EXISTS pin_changed_at timestamptz;
ALTER TABLE public_page_settings ADD COLUMN IF NOT EXISTS pin_epoch integer NOT NULL DEFAULT 1;
--> statement-breakpoint
CREATE TABLE IF NOT EXISTS kiosk_devices (
  id serial PRIMARY KEY,
  token_hash text NOT NULL UNIQUE,
  name text,
  pin_epoch integer NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  last_used_at timestamptz NOT NULL DEFAULT now(),
  last_renewed_at timestamptz NOT NULL DEFAULT now(),
  last_used_ip_hash text,
  revoked_at timestamptz
);
CREATE TABLE IF NOT EXISTS kiosk_pin_limits (
  key text PRIMARY KEY,
  failures integer NOT NULL DEFAULT 0,
  strikes integer NOT NULL DEFAULT 0,
  window_started_at timestamptz NOT NULL DEFAULT now(),
  locked_until timestamptz,
  updated_at timestamptz NOT NULL DEFAULT now()
);
--> statement-breakpoint
ALTER TABLE queue_audit ADD COLUMN IF NOT EXISTS device_id integer REFERENCES kiosk_devices(id);
-- Historical credentials/audit references remain, but no runtime path reads them.
UPDATE kiosk_tokens SET revoked_at=COALESCE(revoked_at,now()) WHERE revoked_at IS NULL;
