---
name: Isolated PostgreSQL test cleanup
description: Why mixed-loader integration tests must account for multiple DB pools during teardown.
---

When isolated PostgreSQL tests mix Node/tsx ESM and CommonJS imports, account for
multiple instances of the database module and close every test-owned pool before
dropping the temporary database.

**Why:** An admin-games integration run completed every assertion but failed
during teardown with “terminating connection due to administrator command.”
Ending only the first exported pool did not reliably prevent a remaining idle
client from being terminated by forced database removal.

**How to apply:** Scope any pool-construction tracking to the isolated test, close
all captured pools, restore the original constructor, and only then drop the
test-owned database. Never terminate development or production workflow pools.
