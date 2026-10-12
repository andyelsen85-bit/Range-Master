---
name: Drizzle SQL array bindings
description: Avoid interpolating JavaScript arrays as PostgreSQL array parameters
---

Do not assume `sql` template interpolation of a JavaScript array produces a
single PostgreSQL array parameter. In this workspace it expands values, and
an `ANY(...::text[])` comparison can consequently fail with a malformed array
literal even for one valid recipient.

**Why:** A persisted recipient save failed despite type checks and builder tests;
the real HTTP/database path exposed Drizzle's interpolation behavior.

**How to apply:** Use `inArray`/`notInArray` or explicit `sql.join` bound values
for membership lists; handle empty lists explicitly. Verify list CRUD through
the real endpoint, not only helper-level tests.
