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

Do not immediately force-drop after `Pool.end()`. Wait, with a bounded timeout,
until PostgreSQL sees no remaining test-database sessions, then use ordinary
`DROP DATABASE`. A single immediate zero-session assertion can also race.

**Why:** Local-player CI and purge integration runs passed their business
assertions but immediate forced drops raced remote backend shutdown and emitted
asynchronous “terminating connection” errors. Even after local pool shutdown,
PostgreSQL can briefly report idle sessions. Bounded polling distinguishes that
delay from an actual leaked pool without terminating clients or suppressing errors.

**How to apply:** Track actual pool connections as well as construction when
loaders may cache constructors, and assert every application connection targets
the disposable database. Do not use forced termination to hide a resource leak.
