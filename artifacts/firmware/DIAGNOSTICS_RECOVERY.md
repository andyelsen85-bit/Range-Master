# Terminal diagnostics and completed-game recovery

## Status: partial implementation — not approved for flashing

The portal, database/API support, terminal log outbox and completed-game journal
are implemented. Host tests are not an ESP32 target build or a power-loss test.
The remaining software and hardware acceptance work below must be completed
before replacing installed firmware.

## Implemented

- Admin pages for terminal logs and game reconciliation. German filters,
  search, JSON details, pagination and CSV export; data-loss warnings appear
  first. Daily latency summaries use exact server-side median/P95/max values.
- Log ingestion acknowledges exact entry IDs, deduplicates terminal entries and
  gateway sequence IDs, checks payload size, rejects credential-bearing fields,
  limits requests and retains at most seven days / 50,000 entries per terminal.
- Normal deployment migrations `0010` and `0011` register the diagnostics schema
  and historical inactive-player game-import boundary. No startup DDL.
- Terminal RAM log queue: 300 entries. The separate diagnostics NVS namespace
  retains six critical entries, within the requested maximum of twenty and the
  board's small NVS partition. Ordinary log writes do not use the game journal.
  Critical persistence runs in a background task; immediate power-cut behavior
  still requires the physical tests below.
- Completed games have checksummed, versioned FAT records, temporary-file
  recovery and exact per-game acknowledgement. Unsynced records do not expire.
  Accepted records remain recoverable for seven days. The queue has 200 slots;
  new games are blocked when the archive is unavailable/full, and occupancy
  above 80% warns the operator.
- FAT formatting is prohibited once the partition contains durable game data.
  Never treat that partition as a disposable cache.
- Local-player games remain queued. The API rejects unknown players per game,
  rather than rejecting unrelated games. Inactive players' completed historical
  games are accepted only through the transaction-scoped historical import
  boundary; new credit/sale/payment activity remains prohibited.
- Disk identity remapping precedes acknowledgement of the local-player update.
  A RAM remap table also translates games finishing during that transition.
- Seven-day archive manifests detect missing portal games. The settings action
  requeues original external IDs rather than generating duplicate game IDs.
- LVGL and short RAM commits share a mutex. Sync does not reset/disable touch or
  create a touch-intercepting overlay. Commits defer while input/scrolling is
  active; publication skips unchanged content and preserves supported form and
  scroll state. Player selection and row actions use stable IDs.
- WiFi power-save is disabled after each connection, with retry on failure.
  Idle nonce prefetch has a single 150 ms attempt and no refresh before the
  gateway nonce lifetime expires, avoiding unnecessary live nonce slots.
  Transport failures retain the same signed fire request; ambiguity must never
  authorize a fresh nonce.

## Remaining software

1. Complete per-dataset sync diagnostics: UTC start/end, each dataset's HTTP
   outcome, durations, pending/acknowledged counts and aggregate commit timing.
   The current cycle summary is not that full report.
2. Complete coherent whole-cycle dataset staging/publication. The current
   bounded RAM windows and final publication generation do not yet constitute
   one atomic replacement of every dataset.
3. Implement the gateway's 50-entry monotonic trace ring, authenticated trace
   retrieval and terminal forwarding/deduplication.
4. Implement a persistent authenticated fire transport through the same gateway
   nonce/admission/counter/radio core. Current fire requests still use HTTP
   connections. Add connection-bound response authentication if using WebSocket.
5. Complete t0–t4, first-byte, RSSI and rolling-60 latency measurements and the
   full quiet-period policy. Current terminal fire traces are only basic
   preparation/total-duration summaries.

## Checks and acceptance

Software checks:

```sh
pnpm --filter @workspace/api-spec run codegen
pnpm --filter @workspace/api-server run typecheck
pnpm --filter @workspace/api-server test
pnpm --filter @workspace/portal run typecheck
node --test lib/db/tests/migrations.test.mjs
python artifacts/firmware/tests/test_game_journal.py
```

The archive test compiles the real journal/log modules against SDK shims and
uses separate processes and file-backed NVS. It covers restart, checksums,
identity remapping, exact log acknowledgements, requeue and complete temporary
files. It does not reproduce flash-controller/FAT power-loss behavior.

Required before flashing:

- Full project build with the pinned ESP-IDF 5.3.5 toolchain and strict warnings;
  verified GPIO/build-time provisioning must remain private and unchanged.
- Power cuts during game write, player-ID remap, portal acceptance, local ACK
  persistence and critical-log persistence. Confirm exact external IDs and no
  silent deletion, including a full 200-game queue.
- On a disconnected test bench, measure 100 single/doublette requests,
  interrupted responses and identical retries. Record actual median/P95/max
  (targets: median under 300 ms, P95 under 650 ms), rather than estimated values.
- Continue scoring, scrolling and typing during sync. Measure short commits and
  total blocked time; confirm no lost taps or selection/result resets.

Do not erase NVS, format FAT, reset replay counters, publish or flash as a
substitute for these tests.
