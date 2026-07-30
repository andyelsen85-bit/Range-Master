-- ============================================================
-- Range-Master — Demo data
-- Safe to run multiple times (skips if already present).
--
-- Usage:
--   kubectl exec -n rangemaster statefulset/postgres -- \
--     psql -U trapmaster -d trapmaster -f /tmp/demo-data.sql
--
-- Or from the host (copy first):
--   kubectl cp scripts/demo-data.sql rangemaster/postgres-0:/tmp/demo-data.sql
--   kubectl exec -n rangemaster statefulset/postgres -- \
--     psql -U trapmaster -d trapmaster -f /tmp/demo-data.sql
-- ============================================================

DO $$ BEGIN
  IF EXISTS (SELECT 1 FROM spieler WHERE id = 100) THEN
    RAISE NOTICE 'Demo data already present — skipping.';
    RETURN;
  END IF;

  -- ----------------------------------------------------------
  -- PLAYERS  (IDs 100-107)
  -- ----------------------------------------------------------
  INSERT INTO spieler (id, name, email, mitglied_nr, portal_aktiv, is_admin) VALUES
    (100, 'Max Mustermann',  'max@example.com',    'WLZ001', true,  false),
    (101, 'Anna Becker',     'anna@example.com',   'WLZ002', true,  false),
    (102, 'Peter Hoffmann',  'peter@example.com',  'WLZ003', true,  false),
    (103, 'Sarah Klein',     'sarah@example.com',  'WLZ004', true,  false),
    (104, 'Thomas Wagner',   'thomas@example.com', 'WLZ005', true,  false),
    (105, 'Maria Schulz',    'maria@example.com',  'WLZ006', false, false),
    (106, 'Klaus Fischer',   'klaus@example.com',  'WLZ007', true,  false),
    (107, 'Julia Braun',     'julia@example.com',  'WLZ008', true,  false);

  PERFORM setval('spieler_id_seq', 200);

  -- ----------------------------------------------------------
  -- GAMES  (IDs 100-109)
  -- Game 109 is intentionally left unfinished (in progress).
  -- ----------------------------------------------------------
  INSERT INTO spiele (id, external_id, datum, modus, lauf, tauben_pro_lauf, abgeschlossen, synced_at) VALUES
    (100, 'demo-001', '2026-05-01 09:00:00+02', 'NORMAL',           1, 9, true,  '2026-05-01 10:30:00+02'),
    (101, 'demo-002', '2026-05-08 09:00:00+02', 'NORMAL',           1, 9, true,  '2026-05-08 10:30:00+02'),
    (102, 'demo-003', '2026-05-15 09:00:00+02', 'HARAKIRI',         1, 9, true,  '2026-05-15 10:30:00+02'),
    (103, 'demo-004', '2026-05-22 09:00:00+02', 'NORMAL',           1, 9, true,  '2026-05-22 10:30:00+02'),
    (104, 'demo-005', '2026-06-05 09:00:00+02', 'NORMAL',           1, 9, true,  '2026-06-05 10:30:00+02'),
    (105, 'demo-006', '2026-06-12 09:00:00+02', 'HARAKIRI_DELAYED', 1, 9, true,  '2026-06-12 10:30:00+02'),
    (106, 'demo-007', '2026-07-03 09:00:00+02', 'CUSTOM_1',         1, 9, true,  '2026-07-03 10:30:00+02'),
    (107, 'demo-008', '2026-07-17 09:00:00+02', 'NORMAL',           1, 9, true,  '2026-07-17 10:30:00+02'),
    (108, 'demo-009', '2026-07-24 09:00:00+02', 'NORMAL',           1, 9, true,  '2026-07-24 10:30:00+02'),
    (109, 'demo-010', '2026-07-30 09:00:00+02', 'NORMAL',           1, 9, false, NULL);

  PERFORM setval('spiele_id_seq', 200);

  -- ----------------------------------------------------------
  -- RESULTS + PARTICIPATIONS
  --
  -- Each row: (spiel_id, spieler_id, start_posten, hits_9chars)
  --   hits: 9-char string, '1' = bird hit (schuss1=true), '0' = miss
  --   Maschine cycles A-H per taube, posten rotates from start_posten.
  --
  -- Player accuracy profiles (rough average per 9 tauben):
  --   100 Max        ~7   good
  --   101 Anna       ~6   medium
  --   102 Peter      ~8   excellent
  --   103 Sarah      ~5   medium-low
  --   104 Thomas     ~7   good
  --   105 Maria      ~6   medium
  --   106 Klaus      ~8+  top shooter
  --   107 Julia      ~4   beginner
  -- ----------------------------------------------------------
  DECLARE
    r        RECORD;
    i        INT;
    hit      BOOL;
    pts      INT;
    mach_arr TEXT[] := ARRAY['A','B','C','D','E','F','G','H'];
  BEGIN
    FOR r IN
      SELECT *
      FROM (VALUES
        -- game 100  (NORMAL — 2026-05-01)
        (100, 100, 1, '110111011'),
        (100, 101, 2, '101100110'),
        (100, 102, 3, '111111101'),
        (100, 103, 4, '100110101'),

        -- game 101  (NORMAL — 2026-05-08)
        (101, 100, 1, '111011110'),
        (101, 101, 2, '110101101'),
        (101, 103, 3, '010101110'),
        (101, 104, 4, '111101011'),
        (101, 105, 5, '011011011'),

        -- game 102  (HARAKIRI — 2026-05-15)
        (102, 102, 1, '111110111'),
        (102, 103, 2, '011001010'),
        (102, 104, 3, '101110011'),
        (102, 106, 4, '111111110'),
        (102, 107, 5, '100100010'),

        -- game 103  (NORMAL — 2026-05-22)
        (103, 100, 1, '011110111'),
        (103, 102, 2, '111011111'),
        (103, 104, 3, '110110101'),
        (103, 106, 4, '111111011'),
        (103, 107, 5, '010100100'),

        -- game 104  (NORMAL — 2026-06-05)
        (104, 100, 1, '111010111'),
        (104, 101, 2, '110110110'),
        (104, 103, 3, '101001101'),
        (104, 107, 4, '010001101'),

        -- game 105  (HARAKIRI_DELAYED — 2026-06-12)
        (105, 101, 1, '011011110'),
        (105, 102, 2, '111111011'),
        (105, 103, 3, '001101100'),
        (105, 106, 4, '111111101'),

        -- game 106  (CUSTOM_1 — 2026-07-03)
        (106, 100, 1, '110111101'),
        (106, 101, 2, '011011011'),
        (106, 102, 3, '111101111'),
        (106, 103, 4, '100110010'),
        (106, 104, 5, '111011101'),
        (106, 106, 6, '111111111'),   -- perfect round
        (106, 107, 7, '100101000'),

        -- game 107  (NORMAL — 2026-07-17)
        (107, 100, 1, '011111011'),
        (107, 102, 2, '110111111'),
        (107, 104, 3, '111011011'),
        (107, 106, 4, '111110111'),

        -- game 108  (NORMAL — 2026-07-24)
        (108, 100, 1, '111110110'),
        (108, 101, 2, '110011110'),
        (108, 103, 3, '011010010'),
        (108, 105, 4, '110101001'),
        (108, 107, 5, '010100001'),

        -- game 109  (NORMAL — in progress, partial shots only)
        (109, 100, 1, '110000000'),
        (109, 101, 2, '100000000'),
        (109, 102, 3, '111000000')

      ) AS t(spiel_id, spieler_id, start_posten, hits)
    LOOP
      pts := 0;

      FOR i IN 1..9 LOOP
        hit := substr(r.hits, i, 1) = '1';
        pts := pts + (CASE WHEN hit THEN 1 ELSE 0 END);

        -- Only insert rows where a shot was actually fired (non-zero char position matters
        -- for the in-progress game 109 — trailing zeros = not yet shot, skip them).
        CONTINUE WHEN r.spiel_id = 109 AND substr(r.hits, i, 1) = '0'
                      AND i > (length(rtrim(r.hits, '0')));

        INSERT INTO ergebnisse
          (spiel_id, spieler_id, lauf, taube, maschine, posten, schuss1, schuss2, punkte, wiederholt)
        VALUES (
          r.spiel_id,
          r.spieler_id,
          1,
          i,
          (mach_arr[((i - 1) % 8) + 1])::maschine,
          ((r.start_posten + i - 2) % 8) + 1,
          hit,
          false,                                       -- schuss2 always false in demo
          CASE WHEN hit THEN 1 ELSE 0 END,
          false
        );
      END LOOP;

      -- Teilnahme row (skip for unfinished game 109 — no total yet)
      IF r.spiel_id <> 109 THEN
        INSERT INTO spiel_teilnahmen (spiel_id, spieler_id, start_posten, punkte, lauf)
        VALUES (r.spiel_id, r.spieler_id, r.start_posten, pts, 1)
        ON CONFLICT DO NOTHING;
      END IF;
    END LOOP;
  END;

  -- ----------------------------------------------------------
  -- CREDIT EVENTS
  -- A mix of GRANT and USE events across recent days.
  -- ----------------------------------------------------------
  INSERT INTO kredit_events (external_id, spieler_id, datum, typ, anzahl) VALUES
    -- Max: 2 credits granted, 1 used today
    ('demo-k-001', 100, '2026-07-28', 'GRANT', 2),
    ('demo-k-002', 100, '2026-07-28', 'USE',   1),
    -- Anna: 1 credit granted and used
    ('demo-k-003', 101, '2026-07-28', 'GRANT', 1),
    ('demo-k-004', 101, '2026-07-28', 'USE',   1),
    -- Peter: 3 credits granted, 1 used (2 remaining)
    ('demo-k-005', 102, '2026-07-30', 'GRANT', 3),
    ('demo-k-006', 102, '2026-07-30', 'USE',   1),
    -- Sarah: 1 granted, 1 used
    ('demo-k-007', 103, '2026-07-30', 'GRANT', 1),
    ('demo-k-008', 103, '2026-07-30', 'USE',   1),
    -- Klaus: 2 credits granted, none used yet
    ('demo-k-009', 106, '2026-07-30', 'GRANT', 2),
    -- Julia: 1 credit from last week
    ('demo-k-010', 107, '2026-07-24', 'GRANT', 1),
    ('demo-k-011', 107, '2026-07-24', 'USE',   1);

  RAISE NOTICE 'Demo data inserted: 8 players, 10 games, results & credits.';
END $$;
