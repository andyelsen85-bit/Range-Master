# Admin purge integrity — deployment and verification

## Migration

`0018_admin_purge_integrity` (journal index 18) runs through the existing Drizzle
initContainer/deployment migration path, **not manual SQL or API-startup DDL**.
It backfills existing game audit snapshots and changes exactly one constraint:

`admin_game_edits_spiel_id_fkey`: `NO ACTION` → `ON DELETE SET NULL`.

The Drizzle schema has the same `onDelete: "set null"`. Existing `before`/`after`
fields are preserved and enriched with `game.id`, `game.externalId`, `game.date`.
New hide/unhide, participant exclusion and shot-edit audits copy this identity
when written. Purge also enriches old audit rows defensively before detaching
them, covering writes from an older server during a rolling deployment.

No purge was executed against development or production user data while making
this fix. No migration was manually applied to either persistent database.

## Foreign-key census

The following was read from the development database's PostgreSQL catalog, not
inferred from TypeScript. Development already had the audit FK set to SET NULL;
the committed schema/migration chain did not, explaining production drift.

```sql
SELECT con.conname, con.conrelid::regclass AS source,
       con.confrelid::regclass AS target, pg_get_constraintdef(con.oid)
FROM pg_constraint con
WHERE con.contype='f'
  AND con.confrelid IN ('spiele'::regclass,'spieler'::regclass)
ORDER BY target,source,con.conname;
```

All 19 references checked:

| Target | Constraint | Delete rule / purge handling |
|---|---|---|
| spiele | admin_game_edits_spiel_id_fkey | SET NULL after 0018; explicitly enrich and detach; never delete audit |
| spiele | ergebnisse_spiel_id_spiele_id_fk | CASCADE; explicitly delete selected results first and count |
| spiele | spiel_teilnahmen_spiel_id_spiele_id_fk | CASCADE; explicitly delete selected participations first and count |
| spiele | spiel_spieler_hidden_spiel_id_fkey | CASCADE; explicitly delete selected exclusions first |
| spiele | queue_entries_spiel_id_fkey | SET NULL; delete selected-day entries before games |
| spiele | queue_games_started_spiel_id_fkey | SET NULL; delete selected-day queue games before games |
| spieler | bill_payments_marked_by_admin_id_spieler_id_fk | RESTRICT; delete bill activity before global player deletion |
| spieler | bill_payments_spieler_id_spieler_id_fk | RESTRICT; same |
| spieler | ergebnisse_spieler_id_spieler_id_fk | NO ACTION; results deleted first |
| spieler | spiel_teilnahmen_spieler_id_spieler_id_fk | NO ACTION; participations deleted first |
| spieler | gameday_maitre_spieler_id_fkey | NO ACTION; clear selected day / all before player deletion |
| spieler | gameday_players_spieler_id_fkey | NO ACTION; same |
| spieler | kredit_events_spieler_id_fkey | NO ACTION; credit activity deleted first |
| spieler | queue_entries_spieler_id_fkey | NO ACTION; queue activity deleted first |
| spieler | sale_events_spieler_id_spieler_id_fk | RESTRICT; sales deleted first |
| spieler | spiel_spieler_hidden_spieler_id_fkey | NO ACTION; exclusions deleted first |
| spieler | spieler_gruppen_mitglieder_spieler_id_fkey | NO ACTION; remove non-admin membership before global deletion; preserve for day |
| spieler | spieler_updates_spieler_id_fkey | NO ACTION; clear updates before global deletion; preserve for day |
| spieler | terminal_restore_authorizations_created_by_fkey | SET NULL; explicitly detach deleted creators, retaining approvals |

No other FK was changed. Purge discovers and ACCESS EXCLUSIVE-locks all
referencing tables plus its operational tables. An unknown dependent row blocks
and rolls back the operation; it is not deleted blindly.

## Scope and retention

- One transaction covers locks, cleanup, counts and the retained `PURGE` audit
  (administrator ID, mode, date and counts). Confirmation stays `PURGE_DAY` /
  `PURGE_ALL`. Every administrator account and its credentials survive global
  reset, including inactive administrators.
- Day ownership for timestamped games is **Europe/Luxembourg**, not UTC.
  Queues use start time, otherwise creation time, plus their linked games;
  entries additionally use signup day. Live snapshots use their linked game or
  validated start timestamp, falling back to update time only if absent.
- Day mode clears the day's games, results, participations, hidden flags,
  credits, sales, bills, queue, daily roster, roster-event receipts, maîtres,
  inventory opening/closing rows, daily report status and terminal manifests.
- Player profiles, shared group membership, profile notifications and
  configuration are not day-owned and remain in day mode. Global mode removes
  non-admin players' membership and all activity before removing those players.
  Group templates, products, prices, API keys and configuration remain.
- Legacy `gameday_event` closing receipts contain only an ID/hash/terminal and
  receipt time, **no business date**. Offline receipt time cannot establish the
  shooting day. Day mode retains these immutable dedupe receipts rather than
  guessing and affecting another day; all actual day-closing state is removed.
  Global mode clears these receipts with terminals disconnected.
- Audit history is never deleted. JSON database failures contain a German
  message, table/constraint when supplied by PostgreSQL, and request ID. The
  request logger records the full error and the same ID; no SQL or parameters
  are exposed to the client. Only authenticated admins can reach this handler.
- The confirmation dialog lists all cleanup counts and its audit number after
  success, rather than disappearing before the administrator can read them.

## Tests and deployment checks

`pnpm --filter @workspace/api-server test:purge` creates a disposable PostgreSQL
database, applies the original chain through 0017, reproduces the blocking audit
FK, upgrades through 0018 and migrates again. It tests:

1. Original audit metadata backfill and the new SET NULL constraint.
2. Real hide/player-exclusion/shot-edit audit writers.
3. Invalid confirmations/calendar dates and non-admin/anonymous requests.
4. A deliberately added unknown FK: 409 JSON naming its table/constraint and
   byte-for-byte rollback of all operational rows and audits.
5. Complete day cleanup, retained audit IDs, unchanged other-day rows (including
   games across the UTC/Luxembourg midnight boundary) and shared membership.
6. Global cleanup with the same dependencies; both active and inactive admin
   credentials, products, audit history and PURGE records survive.

The suite asserts all application queries target its disposable database, closes
test-owned pools and checks for remaining sessions before dropping it normally.
It is included in CI; no deployment or workflow dispatch is triggered here.

After the separately approved deployment, check the deployed commit through
`GET /api/health`, the successful initContainer log, the Drizzle receipt for
0018, and the catalog query above. Only then retry the confirmed
`POST /api/admin/purge` for `2026-10-19` through the admin UI. This retry is
destructive; it has intentionally not been run as part of verification.

### Verification results

- Focused purge suite: all five reported tests pass.
- API and portal TypeScript checks: pass.
- Full migration-chain fresh/repeat/direct-replay/schema-push check: passes;
  both protected-backup abort/recovery scenarios also pass.
- Two broader-suite discrepancies remain outside this fix: the legacy
  manual-Gameday snapshot comparison does not account for public-page columns
  added by later migrations, and the terminal-log suite expects a batch-level
  400 for forbidden diagnostic data while the current endpoint returns 200
  with per-entry rejection. The API suite otherwise reports 37 passing tests.
