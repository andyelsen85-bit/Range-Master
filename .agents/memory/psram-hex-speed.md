---
name: ESP32-P4 hex PSRAM speed config
description: Correct sdkconfig symbols for 200MHz hex PSRAM on IDF 5.3.5 — only two speeds exist
---

On IDF 5.3.5 for ESP32-P4, the PSRAM speed choice in `esp32p4/Kconfig.spiram` has **exactly two options**:
- `CONFIG_SPIRAM_SPEED_20M=y` — default
- `CONFIG_SPIRAM_SPEED_200M=y` — requires `CONFIG_IDF_EXPERIMENTAL_FEATURES=y`

There is no 40MHz, 80MHz, or 100MHz option. `CONFIG_SPIRAM_SPEED_100M` appears as a dead reference in the int-derivation stanza but has no corresponding choice entry.
`CONFIG_SPIRAM_MODE_OCT` does not exist for P4 (that's S3-specific). `CONFIG_SPIRAM_MODE_HEX=y` is the default and must be set explicitly only for clarity.

**Correct sdkconfig.defaults block:**
```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_IDF_EXPERIMENTAL_FEATURES=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
```

**Why:** Any attempt to use SPIRAM_SPEED_80M or SPIRAM_SPEED_40M is silently ignored — Kconfig warns "unknown symbol" and PSRAM stays at 20MHz default.

**How to apply:** Always `del sdkconfig` (Windows) / `rm sdkconfig` (Linux) before rebuilding after changing sdkconfig.defaults, otherwise the cached sdkconfig overrides the new defaults.
