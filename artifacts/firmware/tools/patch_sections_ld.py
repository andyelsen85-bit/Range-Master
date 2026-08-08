#!/usr/bin/env python3
"""
IDF 5.3.x / ESP32-P4 linker-script patch
=========================================
IDF unconditionally passes --enable-non-contiguous-regions for ESP32-P4+PSRAM.
Under that flag the linker discards every section that has no explicit placement
rule.  The IDF 5.3.x sections.ld.in for ESP32-P4 is missing *(.sbss.*) and
*(.bss.*) wildcard entries (fixed in IDF 5.4.x).

This script runs PRE_LINK and inserts those two lines into the already-generated
sections.ld so that every .sbss.XXX / .bss.XXX subsection is placed in DRAM
instead of being silently discarded.
"""
import sys, os, re

path = sys.argv[1]

if not os.path.isfile(path):
    print(f"[patch_sections_ld] WARNING: {path} not found – skipping")
    sys.exit(0)

with open(path, encoding="utf-8") as fh:
    src = fh.read()

if "*(.sbss.*)" in src:
    print("[patch_sections_ld] already patched – nothing to do")
    sys.exit(0)

# Insert *(.sbss.*) right after every *(.sbss ) pattern (with optional spaces)
out = re.sub(r'(\*\(\.sbss[ \t]*\))', r'\1\n        *(.sbss.*)', src)

# Insert *(.bss.*)  right after every *(.bss )  pattern
out = re.sub(r'(\*\(\.bss[ \t]*\))',  r'\1\n        *(.bss.*)',  out)

if out == src:
    # Fallback: just append an extra SECTIONS block with INSERT AFTER
    print("[patch_sections_ld] WARNING: could not find .sbss/.bss anchors; "
          "trying INSERT AFTER fallback")
    out = src + """

/* ---- IDF 5.3.x ESP32-P4 BSS wildcard fix (appended by patch_sections_ld.py) ---- */
SECTIONS
{
  .dram0.bss_wildcards (NOLOAD) :
  {
    *(.sbss.*)
    *(.bss.*)
  }
}
INSERT AFTER .dram0.bss;
"""

with open(path, "w", encoding="utf-8") as fh:
    fh.write(out)

print(f"[patch_sections_ld] patched {path}")
