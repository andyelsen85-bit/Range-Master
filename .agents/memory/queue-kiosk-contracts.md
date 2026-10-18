---
name: Queue and kiosk contracts
description: Business isolation and decisions for contradictory live/kiosk requirements.
---

Queue takeover and game start are separate voluntary operator actions. A group is
not split by default. Hiding or correcting scores changes only display/statistics,
never billing, credits, ammunition counters, inventory, daily close or journal data.

**Why:** The requested kiosk feature explicitly separates spectator/operator views
from physical launches and immutable financial operations.

**How to apply:** Keep operational queries unfiltered when changing visible-results
logic; do not connect queue signup/takeover to automatic start, launch or credit use.

The live contract deliberately prefers full bounded snapshots over the contradictory
4-KiB limit. Its `null` response is authoritative: never retain a cached RUNNING panel
or infer a FINISHED transition from null, because stale/aborted also return null.
Only persistent sequence/start metadata is allowed, not a live-payload outbox.

**Why:** Six-player full snapshots cannot fit 4 KiB, and identical null responses
cannot distinguish finish from expiry. Both contradictions were documented rather
than silently truncating results or showing stale activity.

**How to apply:** A future “BEENDET” bridge needs an explicit changed response
contract. Live retries must coalesce to the newest state rather than replay history.

Do not increase binary journal record dimensions merely to increase live capacity.

**Why:** Existing FAT records may contain unsynchronised games; changing struct sizes
without a versioned reader/migration can make those records unreadable.

**How to apply:** Preserve compatibility or implement and test a versioned migration
before changing persisted capacities. Never format or erase storage to accommodate it.
