---
name: Terminal restore trust boundary
description: Security invariants for approving and applying terminal configuration restores.
---

Restore approval must be bound to both the exact replacement terminal identity and its active terminal API-key identity. The source terminal API key must never be included in a backup or applied during restore; the replacement keeps its separately provisioned key.

**Why:** A code alone is transferable, and restoring the source key would make the replacement impersonate the source after a successful pairing, defeating attribution and key revocation.

**How to apply:** Keep target ID/key checks atomic with one-time code consumption and backup revocation. Validate and decrypt before consuming the code, and preserve the replacement credential when applying settings.

Legacy UNKNOWN is not an alias for any concrete chip. Keep ambiguous historical
backups/sources and old approval bindings; require an explicitly selected
backup and a fresh real-ID/key approval, never infer ownership from UNKNOWN.

**Why:** The P4 WiFi-MAC API failed on devices whose WiFi is on the C6, making
multiple terminals share the same old identity. Historical attribution cannot
be recovered from that value alone.

**How to apply:** Derive identity from P4 factory eFuse, fail visibly without a
shared fallback, and preserve external event IDs across firmware updates.
Legacy immutable inventory retries may receive only an exact-payload duplicate
ACK, never relabelling or applying the event again.