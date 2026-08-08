#!/usr/bin/env python3
"""
One-time patch for the IDF 5.3.x installation on this machine.

IDF 5.3.x injects -Wl,--enable-non-contiguous-regions for ESP32-P4+PSRAM via
a generator expression in a component CMakeLists.txt.  That flag causes the
linker to silently discard ~70 KB of core BSS (libc, libstdc++, newlib,
pthread, esp_timer) because sections.ld.in for IDF 5.3.x / ESP32-P4 does not
handle .sbss.* correctly when the flag is active.  The bug is fixed in IDF
5.4.x, but 5.4.x breaks DSI bus init on ECO2 silicon.

This script comments out the offending target_link_options() call.
Run it ONCE after installing IDF.  Then delete the firmware build/ directory
and rebuild.

Usage:
    python tools/patch_idf_install.py [idf_root]

idf_root defaults to %IDF_PATH% or C:/Espressif/frameworks/esp-idf-v5.3.5
"""

import sys, os, re, pathlib

FLAG = "enable-non-contiguous-regions"

def find_idf_root():
    if len(sys.argv) > 1:
        return pathlib.Path(sys.argv[1])
    env = os.environ.get("IDF_PATH")
    if env:
        return pathlib.Path(env)
    default = pathlib.Path(r"C:\Espressif\frameworks\esp-idf-v5.3.5")
    if default.exists():
        return default
    raise SystemExit("Cannot find IDF root. Pass it as an argument: "
                     "python tools/patch_idf_install.py C:/path/to/esp-idf")

def patch_file(path: pathlib.Path) -> bool:
    text = path.read_text(encoding="utf-8", errors="replace")
    if FLAG not in text:
        return False

    # Already patched?
    if "# [NCR-PATCH]" in text:
        print(f"  Already patched: {path}")
        return True

    new_text = re.sub(
        r"([ \t]*target_link_options[^\n]*enable-non-contiguous-regions[^\n]*)",
        r"# [NCR-PATCH] disabled for IDF 5.3.x ESP32-P4 .sbss bug\n# \1",
        text
    )
    if new_text == text:
        return False

    path.write_text(new_text, encoding="utf-8")
    print(f"  Patched: {path}")
    return True


idf_root = find_idf_root()
print(f"IDF root: {idf_root}")

# Priority candidates (most likely first)
candidates = [
    idf_root / "components" / "esp_mm"     / "CMakeLists.txt",
    idf_root / "components" / "esp_psram"  / "CMakeLists.txt",
    idf_root / "components" / "esp_system" / "CMakeLists.txt",
    idf_root / "components" / "hal"        / "CMakeLists.txt",
]

patched = False
for c in candidates:
    if c.exists() and patch_file(c):
        patched = True

if not patched:
    print("Flag not found in priority candidates — scanning all IDF CMakeLists.txt files...")
    for f in sorted(idf_root.rglob("CMakeLists.txt")):
        try:
            if patch_file(f):
                patched = True
        except Exception:
            pass

if patched:
    print()
    print("Done. Now:")
    print("  1. rmdir /s /q build    (delete old build dir so CMake re-reads IDF)")
    print("  2. idf.py build")
else:
    print()
    print("Flag not found anywhere in the IDF installation.")
    print("It may already be absent, or the IDF path is wrong.")
    print("Try passing the path explicitly:")
    print("  python tools/patch_idf_install.py C:\\Espressif\\frameworks\\esp-idf-v5.3.5")
