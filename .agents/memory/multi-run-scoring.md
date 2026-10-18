---
name: Multi-run scoring compatibility
description: Why wire participations may expand but the terminal journal ABI must remain stable.
---

Per-run participation rows on the wire must not enlarge the terminal's binary
pending/finished-game structs. Expand from the shot ledger when serializing and
fold downloaded run rows into per-player totals.

**Why:** Existing terminals can hold unsynced FAT journals, active checkpoints and
an emergency NVS record written using the prior binary layout. Enlarging a
participation array would make those records unreadable; the user requires no
silent loss of unsynced games.

**How to apply:** Treat persisted-layout changes as an explicit, versioned
migration, not a routine scoring fix. Preserve aggregate-only legacy history:
without a shot ledger, the individual Lauf values cannot be reconstructed safely.
