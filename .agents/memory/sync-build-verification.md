---
name: Sync build verification
description: Trust boundaries for proving that a deployed portal has the terminal's sync contract.
---

Build identity must be embedded in the API bundle at build time, not read from
a mutable runtime commit variable.

**Why:** Updating an environment variable or image tag without replacing the
running binary can otherwise make an old server claim a new version.

**How to apply:** Compare the production health identity and actual authenticated
sync responses with the expected commit. Local builds with uncommitted changes
must identify that fact. A successful image push is not proof of a rollout.

An unauthorized response is not a successful endpoint-contract check.

**Why:** Authentication or fallback middleware can mask an absent route. The
reported incident concerned production while the repository/dev route existed.

**How to apply:** Use a valid terminal key and safe request bodies, test both
ingress forms, and retain a negative control that removes a known sync route
and proves the integration test catches the resulting 404.
