---
name: Player deactivation integrity
description: Data-integrity rules for reversible player deactivation and destructive operational resets.
---

Player deactivation is reversible and must preserve historical games, sales, credits, bills, and the player's prior portal-access setting. Inactive players must not receive new ledger activity.

**Why:** Route-level active checks alone have time-of-check/time-of-use races with concurrent terminal sync. A stale terminal can pass a check immediately before deactivation unless the database also enforces the boundary.

**How to apply:** Protect player-linked activity with a database trigger that locks and verifies the active player row inside the write transaction. For day/global purge, lock all affected activity and player tables in the purge transaction rather than reserving extra pooled connections with session advisory locks.

Already-completed offline games from before a player's deactivation may be
imported as historical scores. This is not permission for new credits, sales
or payments, and an unknown player must not cause unrelated games to be lost.

**Why:** The user requires missing historical games to remain recoverable even
when the player has since been deactivated.

**How to apply:** Keep the historical exception transaction-scoped and limited
to completed-game participation/results with a valid deactivation cutoff.
Reject individual unknown/invalid games and retain them locally.

The global post-testing reset must delete all non-admin player accounts, preserve
every admin account and its login credentials, and clear activity/statistics for
everyone, including administrators.

**Why:** The user wants to remove demo players and test statistics before live use
without deleting admin-flagged accounts.

**How to apply:** Do not restrict cleanup to players whose names look like demos,
and do not exempt admin activity from deletion. Keep terminal sync disconnected
until local test data is removed so it cannot repopulate the portal.

Destructive game/day cleanup must retain administrator audit history with enough
copied game identity to remain understandable after game references are nulled.
New player/game-linked features must account for both day cleanup and global
reset, rather than relying only on the original financial/game tables.

**Why:** The user requires “Tag löschen” to clear the operational day without
losing audit evidence or leaving queue and daily-list references that block
deletion. Shared player profiles and group membership are not owned by one day.

**How to apply:** Keep all cleanup and the surviving PURGE audit in one locked
transaction. Reject unknown blocking dependencies with a readable admin error
and complete rollback; never silently cascade new tables or broaden a day purge.