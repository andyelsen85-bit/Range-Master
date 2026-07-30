---
name: drizzle-kit push blocked by interactive prompt
description: How to apply schema changes when `pnpm --filter @workspace/db run push` hangs on a TTY prompt
---

`drizzle-kit push` in this repo hits an interactive prompt ("add api_keys_key_unique unique constraint … truncate?") caused by pre-existing drift between schema and DB, and fails in non-TTY shells.

**Why:** the api_keys table pre-dates the unique constraint in the schema; drizzle wants confirmation every push.

**How to apply:** create new tables/enums directly with idempotent SQL (`CREATE TABLE IF NOT EXISTS`, `DO $$ ... EXCEPTION WHEN duplicate_object`) via `node --input-type=module` run from `lib/db/` (pg is resolvable there). Afterwards run `npx tsc -b lib/db` so dependents' project references see the new exports.
