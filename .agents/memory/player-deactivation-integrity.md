---
name: Player deactivation integrity
description: Data-integrity rules for reversible player deactivation and destructive operational resets.
---

Player deactivation is reversible and must preserve historical games, sales, credits, bills, and the player's prior portal-access setting. Inactive players must not receive new ledger activity.

**Why:** Route-level active checks alone have time-of-check/time-of-use races with concurrent terminal sync. A stale terminal can pass a check immediately before deactivation unless the database also enforces the boundary.

**How to apply:** Protect player-linked activity with a database trigger that locks and verifies the active player row inside the write transaction. For day/global purge, lock all affected activity and player tables in the purge transaction rather than reserving extra pooled connections with session advisory locks.

The global post-testing reset must delete all non-admin player accounts, preserve
every admin account and its login credentials, and clear activity/statistics for
everyone, including administrators.

**Why:** The user wants to remove demo players and test statistics before live use
without deleting admin-flagged accounts.

**How to apply:** Do not restrict cleanup to players whose names look like demos,
and do not exempt admin activity from deletion. Keep terminal sync disconnected
until local test data is removed so it cannot repopulate the portal.