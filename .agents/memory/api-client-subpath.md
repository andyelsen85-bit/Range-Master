---
name: api-client-react subpath exports
description: custom-fetch.ts and any other non-index file in lib/api-client-react must have explicit exports entries to be importable as subpaths
---

**Rule:** `lib/api-client-react/package.json` exports map must include an entry for every file that consumers import as a subpath. The design subagent imports `@workspace/api-client-react/custom-fetch` to wire auth token injection.

**Why:** Node/Vite package resolution enforces the `exports` map — unlisted paths throw "Missing specifier" at startup.

**How to apply:** When adding a new importable file to lib/api-client-react, add `"./filename": "./src/filename.ts"` to the `exports` field in its package.json.

Current required entries (as of initial build):
- `"."` → `./src/index.ts`
- `"./custom-fetch"` → `./src/custom-fetch.ts`
