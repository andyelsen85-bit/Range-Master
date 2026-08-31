---
name: LVGL generated font include path
description: Generated LVGL C font sources must use the project's simple lvgl.h include
---

Generated LVGL font sources must include `lvgl.h`, not `lvgl/lvgl.h`, in this firmware project.

**Why:** The ESP-IDF component exposes LVGL through the simple include path; the generated conditional fallback selected a nested path that is not present and stopped the Windows build before compilation.

**How to apply:** After generating any LVGL font source, normalize its include to `#include "lvgl.h"` and check the target build log for the generated font object files.