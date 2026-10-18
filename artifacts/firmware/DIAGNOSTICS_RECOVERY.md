# Terminal diagnostics and completed-game recovery

## Active Harakiri games

`/fatfs/active0.bin` and `/fatfs/active1.bin` are durable game data, **not cache**.
Do not delete them during cache maintenance, format FAT, or erase NVS to recover
an open game. Each bounded slot is 8,084 bytes (8,192 allocated), with version,
generation and checksum. The newest valid slot wins; interrupted replacements
retain the previous slot and emit a warning. If neither slot can be validated,
new starts are blocked rather than silently generating another sequence.

After reboot inspect the last launch before scoring/skipping or deliberately
repeating it. A restart never authorizes a duplicate FIRE by itself. A verified
finished journal record takes precedence over stale active state; an unreadable
journal is not treated as proof that a game never finished. Configuration restore
is refused while a Harakiri game is open. Finish/cancel the active game before
installing firmware that changes its checkpoint format.

Host checks: `python3 artifacts/firmware/tests/test_harakiri_players.py` exercises
both Harakiri modes, all valid active-machine subsets, 2–6 players, mid-pair
restart, scoring, repeat/skip/undo, failed/interrupted writes, and terminal
scorecard alignment under ASan/UBSan. Physical power-cut timing remains a
hardware check; none of these tests flashes or erases a terminal.

## Status: integrated software — hardware acceptance required

Portal/API diagnostics, recovery, coherent sync publication and the fire
transport are implemented together. Native builds and host regression tests
do not prove physical relay latency or flash-controller power-loss behavior.
The hardware acceptance work below remains mandatory before installation.

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
  Accepted records remain recoverable for up to seven days, capped at the
  newest 20 accepted games and 200 total journal slots (pending always wins).
  The pending queue has 200 slots;
  new games are blocked when the archive is unavailable/full, and occupancy
  above 80% warns the operator.
- FAT formatting is prohibited once the partition contains durable game data.
  Never treat that partition as a disposable cache.

### Full FAT: diagnosis and safe recovery

The previous seven-day accepted archive had no count cap: a busy terminal or
an unset clock could accumulate unlimited acknowledged records. Failed journal
writes could also leave partial files. Both growth paths are now bounded.
Failed incomplete writes are removed; complete checksummed journal temporaries
survive failed fsync/close and remain protected/recoverable, never treated as cache.
The actual contents of an affected physical terminal are not available to a
host build; its next boot prints a once-per-mount census of every `/fatfs` file,
size/cluster allocation, file count and byte sum. It also prints usable/free
capacity, raw-minus-usable WL/FAT overhead, every cache section size,
`sizeof(FinishedGame)` and journal `Record` size/capacity. Unknown files are
reported but never treated as disposable.

The current structures budget 7,052 bytes per `FinishedGame`, 7,072 per journal
record, rounded to 8,192 on FAT. The 200 slots plus two atomic-replacement
scratch records need **1,654,784 bytes** (about 16.8% of 9,842,688 usable bytes).
All cache sections, including metadata and the 20-game history, need
**364,544 allocated bytes** (about 3.7%). Runtime limits require journal ≤50%,
cache ≤25%, and ≥20% free, with additional journal write scratch reserved.
The boot census supplies the exact sizes for the installed build.

Rebuildable snapshots use a single copy: the old section is discarded before
writing its replacement; failed writes remove temporary files. A missing
snapshot invalidates its old revision token on reload, forcing a portal pull.
Journal atomic replacements still preserve complete recoverable copies; a
failed remap/requeue stops at the first write failure rather than leaving
one temp file per game. Only acknowledged history can expire or be evicted;
unsynced games never expire or get evicted.

When full/unwritable, cleanup requires both RAM pending count zero AND a
read-only, checksum/version-verified scan proving no unacknowledged journal
records. Corrupt, unreadable or incomplete records fail closed, including
`.part`/`.bak` evidence. Under the cache and journal locks it removes ONLY the
known cache `.bin`/`.bak`/`.tmp` files and probe file, then remounts WITHOUT
formatting. Journal records, unknown files, NVS and replay counters survive.
The old `fat_layout` marker cannot veto this safe cache-only recovery.
Recovery/refusal is a WARN in serial output and a terminal log entry.

A readable-but-unwritable mount remains available for journal recovery/upload;
optional cache health no longer prevents journal initialization. With pending
games, no cache cleanup is performed and new games remain blocked until safe
space is available. An exact portal ACK can release ONLY its acknowledged
record if ENOSPC prevents retaining an archive copy, allowing a full journal
to drain without deleting any game awaiting acknowledgement.

