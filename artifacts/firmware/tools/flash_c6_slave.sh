#!/usr/bin/env bash
# =============================================================================
# flash_c6_slave.sh — Build and flash the ESP-Hosted slave (co-processor)
#                     firmware onto the ESP32-C6 (Guition JC8012P4A1C, CN5).
#
# WHY THIS SCRIPT EXISTS
# ─────────────────────
# The P4 host firmware is built with espressif/esp_hosted == 2.12.12.
# At runtime the host and slave exchange a version handshake; if they differ
# the host logs:
#   W (...) transport: Version mismatch: Host [2.12.0] > Co-proc [2.3.0]
# and WiFi scans time out.  This script fetches the SAME component version
# (2.12.12), builds the coprocessor (slave) project included in the component
# package, configures it for SDIO transport on ESP32-C6, then flashes via CN5.
#
# BOARD WIRING — Guition JC8012P4A1C
# ───────────────────────────────────
# The P4 (SDIO master) and C6 (SDIO slave) are wired as follows on the PCB
# (source: profi-max/JC8012P4A1_BSP_ESP32P4, app_config.h):
#
#   P4 GPIO (master)   Signal     C6 GPIO (slave, hardware-fixed)
#   ──────────────────────────────────────────────────────────────
#   GPIO18             SDIO CLK   GPIO19  (fixed IO_MUX; no remapping possible)
#   GPIO19             SDIO CMD   GPIO18  (fixed IO_MUX)
#   GPIO14             SDIO D0    GPIO20  (fixed IO_MUX)
#   GPIO15             SDIO D1    GPIO21  (fixed IO_MUX)
#   GPIO16             SDIO D2    GPIO22  (fixed IO_MUX)
#   GPIO17             SDIO D3    GPIO23  (fixed IO_MUX)
#   GPIO54             C6 RST     EN      (hardware reset)
#   GPIO6              C6 WAKEUP  GPIO10  (wakeup/boot handshake)
#
# The ESP32-C6 SDIO slave controller uses those GPIOs through the hardware
# IO_MUX — they are NOT configurable in firmware.  The slave sdkconfig only
# selects the transport (SDIO vs SPI); pin overrides are impossible and
# therefore not needed.  The factory C6 slave firmware already used these
# exact pins (ESP-Hosted-MCU SDIO mode), confirming the PCB is wired correctly.
#
# USAGE
# ─────
#   ./tools/flash_c6_slave.sh [PORT]
#
#   PORT  — C6 serial port.  Defaults to /dev/ttyUSB1.
#           On Windows use COMx inside Git Bash (IDF env active).
#           On Mac the port is usually /dev/cu.usbserial-*
#
# HARDWARE CONNECTIONS
# ────────────────────
#   CN4 (lower USB-C)  → ESP32-P4 main SoC   ← flash P4 firmware here
#   CN5 (upper USB-C)  → ESP32-C6 radio       ← flash C6 slave HERE
#
# PREREQUISITES
# ─────────────
#   1.  ESP-IDF 5.3.x activated:
#         . $IDF_PATH/export.sh
#       (Use 5.3.x — NOT 5.4.x/5.5.x; the P4 ECO2 display requires 5.3.x)
#   2.  Board connected via CN5 (upper USB-C).
#   3.  No other monitor holding the port open.
#
# AFTER FLASHING
# ──────────────
#   1.  Press RST on the board (or power-cycle).
#   2.  Flash/boot the P4 as usual:
#         cd artifacts/firmware && idf.py -p /dev/ttyUSB0 flash monitor
#   3.  In the P4 boot log confirm the warning is GONE:
#         W (...) transport: Version mismatch  ← must NOT appear
#   4.  WiFi screen → Scan → completes in a few seconds.
# =============================================================================

set -euo pipefail

# ── Configuration ──────────────────────────────────────────────────────────
ESP_HOSTED_VERSION="2.12.12"   # MUST match espressif/esp_hosted in idf_component.yml
TARGET="esp32c6"
PORT="${1:-/dev/ttyUSB1}"
WORKDIR="$(mktemp -d)"

