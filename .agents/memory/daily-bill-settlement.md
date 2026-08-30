---
name: Daily bill settlement
description: Durable rules for player bills, payment closure, and confirmed clay totals.
---

Derive daily bills from immutable sale and consumed-credit price snapshots. Paid is an immutable, idempotent cutoff: each accepted payment closes server-received billable activity up to that point, while later activity reopens a new balance. Offline Paid events remain pending until the portal accepts or idempotently skips them.

**Why:** Local confirmation is not authoritative while offline; removing a player before portal acceptance can lose an unsettled bill. Mutable catalog prices can rewrite historical bills, and terminal clocks are not trustworthy for settlement ordering.

**How to apply:** Snapshot a credit USE price/revision at consumption, but order payment coverage by server receipt time under the same player/day lock as activity inserts. Reconcile acknowledgements by exact payment event and date—not player ID—so a delayed prior-day payment cannot retire today's balance. Preserve player-scoped full-day lines and totals separately from the open balance. Retire a player from the terminal's operational day roster only when the bill projection is authoritatively Paid and no local day activity is pending; credit-total pulls must not recreate that settled roster entry. Keep one retryable Paid outbox event per open balance, recover interrupted in-flight events after reboot, and count clays only from acknowledged gameplay launches—not scores, tests, rejected commands, or unacknowledged fires.