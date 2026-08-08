# TrapMaster Terminal — Installatiounsguide

**Hardware:** Guition JC8012P4A1C-I-W-Y (ESP32-P4 + ESP32-C6, 10.1″ 1280×800)  
**Software:** ESP-IDF 5.3.x + LVGL v9

---

## Iwwersiicht

```
Laptop  ──USB-C──▶  Terminal (ESP32-P4)
                         │
                    UART intern
                         │
                    ESP32-C6  ──WiFi──▶  rangemaster.hostzone.lu
```

All dräi Schrëtt:

1. **Laptop virbereden** — ESP-IDF installéieren
2. **Firmware bauen** — Code compiléieren
3. **Terminal flashen** — Firmware op d'Apparat iwwerdroën

---

## Deel 1 — Laptop virbereden

### 1.1 USB-Treiber installéieren

D'Guition-Board benotzt en **CP2102N** USB-Chip.

1. Öffne: https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers
2. Lued `CP210x Universal Windows Driver` erof
3. Entpack a klick `silabser.inf` → Riets-Klick → **Installéieren**
4. Board mam USB-C Kabel unschléissen (ënnen am Board)
5. Windows-Device Manager opmaachen (`Win+X` → Device Manager) → ënner **Ports (COM & LPT)** erschéngt `Silicon Labs CP210x USB to UART Bridge (COM3)` — notéier d'COM-Nummer

> **Mac/Linux:** Kee Treiber néideg. Port ass `/dev/ttyUSB0` (Linux) oder `/dev/cu.usbserial-*` (Mac).

---

### 1.2 Git installéieren (falls net do)

- Windows: https://git-scm.com/download/win → Standard-Installatioun
- Mac: `xcode-select --install` am Terminal
- Linux: `sudo apt install git`

---

### 1.3 ESP-IDF 5.4 installéieren

#### Windows (einfachst Wee — Offline Installer)

> ⚠️ **Wichteg:** Nëmmen **IDF 5.3.x** benotzen. IDF 5.4.x hänkt am MIPI-DSI-Init (bekannte Bug fir ECO2-Chips). IDF 5.5.x/6.x refuséiert ECO2 ganz.

1. Öffne: https://dl.espressif.com/dl/esp-idf/
2. Lued `ESP-IDF v5.3.x Windows Installer` erof (Sektioun **Previous Releases** falls néideg)
3. Installer starten → all Standardoptioune bäibehalen → **Finish**
4. Um Desktop erschéngt `ESP-IDF 5.3 CMD` — benotze **dësen** fir all weider Commanden

#### Mac / Linux

```bash
mkdir -p ~/esp && cd ~/esp
git clone --branch v5.4 --depth 1 https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32p4
. ./export.sh
```

> **Linux:** Falls `/dev/ttyUSB0` net funktionnéiert: `sudo usermod -aG dialout $USER` → ausloggen/anloggen

---

### 1.4 Range-Master Code erof lueden

```bash
cd C:\Users\<Äre-Numm>        # Windows
# oder:  cd ~                  # Mac/Linux

git clone https://github.com/fshcl-sektioun-wolz/Range-Master.git
cd Range-Master
```

> Falls de Repo privat ass, brauchs de de GitHub-PAT. Kontaktéier den Admin fir Zougang.

---

## Deel 2 — Firmware bauen

**Windows:** Öffne `ESP-IDF 5.4 CMD` (net de normalen CMD).  
**Mac/Linux:** Stell sécher datt `. ~/esp/esp-idf/export.sh` gelaf ass.

```bash
cd Range-Master\artifacts\firmware      # Windows
# oder: cd ~/Range-Master/artifacts/firmware   # Mac/Linux

idf.py set-target esp32p4
idf.py build
```

Den éischte Build dauert **10–15 Minutten** well LVGL a Bibliothéiken compiléiert ginn.  
Spéider Builds dauern nëmmen 1–2 Minutten.

Erfollegräiche Bau gesäit esou aus:
```
Project build complete.
To flash all build output, run:
  idf.py flash
```

---

## Deel 3 — Terminal flashen

### 3.1 Board am Flash-Modus setzen

1. USB-C Kabel an de Laptop stiechen
2. **Knopp `BOOT` gedréckt halen** (klengen Knopp nieft USB-C)
3. **Knopp `RST` kuerz drécken** (danach `RST` lassloossen)
4. Dann `BOOT` lassloossen
5. Am Device Manager erschéngt elo e zusätzleche COM-Port — dat ass de Flash-Modus

> Falls kee separaten BOOT/RST-Knopp siichtbar ass: `idf.py flash` detektéiert de Chip automatesch a setzt ihn selbst an de Flash-Modus.

---

### 3.2 Flashen

```bash
# Windows (COM3 duerch Äre Port ersetzen):
idf.py -p COM3 flash

# Mac:
idf.py -p /dev/cu.usbserial-0001 flash

# Linux:
idf.py -p /dev/ttyUSB0 flash
```

Den Output gesäit esou aus:
```
Connecting...
Chip is ESP32-P4 (revision v0.1)
Uploading stub...
Running stub...
Erasing flash...
Writing at 0x00020000... (100 %)
Leaving...
Hard resetting via RTS pin...
```

Gesamtdauer: **~1 Minutt**.

---

