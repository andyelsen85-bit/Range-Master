# Local player identity and recovery

Applies to the real ESP32-P4 terminal and portal API, not the emulator.

## Contract

- Each terminal CREATE has a stable `externalId`. New events use 128 random
  bits; retries and firmware upgrades preserve existing event IDs.
- Migration `0017_terminal_player_identity` adds a nullable
  `spieler.terminal_external_id` with a partial unique index. The existing
  deployment initContainer runs it through Drizzle; no API-startup DDL.
  Existing players keep NULL. Historical duplicate players are retained:
  **never merge by name or automatically reassign their games**.
- `POST /api/sync/spieler-neu` trims and validates the name (1–100 characters).
  A new identity returns 201 with portal access disabled; a repeated or
  concurrently repeated identity returns 200 with the same ID and saved name.
  Retrying does not overwrite operator profile changes. The structured server
  audit log records the create identity, player ID and whether it was new.
- The terminal persists the returned `portalId` in the CREATE entry **before**
  remapping. When present, a later cycle retries only local remapping/storage;
  it does not call `spieler-neu` again. A response lost before its ID is known
  necessarily requires another HTTP call; the server upsert returns one player.
- A CREATE remains pending while journal, active checkpoint, credit, sales,
  payment or day-player persistence fails. Its original negative ID is retained
  for retry. The final acknowledgement is checked before removing the entry.
- Released player-update NVS rows are upgraded without discarding CREATE IDs.
  Unreadable rows are retained in NVS and block new local creation instead of
  being overwritten by an empty queue.
  Do not downgrade to older firmware while CREATE entries are pending: older
  firmware cannot read the extended receipt rows.
- The negative-ID allocator survives reboot and never reuses a previously
  remapped local ID. Local creation refuses a full/unwritable outbox and the
  form retains the entered name with an error instead of suggesting success.
- Day-player ADD/REMOVE events remain queued with negative IDs until the
  identity is known, then are remapped and flushed to `DAYP0/1.BIN`. Journal
  remapping also rewrites `ACTIVE0/1.BIN` without changing its gameplay revision.
  Credits, immutable event IDs, player selections, saved lineup, games and
  history follow the central identity-remap path.
- Games with unresolved IDs stay pending. Each affected player is logged once
  per cycle at INFO, with name and reason; sync status shows
  `N Spiele warten auf neue Spieler`.

## Automated checks

```sh
pnpm --filter @workspace/api-server test:local-players
pnpm --filter @workspace/api-server test:sync-contract
python artifacts/firmware/tests/test_local_players.py
python artifacts/firmware/tests/test_game_journal.py
python artifacts/firmware/tests/test_day_players.py
python artifacts/firmware/tests/test_credit_cancellation.py
```

The API tests use a disposable PostgreSQL database, run all migrations twice,
and exercise sequential/concurrent creates, an ignored response, same-name
separate identities, validation, the unique index and portal access defaults.
Firmware tests exercise the actual CREATE state machine and RAM remapping with
receipt/journal/ledger/UI/final-ACK faults, legacy row conversion, ID allocation,
waiting-message deduplication and status text.

## Required physical-terminal evidence — still pending

No terminal or gateway is connected in the development workspace. Native target
builds and deterministic host regressions do not prove the following scenario:

1. Create a local player; add them to the day's players, grant credits and play
   a complete game. Synchronize and retain the serial log.
2. Confirm a single successful `POST /api/sync/spieler-neu`, the INFO
   `player_remap` line, positive player IDs in the game upload, and
   `pending_push ... remaining=0`.
3. In the portal, verify exactly one player for that CREATE identity, with the
   game, credits and day-player registration attached.
4. Repeat with WiFi lost during creation, then reconnect. Multiple HTTP calls
   are permissible if no response was received; they must resolve the same
   `terminal_external_id`, never create another player.
5. On a safe test terminal, inject a local remap/storage failure after receiving
   the ID. Retry/reboot: retain the CREATE receipt and verify no further create
   POST once that receipt is durable. Confirm an open game's checkpoint and
   `DAYP0/1.BIN` also recover with positive IDs.

Keep all unsynced data, NVS and FAT intact. Never erase-flash or format storage
for these checks.
