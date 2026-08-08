#!/usr/bin/env python3
"""
IDF 5.3.x / ESP32-P4 linker-script patch.

Root cause
----------
IDF 5.3.x for ESP32-P4 has no separate PSRAM output section for BSS.
Everything — including toolchain BSS from libstdc++.a and libc.a — is
placed in .dram0.bss (internal SRAM).  When the DRAM region fills up,
--enable-non-contiguous-regions silently discards the overflow instead of
redirecting it to PSRAM.

Fix
---
We inject a new NOLOAD output section BEFORE .dram0.bss that routes
specific archive libraries' .bss/.sbss into the PSRAM region.  Because
the section appears first, the linker assigns those symbols to PSRAM;
.dram0.bss then only sees the IDF runtime sections that genuinely need
internal SRAM.

Memory region discovery
-----------------------
We find the PSRAM region automatically from memory.ld by picking the
MEMORY region with the largest declared length (32 MB on this board).
"""

import sys, os, re, pathlib

def _find_psram_region(memory_ld: pathlib.Path) -> str:
    """Return the name of the largest MEMORY region (most likely PSRAM)."""
    text = memory_ld.read_text(encoding="utf-8", errors="replace")
    best_name, best_len = "ext_ram_seg", 0
    for m in re.finditer(
            r'(\w+)\s*\([^)]*\)\s*:\s*org\s*=\s*(\w+)\s*,\s*len\s*=\s*(\w+)',
            text, re.IGNORECASE):
        name, length_str = m.group(1), m.group(3)
        try:
            length = int(length_str, 0)
        except ValueError:
            continue
        if length > best_len:
            best_len = length
            best_name = name
    print(f"[patch_sections_ld] PSRAM region = '{best_name}' ({best_len:#x} bytes)")
    return best_name


def _build_overflow_section(psram_region: str) -> str:
    """
    A NOLOAD output section placed BEFORE .dram0.bss that pulls toolchain
    and selected IDF archive BSS/SBSS into PSRAM.

    Library names are matched with a leading wildcard so any path prefix
    (e.g. C:/Espressif/tools/…/lib/rv32imafc…/) is ignored.
    """
    libs = [
        # RISC-V toolchain (always linked, always have SBSS)
        "*libstdc++.a",
        "*libc.a",
        "*libgcc.a",
        # IDF components whose SBSS overflows DRAM on ESP32-P4
        "*liblwip.a",
        "*libpthread.a",
        "*libnewlib.a",
        "*libesp_timer.a",
        "*libesp_driver_uart.a",
        "*libesp_driver_usb_serial_jtag.a",
        "*libapp_update.a",
        "*libmbedcrypto.a",
        "*libmbedtls.a",
        "*libmbedx509.a",
        "*libesp-tls.a",
    ]
    entries = "\n".join(
        f"        {lib}:*(.bss .bss.* .sbss .sbss.* .sbss2 .sbss2.* COMMON)"
        for lib in libs
    )
    return f"""
  /* ── IDF 5.3.x / ESP32-P4 PSRAM overflow fix (injected by patch_sections_ld.py) ──
   * Routes toolchain and selected IDF archive BSS into PSRAM so that
   * .dram0.bss stays within the internal SRAM region and
   * --enable-non-contiguous-regions does not silently discard sections.
   * All listed archives are safe in cache-mapped PSRAM on ESP32-P4.      */
  .psram_bss_overflow (NOLOAD) :
  {{
    . = ALIGN(8);
    _psram_bss_overflow_start = ABSOLUTE(.);
{entries}
    _psram_bss_overflow_end = ABSOLUTE(.);
    . = ALIGN(8);
  }} > {psram_region}
"""


# ── Main ──────────────────────────────────────────────────────────────────

sections_ld_path = pathlib.Path(sys.argv[1])
memory_ld_path   = sections_ld_path.parent / "memory.ld"

if not sections_ld_path.is_file():
    print(f"[patch_sections_ld] WARNING: {sections_ld_path} not found – skipping")
    sys.exit(0)

content = sections_ld_path.read_text(encoding="utf-8", errors="replace")

if "psram_bss_overflow" in content:
    print("[patch_sections_ld] already patched – skipping")
    sys.exit(0)

# Find the PSRAM region
if memory_ld_path.is_file():
    psram_region = _find_psram_region(memory_ld_path)
else:
    psram_region = "ext_ram_seg"
    print(f"[patch_sections_ld] memory.ld not found; assuming region = '{psram_region}'")

# Locate .dram0.bss in sections.ld and insert the overflow section before it
anchor = re.search(r'\n([ \t]+\.dram0\.bss\b)', content)
if not anchor:
    print("[patch_sections_ld] ERROR: could not find .dram0.bss in sections.ld")
    # Print section headers to aid diagnosis
    for m in re.finditer(r'\n[ \t]+(\.[\w.]+)\s*(?:\(NOLOAD\))?\s*:', content):
        print(f"  section: {m.group(1)}")
    sys.exit(1)

insert_pos  = anchor.start(1)          # position of the leading whitespace
overflow    = _build_overflow_section(psram_region)
new_content = content[:insert_pos] + overflow + content[insert_pos:]

sections_ld_path.write_text(new_content, encoding="utf-8")
print(f"[patch_sections_ld] inserted .psram_bss_overflow before .dram0.bss "
      f"(region={psram_region})")
