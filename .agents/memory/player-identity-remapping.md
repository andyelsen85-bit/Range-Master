---
name: Player identity remapping
description: Durable rules for roster refresh and local-to-portal player identity changes.
---

Apply authoritative player rosters through one shared reconciliation path, regardless of whether the refresh is direct or part of a full sync. Preserve pending local creates and reconcile saved lineups there.

**Why:** Separate refresh paths drifted: one preserved local players and repaired lineups while full sync replaced the roster directly, leaving stale assignments and old cached data.

**How to apply:** Any new roster-fetch path must call the shared roster application behavior rather than copying records itself.

When a local player receives a portal ID, remap every player-keyed structure as one coherent state transition, including credits, sales, queued games, history, active-game state, updates, and saved lineup positions.

**Why:** Remapping only the visible roster or lineup can strand balances and outbox events under the old ID, making the same player appear unfunded or unsynchronizable.

**How to apply:** Extend the central identity-remap operation whenever a new player-keyed ledger or cache is introduced; do not add one-off remaps in individual sync routes.