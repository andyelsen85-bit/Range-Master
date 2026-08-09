---
name: C6 WiFi architecture on Guition JC8012P4A1C
description: How the ESP32-C6 co-processor connects to the ESP32-P4 on this board, confirmed from official Guition schematics and burn files.
---

## Factory C6 firmware is NOT AT firmware

The Guition burn-files folder contains `JC-C6-slave_v2.3.2.bin`.
"Slave" = esp_hosted slave firmware. The C6 acts as a WiFi radio over **SDIO** (SD2_CMD/CLK/D0–D3 lines in the schematic), not UART AT commands.

**The C6 must be reflashed with Espressif AT firmware before any AT command will ever get a reply.**
AT firmware zip: `attached_assets/ESP32-C6-4MB-AT-V4.1.1.0_1786225071929.zip`
Flash `factory/factory_ESP32C6-4MB.bin` at address 0x0 via the C6's USB port (labeled USB1 on the board).

**Why:** The Guition demo (xiaozhi) uses the C6 natively via esp_hosted/SDIO — that's why none of the demo `config.h` files have any UART pins for the C6.

## C6 EN/CHIP_PU — boots automatically, no P4 action needed

Schematic page `5_ESP32-C6.png` shows R59 (10 kΩ) pulling CHIP_PU to ESP_3V3 directly.
The C6 boots on its own power-on; the P4 does not need to drive any enable GPIO.

## C6 UART pins (for AT firmware mode)

- C6's own UART0: TXD = C6 IO9 (pin 31), RXD = C6 IO8 (pin 30)
- Exposed on connector **CN5**: pin 3 = C6_U0TXD, pin 4 = C6_U0RXD, pin 6 = C6_CHIP_PU
- P4-side GPIO numbers: **unconfirmed** — net labels C6_U0TXD / C6_U0RXD appear on the P4 schematic but the image resolution is too low to read them
- Current best guess (committed b8b8458): GPIO5 = P4 TX → C6 RX, GPIO4 = P4 RX ← C6 TX
  (swapped from original GPIO4/5 assignment which had no source citation)

**How to apply:** After flashing AT firmware, run `idf.py monitor` and look for
`C6 responsive (attempt N/5)`. If still silent, trace CN5 physically on the board
to find which P4 GPIOs it connects to and update `app_config.h`.

## AT firmware UART defaults (from sdkconfig)

Espressif ESP-AT v4.1.1.0 for ESP32-C6-4MB uses UART0 at 115200 baud by default.
Our `C6_UART_BAUD = 115200` matches.

## Correct unsolicited event strings (Espressif ESP-AT)

- Connected + IP: `"WIFI GOT IP"` (sets s_wifi_connected = true)
- Disconnected:   `"WIFI DISCONNECT"` (sets s_wifi_connected = false)
- NOT: `"+WIFI:CONNECTED"` / `"+WIFI:DISCONNECTED"` (those were wrong — fixed in 2c84cd7)
