---
name: Password hashing in seeds
description: bcryptjs cannot be imported in the CodeExecution sandbox; use pgcrypto for SQL-side password hashing
---

**Rule:** To seed hashed passwords in CodeExecution, use PostgreSQL's pgcrypto extension instead of importing bcryptjs.

**Why:** `bcryptjs` is installed only in `artifacts/api-server`, not at the workspace root. The CodeExecution sandbox resolves from the workspace root and cannot find it.

**How to apply:**
```sql
CREATE EXTENSION IF NOT EXISTS pgcrypto;
INSERT INTO spieler (..., passwort_hash)
VALUES (..., crypt('demo1234', gen_salt('bf')));
```

The `auth.ts` route uses `bcrypt.compare(passwort, hash)` — bcryptjs compare is compatible with pgcrypto's `crypt()` output.
