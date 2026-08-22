---
name: Nested Arduino include resolution
description: Relative include behavior when Arduino compiles a wrapper sketch that includes a shared parent .ino
---

Arduino IDE compiles the nested sketch entry point as the active source context even when that file includes the shared parent `.ino`. Relative paths in the shared implementation therefore need to support both the top-level sketch directory and the nested sketch directory.

**Why:** A path that works when compiling the parent `.ino` directly can fail in the Arduino-required same-name subfolder, because the IDE reports and resolves the included source from the nested sketch location.

**How to apply:** For shared Arduino implementations used by both layouts, use guarded `__has_include` branches for the one-level-up and two-level-up shared-header paths. Keep the implementation single-sourced rather than copying it into the wrapper.