---
name: FAT cache mount safety
description: Rules for initializing and writing the terminal's FAT-backed offline cache without mount storms or event-loop instability.
---

Initialize the reserved FAT cache partition only on first use, and never repeatedly call the ESP-IDF mount helper after a failed attempt in the same boot. Track a FAT layout generation rather than a permanent boolean. Validate successful mounts with capacity information and a small write/fsync/close probe. A bad-capacity volume, or a failing probe on a fresh/migrating volume, may receive one explicit, logged cache-only rebuild; never silently erase an established volume merely because mounting or probing failed. Cache writes belong at explicit dataset replacement or local identity-change boundaries, not in the generic NVS save path. Replace existing FatFs files through a temp → backup → destination swap because FatFs rename does not overwrite an existing destination.

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