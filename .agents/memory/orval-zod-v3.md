---
name: Orval Zod v3 compatibility
description: How to keep generated validation code runnable while Orval emits helpers unavailable in the workspace's Zod version.
---

Orval currently emits Zod v4 primitive helpers for integer, email, and UUID schemas, while this workspace resolves Zod v3. Keep a deterministic post-generation compatibility transform in the codegen command.

**Why:** TypeScript can miss this mismatch, but the API crashes during module initialization because those helper functions are undefined at runtime.

**How to apply:** After every OpenAPI generation, run the checked-in compatibility transform before library typechecking or starting the API. Do not manually patch generated output without updating the transform.