**Einstellungen → System → Speicher bereinigen** first checks and displays the
pending-game count, then asks for confirmation. Pending/uncertain journal data
refuses cleanup; the worker rechecks at execution time and serializes against
writers. A running sync must finish first. Filesystem work runs off the LVGL
thread. Start-screen warnings explain that games are preserved and must be
synchronized. No erase-flash, FAT format or NVS reset is part of this procedure.

Host checks compile the real journal/cache/log modules together with a
quota-backed filesystem shim: full-cache healing, pending on disk with RAM
empty, corrupt partial refusal, exact-ACK ENOSPC draining, NVS sentinel
preservation, 250 acknowledged games capped to 20, and 200 pending records.
Separate cache tests cover 512/4096-byte WL geometry, census, failed writes,
single-copy replacement and budget enforcement. These do not replace physical
flash/power-cut acceptance tests.
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

## Integrated sync and fire implementation

- Sync stages validated roster, products, sales, credits, inventory, bills and
  history privately. One final RAM boundary applies the eligible baselines,
  replaying current local outboxes; persistence and callback destruction happen
  outside it. A rejected game does not prevent other games or datasets syncing.
  Critical outbox ACK/identity changes retain their own short, durable windows.
- The compact sync report includes UTC start/end, outcome, total commit time
  and per-dataset tuples `[startOffsetMs,endOffsetMs,httpStatus,pushed,pending]`.
  Aliases are `p` players, `pr` products, `v` sales, `k` credits, `pay` payments,
  `i` inventory, `b` bills, `g` games, `h` history and `bk` backup. Offsets are
  relative to the cycle start; zero status means no HTTP response was received.
  Count successful ACKs directly, not by subtracting concurrently changing queues.
- The existing authenticated WebSocket carries FIRE with the original nonce
  and action HMAC. Signed progress/final replies bind both connection challenges,
  action nonce, status and exact body. HTTP fallback uses the SAME request and
  admission/radio/counter path. At most three sends total, never a fresh action
  after an ambiguous send. Only an unambiguous expired-prefetch 401 permits a
  fresh inline nonce, within that same budget.
- Gateway traces retain 50 entries with authenticated `/trace?after=...`.
  Sequence ranges are reserved separately in NVS, surviving reboot without
  resetting radio counters. The terminal forwards only whitelisted fields
  from a separate low-priority worker; portal dedupe uses gateway ID and seq.
- FIRE captures t0 press, t1 nonce ready, t2 send, t3 first response and t4
  completion, RSSI, retries, HTTP result and ACK. Settings show last-50
  min/median/P95/max and **Messwerte löschen**. These measure press-to-response,
  NOT physical press-to-relay-pulse latency (pairs include their configured delay).
- Prefetch is scoped to URL/key, consumed once, prepared after completion and
  refreshed while idle. Health checks stay out of the two-second FIRE quiet
  period; automatic health attempts are bounded to 150 ms. Slow trace retrieval
  does not occupy the FIRE worker.
- The pinned Hosted slave already initializes `WIFI_PS_NONE` and exposes the
  host's SetPs RPC. Host reconnect also explicitly disables power saving.

## Checks and acceptance

### P4 chip identity and legacy UNKNOWN records

All identity-bearing requests now use the P4 factory eFuse MAC, read with
`esp_efuse_mac_get_default`, formatted as `ESP32-P4-XXXXXXXXXXXX` (uppercase).
The WiFi STA MAC is not a valid P4 identity source: WiFi is on the C6.
Failed reads, zero/all-FF MACs and insufficient output buffers return failure
with an empty output. There is no shared or random fallback. Backup, restore,
inventory, diagnostics and game-manifest callers refuse submission on failure.
HTTP session creation also refuses, making full/billing sync fail visibly
with “Chip-ID nicht lesbar; Übertragung gesperrt”. Credentials, local external
event/game IDs, NVS and FAT are not reset or regenerated.

**Legacy policy: retention only, no terminal-ID alias and no automatic rewrite.**
`ESP32-P4-UNKNOWN` could refer to several physical devices and cannot safely
be assigned to one chip or API key:

- Existing configuration backups remain under the old ID, under the existing
  retention/protection rules. New per-chip backups are separate records. An
  administrator can explicitly select a verified old backup and authorize it
  for the real replacement ID plus that replacement's active terminal key.
  Previous snapshot contents already overwritten by the shared-ID daily
  collision cannot be reconstructed by this firmware fix.
- Existing restore approvals keep their exact old target/key binding. They
  are never silently retargeted or consumed as a result of the ID change.
  Approval for `UNKNOWN` cannot be used by the new chip ID; revoke/reissue it
  for the correct chip and key. The normal reauthorization action revokes the
  selected backup's previous unused approvals. New `UNKNOWN` targets are refused.
- Existing inventory source fields, immutable event receipts, billing rows,
  logs and manifests retain their original attribution. No migration merges,
  relabels or deletes them. Log filtering/export still accepts the historical
  `UNKNOWN` value; normal retention policies continue to apply.
