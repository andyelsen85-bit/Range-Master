---
name: C6 AT firmware flashing
description: Status and instructions for flashing Espressif ESP-AT firmware to the ESP32-C6 co-processor on the Guition JC8012P4A1C board
---

## Status
C6 is completely silent (empty RX) — confirmed no AT firmware. Pin swap (GPIO4↔5) ruled out as cause.

## Firmware ready
`c6_firmware/factory_ESP32C6-4MB.bin` — extracted from ESP32-C6-4MB-AT-V4.1.1.0.zip, single merged binary at address 0x0.

## Hardware needed
CP2104 USB-serial adapter (ordered, arriving Monday). Must expose DTR + RTS for auto-reset.

## Wiring (C6-MINI-1U pads on PCB)
- Adapter TXD → C6 pad U0RXD (GPIO17)
- Adapter RXD → C6 pad U0TXD (GPIO16)
- Adapter RTS → C6 pad EN  (via ~100Ω)
- Adapter DTR → C6 pad GPIO9/BOOT (via ~100Ω)
- Adapter GND → board GND
- Do NOT connect 3.3V/5V (board self-powered)

## Flash command
```
esptool.py --chip esp32c6 --port <PORT> --baud 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x0 factory_ESP32C6-4MB.bin
```

**Why:** C6 UART0 (programming port) is separate from the AT-command UART (UART1 on GPIO4/5 that talks to the P4). No dedicated USB port on this board — pads only.
