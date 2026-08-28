---
name: API route prefixes
description: Why the API server supports both prefixed and ingress-rewritten request paths.
---

The API server must accept both `/api/*` requests and equivalent root-mounted requests.

**Why:** The Replit development proxy preserves `/api`, while the production ingress may strip it before forwarding. Supporting only one form makes either previews or production fail with 404s.

**How to apply:** Mount the same API router at `/api` and at root. Keep route handlers themselves prefix-agnostic.