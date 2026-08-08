---
name: ESP32-P4 IDF version compatibility
description: IDF 5.5.x is incompatible with ESP32-P4 ECO2 (chip rev v1.3); must use IDF 5.4.x
---

## Rule
Always build the TrapMaster firmware with **IDF 5.4.x**. Do not use IDF 5.5.x.

**Why:** The Guition JC8012P4A1C board carries an ESP32-P4 revision v1.3 (ROM string: `esp32p4-eco2-20240710`, ECO2 silicon). IDF 5.5.x targets ECO3 (v3.1+) and compiles a bootloader that executes an illegal RISC-V instruction on ECO2 hardware. The crash is `Guru Meditation Error: Core 0 panic'ed (Illegal instruction)` at `call_start_cpu0` — the very first line of the bootloader. `CONFIG_ESP32P4_REV_MIN_FULL=100` in sdkconfig.defaults does NOT fix this; the compiler/linker flags are hardcoded to ECO3 in IDF 5.5.x regardless of Kconfig. Using `--force` in esptool bypasses the flash-time version check but the binary still crashes at runtime.

**How to apply:** Any time the firmware build instructions or CI config reference an IDF version, pin it to `5.4.x`. The Windows installer for 5.4 creates a separate `ESP-IDF 5.4 CMD` shortcut; it coexists with 5.5.