### 3.3 Kontrolléieren ob et funktionnéiert

```bash
idf.py -p COM3 monitor        # Windows
idf.py -p /dev/ttyUSB0 monitor   # Linux
```

Dir sollt am Terminal gesinn:
```
I (350) trapmaster: TrapMaster v1.0.0-phase1 — F.S.H.C.L. SEKTIOUN WOLZ
I (380) jd9365: MIPI-DSI panel init OK  (800x1280)
I (410) gsl3680: Touch firmware uploaded, 26 fingers ready
I (440) ui_manager: Showing DASHBOARD
```

D'Display leeft op. `Ctrl+]` fir Monitor ofschalten.

---

## Deel 4 — Éischte Start: WiFi a Portal-Schlëssel astellen

### 4.1 WiFi-Netz verbinden

1. Haaptmenü → **Astellungen** (Zantrad-Symbol)
2. Tab **WiFi**
3. SSID (Netzwierks-Numm) aginn
4. Passwuert aginn
5. **Verbinden** drécken
6. Status wechselt op `✓ Verbonnen  192.168.x.x`

---

### 4.2 Portal-URL a API-Schlëssel aginn

Den API-Schlëssel kritt dir vum Portal-Admin:

1. **Portal opmaachen:** https://rangemaster.hostzone.lu
2. Aloggen als Admin
3. Linksmenü → **Admin → API-Schlësselen**
4. Schlëssel fir `Terminal Wolz` kopéieren (oder neien erstellen)

Am Terminal:

1. Haaptmenü → **Astellungen**
2. Feld **Server-URL:** `https://rangemaster.hostzone.lu` *(scho viragesat)*
3. Feld **API-Schlëssel:** de kopéierte Schlëssel aginn (Bluetooth-Tastatur oder Touchscreen-Tastatur)
4. **Späicheren** drécken
5. Dashboard → **Sync** drécken → éischte Sync mat dem Portal

---

## Wéi Bluetooth-Tastatur pairen

1. Astellungen → Tab **Bluetooth**
2. **Nei Tastatur pairen** drécken
3. Terminal geet a Pairing-Modus (blinkt 30 Sekonnen)
4. Op der Tastatur `Fn`+`Bluetooth` drécken
5. `TrapMaster KB` am Listeng der Tastatur auswielen
6. Status: `✓ Verbonnen`

---

## Welch Dateien ginn op den Terminal?

| Wat | Wou am Repo |
|-----|-------------|
| Haaptprogramm | `artifacts/firmware/main/main.c` |
| Display-Driver (JD9365) | `artifacts/firmware/main/display/jd9365_panel.c` |
| Touch-Driver (GSL3680) | `artifacts/firmware/main/display/gsl3680_touch.c` |
| Touch-Firmware (Blob) | `artifacts/firmware/main/display/gsl3680_firmware.h` |
| Netzwierk-Bridge zu C6 | `artifacts/firmware/main/net/coprocessor.c` |
| Portal HTTP-Sync | `artifacts/firmware/main/net/http_sync.c` |
| Spillzoustand | `artifacts/firmware/main/store/game_store.c` |
| All Ecrainen (LVGL) | `artifacts/firmware/main/ui/screen_*.c` |
| Hardware-Pinmap | `artifacts/firmware/main/app_config.h` |
| Flash-Partitions | `artifacts/firmware/partitions.csv` |

Alles gëtt automatesch compiléiert mat `idf.py build`. Dir musst keng Dateien manuell kopéieren.

---

## Update: Neit Firmware installéieren

Wann eng nei Firmwareversoun verfügbar ass (GitHub-Push):

```bash
cd Range-Master
git pull
cd artifacts/firmware
idf.py -p COM3 build flash
```

Dat ass alles.

---

## Troubleshooting

| Problem | Léisung |
|---------|---------|
| `A fatal error occurred: Failed to connect` | BOOT-Knopp gedréckt halen während RST drécken. COM-Port korrekt? |
| `No such file or directory: COM3` | Device Manager kontrolléieren. CP2102N Treiber installéiert? |
| Display bleift schwaarze | Monitor opmaachen (`idf.py monitor`) a Feelertext notéieren |
| `gsl3680: firmware upload failed` | Board nei starten (RST), nees probéieren |
| Touch funktionnéiert net | Astellungen → Bluetooth aus, Touch-Kalibratioun an `app_config.h` kontrolléieren |
| Sync feelt schl | WiFi-Verbindung kontrolléieren; API-Schlëssel korrekt? Portal-Log nokucken |
| `idf.py: command not found` | `ESP-IDF 5.4 CMD` benotzen, net normalen CMD |

---

## Hardware-Pinmap (fir Referenz)

| Signal | GPIO |
|--------|------|
| Touch SDA | GPIO 7 |
| Touch SCL | GPIO 8 |
| Touch Reset | GPIO 9 |
| Touch Interrupt | GPIO 10 |
| Backlight PWM | GPIO 26 |
| P4 → C6 TX | GPIO 4 |
| P4 ← C6 RX | GPIO 5 |
| LoRa TX *(Phase 2)* | GPIO 17 |
| LoRa RX *(Phase 2)* | GPIO 18 |

---

*Froen? Kontaktéiert den Club-Admin oder kuckt d'README.md am `artifacts/firmware/` Ordner.*
