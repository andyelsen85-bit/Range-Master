#!/usr/bin/env python3
"""
IDF 5.3.x / ESP32-P4 linker-script patch
Inserts *(.sbss.*) and *(.bss.*) into sections.ld before linking.
NOTE: INSERT AFTER is explicitly incompatible with --enable-non-contiguous-regions
      so we must edit sections.ld in-place rather than appending a new SECTIONS block.
"""
import sys, os, re

path = sys.argv[1]

if not os.path.isfile(path):
    print(f"[patch_sections_ld] WARNING: {path} not found – skipping")
    sys.exit(0)

with open(path, encoding="utf-8") as fh:
    content = fh.read()

if "*(.sbss.*)" in content:
    print("[patch_sections_ld] already patched – nothing to do")
    sys.exit(0)

# ── Diagnostic: show every line that mentions bss so we know the real format ──
bss_lines = [(i+1, l) for i, l in enumerate(content.splitlines())
             if re.search(r'\.(s?bss|COMMON)', l, re.IGNORECASE)]
print("[patch_sections_ld] BSS-related lines in sections.ld:")
for lineno, line in bss_lines[:30]:
    print(f"  {lineno:4d}: {line}")

out = content

# Strategy 1 – add wildcard right after any *(.sbss...) pattern
out = re.sub(r'(\*\(\.sbss\b[^)]*\))', r'\1\n        *(.sbss.*)', out)

# Strategy 2 – add wildcard right after any *(.bss...) pattern
out = re.sub(r'(\*\(\.bss\b[^)]*\))', r'\1\n        *(.bss.*)', out)

# Strategy 3 (fallback) – if still nothing matched, inject before _bss_end
if "*(.sbss.*)" not in out:
    print("[patch_sections_ld] strategies 1+2 missed; trying _bss_end anchor")
    out = re.sub(
        r'(\s*_bss_end\s*=\s*ABSOLUTE\s*\(\s*\.\s*\)\s*;)',
        r'\n        *(.sbss.*)\n        *(.bss.*)\1',
        out
    )

if "*(.sbss.*)" not in out:
    print("[patch_sections_ld] ERROR: all strategies failed – first 60 lines of sections.ld:")
    for i, l in enumerate(content.splitlines()[:60]):
        print(f"  {i+1:4d}: {l}")
    sys.exit(1)

with open(path, "w", encoding="utf-8") as fh:
    fh.write(out)

print("[patch_sections_ld] patched successfully")
