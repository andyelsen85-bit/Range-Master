---
name: Sync manifest token sizing
description: Contract rule for revision tokens exchanged between the API manifest and terminal firmware.
---

Treat sync manifest revision tokens as opaque strings and size firmware storage for the complete value, including the algorithm prefix and terminating NUL.

**Why:** The API emits `sha256:` followed by 64 hexadecimal characters, while a 65-byte terminal buffer only fit the raw digest. Every valid manifest was therefore rejected as incompatible.

**How to apply:** When changing token generation, verify the serialized token length against firmware parsing, in-memory storage, and persisted metadata capacity.