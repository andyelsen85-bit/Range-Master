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
Never auto-format a journal-bearing volume. Keep saved games recoverable and
block new games when safe storage is unavailable rather than silently dropping
records.

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