# ── Helpers ────────────────────────────────────────────────────────────────
info()  { echo "[INFO]  $*"; }
warn()  { echo "[WARN]  $*"; }
die()   { echo "[ERROR] $*" >&2; exit 1; }
cleanup() { rm -rf "$WORKDIR"; }
trap cleanup EXIT

echo ""
echo "=================================================================="
echo "  ESP-Hosted C6 Slave Flasher"
echo "  Component version : espressif/esp_hosted == $ESP_HOSTED_VERSION"
echo "  Target            : $TARGET (SDIO slave — fixed pins GPIO18-23)"
echo "  Flash port        : $PORT"
echo "  Temp dir          : $WORKDIR"
echo "=================================================================="
echo ""

# ── 1. Sanity checks ───────────────────────────────────────────────────────
[ -z "${IDF_PATH:-}" ] && die \
    "IDF_PATH not set.  Activate IDF 5.3.x first:  . \$IDF_PATH/export.sh"

info "IDF: $IDF_PATH"
idf.py --version

python3 -c "import idf_component_manager" 2>/dev/null || \
    die "idf-component-manager not found.  Ensure IDF 5.3.x environment is active."

# ── 2. Download the component at the pinned version ────────────────────────
# We create a minimal throw-away IDF app whose only purpose is to trigger
# the component manager to download espressif/esp_hosted at the exact version.
# The slave project lives INSIDE the downloaded component package; we build
# it there (not by copying slave/ to a temp dir) so that sibling directories
# such as common/ remain reachable by the slave's CMakeLists via relative paths.
info "Fetching espressif/esp_hosted==$ESP_HOSTED_VERSION from the registry…"
FETCH_DIR="$WORKDIR/fetcher"
mkdir -p "$FETCH_DIR/main"

cat > "$FETCH_DIR/CMakeLists.txt" <<'CMEOF'
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_hosted_fetcher)
CMEOF

cat > "$FETCH_DIR/main/CMakeLists.txt" <<'CMEOF'
idf_component_register(SRCS "stub.c")
CMEOF

cat > "$FETCH_DIR/main/stub.c" <<'CMEOF'
void app_main(void) {}
CMEOF

cat > "$FETCH_DIR/main/idf_component.yml" <<COMPEOF
dependencies:
  espressif/esp_hosted:
    version: "==$ESP_HOSTED_VERSION"
COMPEOF

cd "$FETCH_DIR"
idf.py set-target "$TARGET"
idf.py update-dependencies   # downloads into managed_components/

COMP_DIR="$FETCH_DIR/managed_components/espressif__esp_hosted"
[ -d "$COMP_DIR" ] || die \
    "Component not downloaded.  Check network access and the component registry."
info "Component downloaded to: $COMP_DIR"

# ── 3. Locate the slave project inside the component package ───────────────
# The 2.12.12 package contains a slave/ directory alongside common/.
# We must build from within the component directory (not a copy) so that
# the slave's CMakeLists can resolve ../common/ and other sibling dirs.
SLAVE_DIR=""
for candidate in \
    "$COMP_DIR/slave" \
    "$COMP_DIR/coprocessor" \
    "$COMP_DIR/examples/mcu_hosted_sdio_sdmmc_combined/cp" \
    "$COMP_DIR/examples/mcu_hosted_sdio/cp" \
    "$COMP_DIR/examples/slave"
do
    if [ -f "$candidate/CMakeLists.txt" ]; then
        SLAVE_DIR="$candidate"
        break
    fi
done

if [ -z "$SLAVE_DIR" ]; then
    # Wider search: any project() CMakeLists in a slave-related subdirectory.
    SLAVE_DIR=$(find "$COMP_DIR" -name "CMakeLists.txt" \
        | xargs grep -l "^project(" 2>/dev/null \
        | grep -iE "slave|copro|network_adapter" \
        | head -1 | xargs dirname 2>/dev/null || true)
fi

if [ -z "$SLAVE_DIR" ]; then
    warn "Could not find a slave/coprocessor project in the component package."
    warn "Component directory (depth 3):"
    find "$COMP_DIR" -maxdepth 3 -name "CMakeLists.txt" | sed "s|$COMP_DIR/||"
    die "Slave project not found — cannot continue."
