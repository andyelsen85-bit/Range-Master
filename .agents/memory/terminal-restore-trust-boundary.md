---
name: Terminal restore trust boundary
description: Security invariants for approving and applying terminal configuration restores.
---

Restore approval must be bound to both the exact replacement terminal identity and its active terminal API-key identity. The source terminal API key must never be included in a backup or applied during restore; the replacement keeps its separately provisioned key.

**Why:** A code alone is transferable, and restoring the source key would make the replacement impersonate the source after a successful pairing, defeating attribution and key revocation.

**How to apply:** Keep target ID/key checks atomic with one-time code consumption and backup revocation. Validate and decrypt before consuming the code, and preserve the replacement credential when applying settings.