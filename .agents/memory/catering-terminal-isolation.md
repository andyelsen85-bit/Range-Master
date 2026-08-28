---
name: Catering terminal isolation
description: Safety boundaries for the terminal's self-service food and drink mode.
---

Catering is a separate persisted terminal operating mode, never a value in the shooting-game mode enum. While active, navigation and game-start actions use an allow-list rather than relying only on hidden buttons.

**Why:** A stale callback, reboot, or future UI change must not expose shooting, credits, ammunition, or administration from the unattended bar kiosk.

**How to apply:** Authorize every basket at the store boundary against the current persisted Players-of-the-Day roster and active FOOD/DRINK products with current immutable price revisions. Persist PIN failure/lockout state so reboot cannot bypass it, and enqueue the whole basket atomically or not at all.