- A queued inventory/gameday event may have been applied before upgrade but
  lost its ACK. The portal accepts an **ACK-only duplicate** when its external
  ID and complete payload hash match the old receipt with ONLY the terminal
  ID substituted back to `UNKNOWN`, and the incoming ID is a concrete P4 ID.
  It neither reapplies stock/maîtres nor rewrites the old receipt/source.
  Any content mismatch or identity change between two concrete terminals still
  conflicts; the terminal retains the event rather than silently dropping it.
- Game, credit, sale and payment idempotency remains based on the existing
  immutable external IDs and authenticated terminal key, not a renamed MAC.
  No financial or game history is reassigned by this fix.

The portal rejects new `UNKNOWN` identity-bearing writes/restore targets while
keeping historical records readable. Deploy the matching API changes before
updating terminals that may have an unacknowledged legacy inventory receipt.

Repeated journal-unavailable/load-failed messages share a mutex-protected,
monotonic hourly gate in the diagnostics queue: first occurrence per boot,
then no more than one per hour across startup, sync and start-game variants.
SNTP/timezone changes do not reopen the window. Other errors remain unaffected.
Identity-read failures have a separate hourly gate.

Software checks:

```sh
pnpm --filter @workspace/api-spec run codegen
pnpm --filter @workspace/api-server run typecheck
pnpm --filter @workspace/api-server test
pnpm --filter @workspace/portal run typecheck
node --test lib/db/tests/migrations.test.mjs
python artifacts/firmware/tests/test_game_journal.py
python artifacts/firmware/tests/test_sync_batch.py
python artifacts/firmware/tests/test_fire_link.py
```

Build the full ESP32-P4 target with ESP-IDF 5.3.5 and the resolved
managed-component lockfile. The OTA-only table has `ota_0` at `0x20000` and
`ota_1` at `0x350000`, each `0x330000` bytes. There is no factory app.
SDK/control/layout size optimization preserves pixel renderers, DMA/PPA, radio
and cryptography optimization. NVS, OTA metadata, PHY data and storage offsets
and sizes are unchanged; FAT remains `0x680000` / `0x980000`.
Export the SDK tools normally, including the ROM ELF directory, before building.

Verified full ESP-IDF 5.3.5 build: `app_check_size` passes with an image of
`0x27acc0` (2,600,128 bytes) and **`0xb5340` (742,208 bytes) free per OTA
slot**, exceeding the 500 KiB requirement. The generated binary table, app and
bootloader configuration, erased initial OTA metadata and four-file USB flash
plan also pass `tests/test_partitions.py`. This is build verification only;
no device was flashed or erased.

### Mandatory partition-table migration (USB only)

**Synchronize every game and verify zero unacknowledged games before the first
flash with this new table.** The FAT partition holds the unsynced journal:
never format or move it. Stop play while migrating.

From `artifacts/firmware`, with the SDK exported and the existing ESP32-P4
configuration retained:

```sh
idf.py reconfigure
idf.py build
python tests/test_partitions.py build
# USB-connected device only; substitute your serial port:
idf.py -p /dev/ttyUSB0 flash monitor
```

Use **`flash`, never `erase-flash` / `erase_flash`**. Do not use `app-flash`,
OTA, a full-flash erase or a merged whole-flash image for this first update.
The full flash target writes the new table, bootloader, initial (erased) OTA
metadata and app at `0x20000`. It does not write NVS, PHY data or storage.
Erased OTA metadata makes the bootloader select the first OTA slot; factory
reset and test-app boot must be disabled in any retained `sdkconfig` too.
The regression check verifies those settings, the generated table and flash
boundaries, and requires at least **500 KiB (512,000 bytes)** free per app slot.
After USB flashing, confirm boot, saved settings, journal and portal status
before resuming play or using ordinary app OTA.
See [README flash steps](README.md#first-usb-flash-with-the-ota-only-partition-table).

Run all host scripts in `artifacts/firmware/tests` and `artifacts/lora-gateway/tests`.
Protocol tests use real SHA-256 HMAC. If native mbedTLS has no `mbedcrypto.pc`,
pass its installation prefix as the first argument to `test_button_events.py`
and `test_remote_events.py`; this must match the host architecture. The
100-action nonce model is NOT a 100-shot physical relay measurement.

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
  press-to-relay-pulse latency (median <150 ms, P95 <300 ms, max <500 ms on
  normal WiFi), separately from press-to-response statistics. Do not substitute
  modeled test results or estimates for those measurements.
- Continue scoring, scrolling and typing during sync. Measure short commits and
  total blocked time; confirm no lost taps or selection/result resets.

Do not erase NVS, format FAT, reset replay counters, publish or flash as a
substitute for these tests.
