---
name: FAT cache mount safety
description: Rules for initializing and writing the terminal's FAT-backed offline cache without mount storms or event-loop instability.
---

Initialize the reserved FAT cache partition only on first use, and never repeatedly call the ESP-IDF mount helper after a failed attempt in the same boot. Cache writes belong at explicit dataset replacement or local identity-change boundaries, not in the generic NVS save path. Replace existing FatFs files through a temp → backup → destination swap because FatFs rename does not overwrite an existing destination.

**Why:** A terminal with a non-FAT `storage` partition produced `FR_NO_FILESYSTEM`; duplicated cache writes retried the failed mount many times immediately before a Load access fault in the ESP event loop. After formatting succeeded, subsequent product and roster snapshots still failed because their destination files already existed.

**How to apply:** Keep mount state tri-state (untried, mounted, unavailable), permit formatting only before the initialization marker exists, ensure each authoritative dataset update performs one cache write, and recover a valid backup left by power loss during replacement.