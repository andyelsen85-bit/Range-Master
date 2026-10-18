---
name: Credit cancellation during sync
description: Safe credit rollback when a game is canceled while portal synchronization is underway.
---

Cancellation reverses consumption with a reference to the original USE; it must never create GRANTs or increase granted credits. Treat every attempted delivery as potentially accepted, even after a failed ACK or restart. Preserve the original event's player/day identity through cancellation.

**Why:** The user requires zero net credit effect and no billing for cancelled games, with granted credits unchanged. A local queue entry may already be accepted before the network request returns; failure is not proof it was unsent. A compensating GRANT masks consumed credit instead of reversing it.

**How to apply:** Settle only an immutable sent snapshot. Overlay pending events only when same-snapshot portal acknowledgements prove they are not already reflected in the baseline; never blindly replay the entire outbox. Keep enough identity to prevent a cancelled game resuming if power fails before its active checkpoint is cleared. Retain historical compensating GRANTs for manual review, never heuristic rewriting.

The credit-state capability check before a credit upload is intentional even
though the normal baseline pull follows later.

**Why:** An older portal can accept credit uploads while lacking the state/proof
endpoint. The user requires the durable local outbox to remain queued in that
case, rather than settling it against a portal that cannot reconcile it.

**How to apply:** Treat missing/unsupported sync methods as deployment-contract
errors, not transient network failures. Retain event identities and delivery
uncertainty on refusal; do not introduce a compensating GRANT or silently use
the old aggregate-only endpoint.