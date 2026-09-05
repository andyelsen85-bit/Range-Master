---
name: Heltec S3 build toolchain
description: Build-environment constraint for compiling Heltec Wireless Stick V3 receiver firmware.
---

The current official Heltec board package installs both Xtensa and RISC-V
toolchains even though Wireless Stick V3 uses only ESP32-S3/Xtensa. In this
workspace, the complete installer exceeds the available per-user extraction
quota.

**Why:** Two normal installation attempts failed while unpacking the unrelated
RISC-V compiler. The same official core compiled successfully after retaining
the ESP32 Arduino libraries and Xtensa tools, manually installing the platform
and esptool, and adding the official Heltec ESP32 Dev-Boards library.

**How to apply:** For future Wireless Stick V3 builds, use the official portable
Arduino CLI and install only the ESP32-S3/Xtensa portions of the Heltec package.
Do not spend time retrying the complete all-board package unless the workspace
quota has changed.