fi
info "Slave project: $SLAVE_DIR"

# ── 4. Configure SDIO transport ────────────────────────────────────────────
# The ESP32-C6 SDIO slave uses hardware-fixed IO_MUX paths:
#   CLK=GPIO19  CMD=GPIO18  D0=GPIO20  D1=GPIO21  D2=GPIO22  D3=GPIO23
# These pins connect to the P4 master via the Guition board PCB and CANNOT
# be changed in firmware — no sdkconfig pin-override is possible or needed.
#
# What we DO configure here:
#   CONFIG_ESP_SDIO_HOST_INTERFACE=y  — select SDIO (vs SPI) transport.
#     The ESP32-C6 Kconfig already defaults to SDIO when
#     SOC_SDIO_SLAVE_SUPPORTED=y, but we set it explicitly to ensure a
#     stale sdkconfig or merged default cannot override the choice.
#   CONFIG_SDIO_DAT2_DISABLED=n  — use all 4 data lines (D0-D3).
#     The Guition board wires all four SDIO data lines between P4 and C6;
#     leaving DAT2 enabled (disabled=n) is required for 4-bit SDIO mode.
SDKCONFIG_EXTRA="$WORKDIR/sdkconfig.board.defaults"
cat > "$SDKCONFIG_EXTRA" <<'SDKEOF'
# ESP-Hosted slave transport configuration — Guition JC8012P4A1C
#
# C6 SDIO pins are hardware-fixed IO_MUX paths; no GPIO overrides exist:
#   CLK=GPIO19, CMD=GPIO18, D0=GPIO20, D1=GPIO21, D2=GPIO22, D3=GPIO23
# The board PCB connects these to P4 GPIOs 18/19/14-17 respectively.
#
# Select SDIO transport (Kconfig choice — only this positive assignment needed).
CONFIG_ESP_SDIO_HOST_INTERFACE=y
# Enable all 4 SDIO data lines (D0-D3 all physically wired on this board).
CONFIG_SDIO_DAT2_DISABLED=n
SDKEOF

# IDF respects SDKCONFIG_DEFAULTS as a semicolon-separated list of files,
# each merged in order.  The board defaults are appended last so they win.
EXISTING=""
[ -f "$SLAVE_DIR/sdkconfig.defaults" ] && \
    EXISTING="$SLAVE_DIR/sdkconfig.defaults"
[ -f "$SLAVE_DIR/sdkconfig.defaults.esp32c6" ] && \
    EXISTING="${EXISTING:+$EXISTING;}$SLAVE_DIR/sdkconfig.defaults.esp32c6"
export SDKCONFIG_DEFAULTS="${EXISTING:+$EXISTING;}$SDKCONFIG_EXTRA"
info "SDKCONFIG_DEFAULTS: $SDKCONFIG_DEFAULTS"

# ── 5. Build the slave ─────────────────────────────────────────────────────
info "Building C6 slave firmware (first run: 3-8 min)…"
cd "$SLAVE_DIR"
idf.py set-target "$TARGET"
idf.py build

# ── 6. Flash the C6 ────────────────────────────────────────────────────────
echo ""
info "────────────────────────────────────────────────────────────────────"
info "  Flashing C6 via $PORT"
info "  ⚠  Use CN5 (UPPER USB-C) — not CN4 which goes to the P4."
info "────────────────────────────────────────────────────────────────────"
echo ""
idf.py -p "$PORT" flash

# EXIT trap handles cleanup.
echo ""
echo "=================================================================="
echo "  C6 slave $ESP_HOSTED_VERSION flashed successfully!"
echo ""
echo "  Next:"
echo "  1. Press RST on the board (or power-cycle)."
echo "  2. Flash/boot the P4:"
echo "       cd artifacts/firmware"
echo "       idf.py -p /dev/ttyUSB0 flash monitor"
echo "  3. Confirm this warning is ABSENT in the P4 boot log:"
echo "       W (...) transport: Version mismatch"
echo "  4. WiFi screen → Scan → should complete in a few seconds."
echo "=================================================================="
