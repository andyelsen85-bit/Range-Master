---
name: Zod in api-server routes
description: esbuild cannot resolve workspace catalog packages unless listed as a direct dep in the artifact's own package.json
---

**Rule:** Any `artifacts/api-server` route that imports `zod` must have `zod` in `artifacts/api-server/package.json` `dependencies`. The workspace pnpm catalog entry does not flow through to esbuild's module resolution.

**Why:** esbuild bundles from the artifact's own `node_modules` view. Catalog entries in the workspace root `pnpm-workspace.yaml` only resolve if the package is also listed directly in the consuming package's `package.json`.

**How to apply:** Before adding any import to api-server routes, check `artifacts/api-server/package.json` deps. If missing, run `pnpm --filter @workspace/api-server add <pkg>`.
