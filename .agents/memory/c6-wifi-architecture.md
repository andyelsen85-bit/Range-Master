---
name: C6 WiFi architecture on Guition JC8012P4A1C
description: How the ESP32-C6 co-processor connects to the ESP32-P4 on this board — confirmed from BSP documentation and Espressif compatibility docs.
---

## CONFIRMED: The P4↔C6 link is SDIO, not UART AT

The Guition JC8012P4A1C board was designed and wired exclusively for **SDIO** communication between the P4 and C6. There is NO UART data link between them. GPIO4/GPIO5 on the P4 are NOT connected to the C6 in any way — all UART AT work using those pins was targeting a connection that does not physically exist on this board.

**Confirmed P4↔C6 interface (from profi-max/JC8012P4A1_BSP_ESP32P4 BSP docs):**
| Signal | P4 GPIO |
|--------|---------|
| SDIO CMD | GPIO18 |
| SDIO CLK | GPIO19 |
| SDIO D0  | GPIO14 |
| SDIO D1  | GPIO15 |
| SDIO D2  | GPIO16 |
| SDIO D3  | GPIO17 |
| C6 Reset | GPIO54 |
| C6 Wakeup| GPIO6  |

## Factory C6 firmware: ESP-Hosted-MCU slave (NOT AT firmware)

The C6 shipped with **`ESP-Hosted-MCU slave firmware v0.0.6`** (SDIO slave mode). It was never meant to run AT firmware. The entire reflash-to-AT-firmware effort (CN5 flashing, UART pin remapping, at_customize NVS erasure) was correct execution against a wrong architectural assumption inherited from the original `coprocessor.cpp`.

The C6 needs to be **reflashed back to ESP-Hosted slave firmware** before the SDIO approach can work.

## IDF compatibility: already satisfied

From Espressif official compatibility docs:
> "ESP32-C6: It is recommended to use esp_hosted ≥ 2.4.2, esp_wifi_remote ≥ 1.0.0, ESP-IDF ≥ v5.3.2"

**Our project runs IDF v5.3.5 — already above the minimum.** No IDF upgrade needed.

## Correct P4-side implementation

Replace the custom UART AT `coprocessor.cpp` with:
1. `esp_hosted` managed component (SDIO master on P4)
2. `esp_wifi_remote` managed component (WiFi API over hosted)
3. Configure SDIO on P4 GPIOs 14/15/16/17/18/19, reset=GPIO54, wakeup=GPIO6

**Reference example:** `Simple_WiFi_hosted` folder in the `profi-max/JC8012P4A1_BSP_ESP32P4` GitHub repo — working SDIO+hosted integration for this exact board.

## What NOT to do

- Do not attempt UART AT communication between P4 and C6 — the PCB traces don't exist
- Do not use P4 GPIO4/5 for C6 communication — they are not connected to C6
- Do not use IDF 5.4.x or 5.5.x — the P4 ECO2 DSI display only works stably on 5.3.x

## AT firmware residue

The C6 currently has custom-built AT firmware (v4.1.1.0, with UART remapped to GPIO4/5, at_customize partition erased) as a result of the investigation. This must be replaced with ESP-Hosted slave firmware before the SDIO path works.

## Correct unsolicited event strings (kept for reference, no longer used)

These were correct for the AT approach — no longer relevant.
- Connected + IP: `"WIFI GOT IP"`
- Disconnected: `"WIFI DISCONNECT"`
