CREATE TABLE IF NOT EXISTS gameday_players (
 datum date NOT NULL,
 spieler_id integer NOT NULL REFERENCES spieler(id),
 added_at timestamptz NOT NULL,
 removed_at timestamptz,
 terminal_id text NOT NULL,
 occurred_at timestamptz NOT NULL,
 external_id text NOT NULL,
 PRIMARY KEY(datum,spieler_id)
);
--> statement-breakpoint
CREATE TABLE IF NOT EXISTS gameday_player_events (
 external_id text PRIMARY KEY,
 payload_hash text NOT NULL,
 payload jsonb NOT NULL,
 received_at timestamptz NOT NULL DEFAULT now()
);
--> statement-breakpoint
CREATE OR REPLACE VIEW v_queue_eligibility AS
 SELECT s.id,s.name,(d.spieler_id IS NOT NULL) AS registered,
 COALESCE(c.available,0)::int AS available
 FROM spieler s
 LEFT JOIN gameday_players d ON d.spieler_id=s.id AND d.removed_at IS NULL
   AND d.datum=(now() AT TIME ZONE 'Europe/Luxembourg')::date
 LEFT JOIN LATERAL (
  SELECT sum(CASE WHEN e.typ='GRANT' THEN e.anzahl ELSE -e.anzahl END) AS available
  FROM kredit_events e WHERE e.spieler_id=s.id
   AND e.datum=(now() AT TIME ZONE 'Europe/Luxembourg')::date
   AND e.created_at>COALESCE((SELECT max(p.paid_at) FROM bill_payments p
     WHERE p.spieler_id=s.id AND p.datum=e.datum),'-infinity'::timestamptz)
 ) c ON true WHERE s.aktiv;
