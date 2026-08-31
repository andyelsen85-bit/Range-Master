---
name: Player deactivation integrity
description: Data-integrity rules for reversible player deactivation and destructive operational resets.
---

Player deactivation is reversible and must preserve historical games, sales, credits, bills, and the player's prior portal-access setting. Inactive players must not receive new ledger activity.

**Why:** Route-level active checks alone have time-of-check/time-of-use races with concurrent terminal sync. A stale terminal can pass a check immediately before deactivation unless the database also enforces the boundary.

**How to apply:** Protect player-linked activity with a database trigger that locks and verifies the active player row inside the write transaction. For day/global purge, lock all affected activity and player tables in the purge transaction rather than reserving extra pooled connections with session advisory locks.