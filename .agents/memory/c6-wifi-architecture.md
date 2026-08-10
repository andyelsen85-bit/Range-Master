---
name: C6 WiFi architecture on Guition JC8012P4A1C
description: How the ESP32-C6 co-processor connects to the ESP32-P4 on this board, confirmed from official Guition schematics, burn files, and live device boot log.
---

## Factory C6 firmware is NOT AT firmware

The Guition burn-files folder contains `JC-C6-slave_v2.3.2.bin`.
"Slave" = esp_hosted slave firmware. The C6 acts as a WiFi radio over **SDIO** (SD2_CMD/CLK/D0–D3 lines in the schematic), not UART AT commands.

**The C6 must be reflashed with Espressif AT firmware before any AT command will ever get a reply.**
AT firmware zip: `attached_assets/ESP32-C6-4MB-AT-V4.1.1.0_1786225071929.zip`
Flash `factory/factory_ESP32C6-4MB.bin` at address 0x0 via CN5 (C6 UART0, the flashing/debug connector).

**Why:** The Guition demo (xiaozhi) uses the C6 natively via esp_hosted/SDIO — that's why none of the demo `config.h` files have any UART pins for the C6.

## C6 EN/CHIP_PU — boots automatically, no P4 action needed

Schematic page `5_ESP32-C6.png` shows R59 (10 kΩ) pulling CHIP_PU to ESP_3V3 directly.
The C6 boots on its own power-on; the P4 does not need to drive any enable GPIO.

## AT firmware UART pin assignment — CONFIRMED FROM LIVE DEVICE

Boot log from device (CN5 @ 115200, after reflash with v4.1.1.0):
```
I (815) at-uart: AT cmd port:uart1 tx:7 rx:6 cts:5 rts:4 baudrate:115200
I (816) at-init: module_name: ESP32C6-4MB
I (819) at-init: v4.1.1.0 (gitlab)
```

**C6 AT UART1 default pin assignment (stock firmware):**
| Signal | C6 GPIO |
|--------|---------|
| AT TX (C6 sends) | GPIO7 |
| AT RX (C6 receives) | GPIO6 |
| CTS | GPIO5 |
| RTS | GPIO4 |

**The P4's current wiring (app_config.h: C6_UART_TX=GPIO5, C6_UART_RX=GPIO4) hits C6's CTS/RTS — not the data lines. That's the root cause of all AT silence.**

## CN5 connector

CN5 = C6's UART0 (flashing/boot port, GPIO16/GPIO17 on the C6). It shows boot logs and is how you flash the C6. It is NOT the AT command port — AT commands go to UART1 (GPIO6/GPIO7). CN5 is the only physical USB/serial access to the C6.

## Solution: custom AT firmware with UART remapped to GPIO4/GPIO5

Because the PCB was designed for SDIO (not UART AT), C6 GPIO6/GPIO7 are likely not routed to the P4. The pins that ARE connected to the P4 are GPIO4 (→ P4-GPIO4) and GPIO5 (→ P4-GPIO5).

**Build a custom AT firmware remapping UART1 to GPIO4/GPIO5:**

In `module_config/module_esp32c6_mini_1/sdkconfig.defaults` (or equivalent C6-MINI-1 folder):
```
CONFIG_AT_UART_PORT_TX_PIN=4    # C6 GPIO4 → wire → P4 RX (P4-GPIO4)
CONFIG_AT_UART_PORT_RX_PIN=5    # C6 GPIO5 → wire → P4 TX (P4-GPIO5)
CONFIG_AT_UART_PORT_RTS_PIN=-1  # disable flow control
CONFIG_AT_UART_PORT_CTS_PIN=-1
```

After reflashing, P4's `app_config.h` needs NO changes (C6_UART_TX=GPIO5, C6_UART_RX=GPIO4 stays as-is).

**Why GPIO4=TX and GPIO5=RX on C6 side:**
- P4-GPIO5 is set as UART TX (sends commands) → must connect to C6 RX = C6 GPIO5
- P4-GPIO4 is set as UART RX (receives replies) → must connect to C6 TX = C6 GPIO4

## Correct unsolicited event strings (Espressif ESP-AT)

- Connected + IP: `"WIFI GOT IP"` (sets s_wifi_connected = true)
- Disconnected:   `"WIFI DISCONNECT"` (sets s_wifi_connected = false)
- NOT: `"+WIFI:CONNECTED"` / `"+WIFI:DISCONNECTED"` (those were wrong — fixed in 2c84cd7)
