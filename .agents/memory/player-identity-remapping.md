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

Do not acknowledge the local-player update before durable game identities have
been remapped. Include games finishing during the disk-to-RAM transition and
private UI selections/row actions, not just games already in the RAM queue.

**Why:** A reset after acknowledging the player but before rewriting its games
can strand historical games under a negative ID with no pending create left to
repair them. A concurrent game finish can otherwise recreate that old identity.

**How to apply:** Remap disk records first, translate concurrent finishing
snapshots, retain the local-player update on persistence failure, then publish
the bounded RAM transition. Never let array reordering change the player an
operator's open form or queued action targets.

Local-player creates must be idempotent by the stable create-event identity,
never by player name. Persist the returned portal identity before trying local
remaps; retain it and the pending create until every durable ledger is updated.
Do not regenerate a create identity on retry or silently merge historical
same-name duplicates.

**Why:** A lost response or failed terminal remap previously caused another
portal player to be created on retry. The user explicitly requires separate
same-name people to remain separate unless the operator chooses otherwise.

**How to apply:** Preserve create-event IDs through persistence upgrades.
Also keep local negative IDs unique across creates and reboots: reusing an
acknowledged ID while a journal remap registry still holds it can attribute a
new player's games to the earlier player.