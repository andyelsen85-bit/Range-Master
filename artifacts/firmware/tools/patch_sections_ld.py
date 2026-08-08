#!/usr/bin/env python3
"""
IDF 5.3.x / ESP32-P4 linker-script patch (safety / no-op if already correct)

sections.ld on a fresh IDF 5.3.5 ESP32-P4 build already contains .sbss.*
embedded in a combined multi-pattern line:
    *(.dynbss .dynsbss ... .sbss .sbss.* .sbss2 ... .scommon .share.mem)

If that wildcard is present we exit immediately.  The real root cause of
--enable-non-contiguous-regions discards is DRAM overflow from components
enabled by default (Bluetooth, OpenThread), not a missing placement rule.
Those are disabled in sdkconfig.defaults.
"""
import sys, os, re

path = sys.argv[1]

if not os.path.isfile(path):
    print(f"[patch_sections_ld] WARNING: {path} not found – skipping")
    sys.exit(0)

with open(path, encoding="utf-8") as fh:
    content = fh.read()

# Check for .sbss.* as a substring anywhere in the file (handles combined patterns)
if re.search(r'\.sbss\.\*', content):
    print("[patch_sections_ld] .sbss.* already present in sections.ld – no patch needed")
    sys.exit(0)

print("[patch_sections_ld] .sbss.* NOT found – patching")

# Only patch if the wildcard is genuinely missing.
# Use a precise anchor: look for the _dram0_bss_end symbol (not _rtc_bss_end etc.)
# by requiring a word boundary before _bss_end.
out = re.sub(
    r'(?<![A-Za-z0-9_])(_dram0_bss_end\s*=\s*ABSOLUTE\s*\(\s*\.\s*\)\s*;)',
    r'*(.sbss.*)\n        *(.bss.*)\n        \1',
    content
)

if re.search(r'\.sbss\.\*', out):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(out)
    print("[patch_sections_ld] patched via _dram0_bss_end anchor")
else:
    print("[patch_sections_ld] WARNING: could not patch – build may fail")
    # Do NOT write the file; leave sections.ld untouched rather than corrupt it.
    sys.exit(1)
