#!/usr/bin/env python3
"""
Revert the patch applied by patch_idf_install.py.
Restores the original target_link_options() line that injects
-Wl,--enable-non-contiguous-regions.

Usage:
    python tools/revert_idf_patch.py [idf_root]
"""
import sys, os, re, pathlib

def find_idf_root():
    if len(sys.argv) > 1:
        return pathlib.Path(sys.argv[1])
    env = os.environ.get("IDF_PATH")
    if env:
        return pathlib.Path(env)
    default = pathlib.Path(r"C:\Espressif\frameworks\esp-idf-v5.3.5")
    if default.exists():
        return default
    raise SystemExit("Cannot find IDF root.")

idf_root = find_idf_root()
print(f"IDF root: {idf_root}")

reverted = False
for f in sorted(idf_root.rglob("CMakeLists.txt")):
    try:
        text = f.read_text(encoding="utf-8", errors="replace")
        if "[NCR-PATCH]" not in text:
            continue
        # Remove the comment line and un-comment the original
        new_text = re.sub(
            r"# \[NCR-PATCH\][^\n]*\n# (target_link_options[^\n]*)",
            r"\1",
            text
        )
        if new_text != text:
            f.write_text(new_text, encoding="utf-8")
            print(f"  Reverted: {f}")
            reverted = True
    except Exception:
        pass

if reverted:
    print()
    print("IDF restored. Now:")
    print("  1. del sdkconfig")
    print("  2. rmdir /s /q build")
    print("  3. idf.py build")
else:
    print("Nothing to revert (patch not found).")
