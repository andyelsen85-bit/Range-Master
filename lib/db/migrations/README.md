# DB Migrations

Manual migration steps that must be applied to the live database before or alongside a deploy.

---

## modus enum – CUSTOM_4 (applied 2026-07-31)

**Why:** The `modus` PostgreSQL enum was missing `CUSTOM_4` and contained two removed values
(`HARAKIRI_DELAYED`, `HARAKIRI_FULL`).  The sync route already validates against the new set,
so any `CUSTOM_4` game arriving before the DB migration would cause an insert failure.

**What was changed in code:**
- `lib/db/src/schema/spiele.ts` – removed `HARAKIRI_DELAYED` and `HARAKIRI_FULL`; added `CUSTOM_4`

**SQL to apply (idempotent, additive – safe on a live DB):**
```sql
ALTER TYPE modus ADD VALUE IF NOT EXISTS 'CUSTOM_4';
```

> **Note:** Postgres `ALTER TYPE … ADD VALUE` commits immediately and cannot be run inside a
> transaction block.  Run it directly via `psql` or a plain client connection before deploying
> the new API image.

**Removed values (`HARAKIRI_DELAYED`, `HARAKIRI_FULL`):**
Existing rows that still carry these values will continue to read back correctly — Postgres
retains enum labels that are stored in live rows even after the Drizzle schema no longer
lists them.  No `UPDATE` or backfill is required unless you want to normalise old data.
If a future migration needs to drop those labels, do so only after confirming no rows reference
them:
```sql
SELECT COUNT(*) FROM spiele WHERE modus IN ('HARAKIRI_DELAYED', 'HARAKIRI_FULL');
```
