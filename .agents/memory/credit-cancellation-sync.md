---
name: Credit cancellation during sync
description: Safe credit rollback when a game is canceled while portal synchronization is underway.
---

Credit-event synchronization must operate on an immutable in-flight snapshot, and a cancellation must treat every in-flight `USE` as potentially accepted by the portal. A later portal credit pull must replay all still-pending local events over the received totals.

**Why:** A local queue entry can be serialized before the network request returns. Removing it during cancellation can lose the only portal refund. Even after queuing a refund, a portal pull can otherwise overwrite the restored local balance until the next sync.

**How to apply:** When changing game cancellation, credit syncing, or portal reconciliation, preserve newly queued compensation events while settling only the sent snapshot. Test the accepted-USE → cancel → credit-pull interleaving, including the displayed available-credit balance.