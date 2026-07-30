---
name: API keys DB pattern
description: Sync API keys are stored in the api_keys table and validated by async DB lookup in requireApiKey middleware
---

## Rule
Sync route authentication uses the `api_keys` table — NOT a SYNC_API_KEY environment variable. `requireApiKey` in auth.ts is an async Express 5 middleware that queries `apiKeysTable` for a matching active key.

**Why:** The club needs multiple device keys (1 emulator + 5 terminals) each independently revocable from the portal. A single env var cannot support this.

**How to apply:** When changing sync auth, update the `api_keys` table and the admin routes in `artifacts/api-server/src/routes/admin.ts`. The 6 seed rows were created by a Node.js inline script at initial migration (not via drizzle-kit push). Run from `lib/db/` using `node --input-type=module`.

## Seed info
- 6 rows pre-seeded: Emulator (EMULATOR type) + Terminal 1–5 (TERMINAL type)
- Keys are 32-byte hex strings (64 chars); only last 8 chars shown in portal UI
- Full key returned once on POST /api/admin/api-keys/:id/regenerate
