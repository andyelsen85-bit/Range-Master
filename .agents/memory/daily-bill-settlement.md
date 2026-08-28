---
name: Daily bill settlement
description: Durable rules for player bills, payment closure, and confirmed clay totals.
---

Derive daily bills from immutable sale price snapshots and credit events. Represent Paid as a separate immutable, idempotent player/day closure event rather than mutating sales or credit history. Offline Paid events remain pending and the player remains active until the portal accepts or idempotently skips the same event.

**Why:** Local confirmation is not authoritative while offline; removing a player before portal acceptance can lose an unsettled bill. Mutable catalog labels or prices can also rewrite historical bills.

**How to apply:** Keep one retryable Paid outbox event per player/day, recover interrupted in-flight events after reboot, preserve paid history, and count clays only from acknowledged gameplay launches—not scores, tests, rejected commands, or unacknowledged fires.