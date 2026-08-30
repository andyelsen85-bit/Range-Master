---
name: NVS capacity for runtime caches
description: Persistence boundary for large terminal caches and durable outboxes on the firmware partition layout.
---

The terminal's 24 KiB NVS partition is reserved for settings and durable operational outboxes. Large, rebuildable portal snapshots such as the day-bill summary must remain runtime-only, and variable-length outboxes should persist only their active entries.

**Why:** Persisting a full day-bill snapshot exhausted NVS and caused credit grants to fail with `ESP_ERR_NVS_NOT_ENOUGH_SPACE`.

**How to apply:** Before adding an NVS blob, budget its worst-case size plus NVS copy-on-write overhead. Prefer RAM or the existing filesystem partition for rebuildable caches; store only active elements for bounded queues.