---
name: FAT cache mount safety
description: Rules for initializing and writing the terminal's FAT-backed offline cache without mount storms or event-loop instability.
---

Initialize FAT only on first use, and never retry automatic mounts in a loop.
Validate mounts with capacity information and a small write/fsync/close probe.
Rebuildable snapshots must not retain two full copies: discard the previous
section before writing its replacement and remove failed temporary writes.
Durable journal replacements still need recoverable atomic swaps because
FatFs rename does not overwrite a destination. Missing cache snapshots must
invalidate their persisted manifest token on reload.

**Why:** A terminal with a non-FAT `storage` partition produced `FR_NO_FILESYSTEM`; duplicated cache writes retried the failed mount many times immediately before a Load access fault in the ESP event loop. After formatting succeeded, subsequent product and roster snapshots still failed because their destination files already existed. A later partition move left the old boolean marker in NVS, preventing the new raw partition from being initialized.

**How to apply:** Keep mount state tri-state (untried, mounted, unavailable), preserve the mount/format geometry, and allow only the deliberate recovery criteria above. A layout marker alone does not prove durability: older firmware could mark a successfully mounted volume before discovering that every write fails. A provably empty snapshot volume may still qualify as fresh. Ensure each authoritative dataset update performs one cache write, and recover a valid backup left by power loss during replacement.

The offline cache is an optimization, not the source of truth. A failed FAT
snapshot or metadata write must not reject successfully fetched server data or
turn a successful sync into ESP_FAIL. Apply the data in RAM and existing NVS
operational persistence, report sync success, and separately indicate that the
cache is unavailable. Never move large portal snapshots into the small NVS
partition as an error fallback.

**Why:** The user reported ENOSPC for tiny writes after erase-flash/reflash while
the other terminal's established volume worked, and explicitly requires cache
failures to remain non-fatal.

**How to apply:** Keep cache health separate from sync status. Do not hide a
failed section with another section's success or advance its durable manifest
token. Preserve NVS outboxes during cache reformatting.

A partition that also contains the completed-game journal is no longer a
disposable cache. Its initialization/format permission must remain revoked.

**Why:** The requested large durable game outbox cannot fit in the board's small
NVS partition. Reusing the FAT partition for durable games changes the earlier
cache-only rebuild assumption; formatting would now delete unacknowledged games.

**How to apply:** Distinguish optional snapshot failures from journal failures.
Never auto-format a volume with pending or unverified durable work. A later user-
approved exception permits verified-empty orphan-cluster repair; see below.
Keep saved games recoverable and block new games when safe storage is unavailable
rather than silently dropping records.

Cache-only cleanup is permitted only when RAM and a read-only verified journal
scan both prove no pending games. Corrupt/partial/unreadable evidence fails
closed. Hold cache and journal locks through deletion/remount; never delete
journal or unknown files in this operation. A layout marker cannot prohibit
safe deletion of verified rebuildable files. Keep an unwritable mount readable
so pending games can still upload.

**Why:** The user reported a full FAT preventing all new games. A protected
layout marker must not make cache-only recovery impossible, and an empty RAM
queue does not prove absence of durable unacknowledged records.

**How to apply:** Treat archive retention as both age- and count-bounded;
seven days alone is unbounded under heavy usage or an unset clock. Never evict
pending games. Maintain usable-capacity budgets including cluster rounding,
replacement scratch and a free-space reserve; do not budget from raw flash.
Failed fsync/close does not authorize deleting a complete checksummed journal
temporary: it may be the only recoverable copy. Remove only incomplete writes
whose live source is retained; keep complete unsynced temporaries protected.

A portal's explicit acceptance is authoritative even if the local journal
record is absent. Do not strand an acknowledged game in a RAM retry loop.
Transient write failures require retry and a fresh readiness proof, not a
permanent boot-lifetime failure latch. Cache write throttling is deferral, not
evidence that media failed; deferred snapshots must not advance durable tokens.

**Why:** A missing journal record after a failed finish caused every successful
portal upload to report failure and permanently blocked new games. A separate
five-minute cache throttle falsely reported healthy media as unavailable.

**How to apply:** Preserve unacknowledged records and bounded durable fallback
data across restart; clear only the accepted ID. Include recovery siblings and
older active checkpoints in the reasoning so acceptance cannot resurrect a
pending or active game. Never infer the original I/O cause from free space alone.

The user selected: **"Refuse repair until all durable pending data is synced."**
Only a proven-empty-of-pending-work FAT may be reformatted to reclaim orphan
chains. Active games, queue reservations/events, financial outboxes, unknown files
and uncertain evidence refuse destructive repair. NVS/settings are retained.

**Why:** The device reported nearly all FAT clusters allocated while visible files
used only a few hundred KiB; deleting cache files cannot free unreachable chains.
PSRAM staging cannot protect pending data across a power cut and was explicitly
rejected in favor of refusing repair.

**How to apply:** Verify durable records rather than trusting RAM counts. Persist
empty-volume repair authorization before destruction and block new writes until
it is safely cleared, including on failure/reboot. Fsync/chunking reduces FAT
power-cut exposure but must never be described as a proof of zero orphan clusters.

Empty-media proof must not depend on allocating record-sized buffers, and a
volatile "last active checkpoint" latch alone is not proof of a live game.

**Why:** The user reported that an empty FAT still refused repair. Unconditional
buffer allocation could fabricate uncertainty/presence under memory pressure;
a stale latch could independently veto genuinely empty storage.

**How to apply:** Establish actual file presence before allocating records. Only
discard a stale latch when file inspection succeeds with no active files and RAM
also has no active game. Report journal, active and queue failures separately;
never weaken the guard for real pending, unreadable or live RAM data.