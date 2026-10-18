#!/usr/bin/env python3
"""Check the OTA-only layout and, optionally, a real IDF build. Never flashes."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct


FIRMWARE = Path(__file__).resolve().parents[1]
MIN_HEADROOM = 500 * 1024  # Stricter than 500 decimal KB.
# name: (type, subtype, offset, size). The non-app regions are migration invariants.
EXPECTED = {
    "nvs": (1, 2, 0x9000, 0x6000),
    "otadata": (1, 0, 0xF000, 0x2000),
    "phy_init": (1, 1, 0x11000, 0x1000),
    "ota_0": (0, 0x10, 0x20000, 0x330000),
    "ota_1": (0, 0x11, 0x350000, 0x330000),
    "storage": (1, 0x81, 0x680000, 0x980000),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def csv_layout(path):
    types = {"app": 0, "data": 1}
    subtypes = {"nvs": 2, "ota": 0, "phy": 1, "fat": 0x81,
                "ota_0": 0x10, "ota_1": 0x11, "factory": 0}
    result = {}
    for row in csv.reader(path.read_text().splitlines()):
        if not row or row[0].lstrip().startswith("#"):
            continue
        name, kind, subtype, offset, size, *flags = (cell.strip() for cell in row)
        require(name not in result, f"Duplicate partition: {name}")
        require(not any(flags), f"Unexpected partition flags: {name}")
        result[name] = (types[kind], subtypes[subtype], int(offset, 0), int(size, 0))
    return result


def binary_layout(path):
    data = path.read_bytes()
    result = {}
    found_md5 = False
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if entry == b"\xff" * 32:
            break
        magic = struct.unpack_from("<H", entry)[0]
        if magic == 0xEBEB:
            require(entry[16:] == hashlib.md5(data[:offset]).digest(),
                    "Generated partition-table checksum mismatch")
            found_md5 = True
            break
        require(magic == 0x50AA, "Invalid generated partition entry")
        _, kind, subtype, start, size, label, flags = struct.unpack("<HBBII16sI", entry)
        name = label.split(b"\0", 1)[0].decode("ascii")
        require(name not in result and flags == 0, f"Invalid generated partition: {name}")
        result[name] = (kind, subtype, start, size)
    require(found_md5, "Missing generated partition-table checksum")
    return result


def sdkconfig(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines()
                if line.startswith("CONFIG_") and "=" in line)


def check_config(config, label):
    require(config.get("CONFIG_PARTITION_TABLE_CUSTOM") == "y",
            f"{label}: custom partition table must be enabled")
    require(Path(config.get("CONFIG_PARTITION_TABLE_CUSTOM_FILENAME", "").strip('"')).name
            == "partitions.csv", f"{label}: wrong custom partition table")
    require(int(config.get("CONFIG_PARTITION_TABLE_OFFSET", "0"), 0) == 0x8000,
            f"{label}: partition-table offset changed")
    for key in ("CONFIG_BOOTLOADER_FACTORY_RESET", "CONFIG_BOOTLOADER_APP_TEST"):
        require(config.get(key) != "y", f"{label}: disable {key} before flashing")


def check_layout():
    layout = csv_layout(FIRMWARE / "partitions.csv")
    require(layout == EXPECTED, "Partition CSV differs from the approved OTA-only layout")
    check_config(sdkconfig(FIRMWARE / "sdkconfig.defaults"), "defaults")
    end = 0
    for name, (kind, _, start, size) in sorted(layout.items(), key=lambda item: item[1][2]):
        require(start >= end and size > 0, f"Overlapping/empty partition: {name}")
        require(start % (0x10000 if kind == 0 else 0x1000) == 0,
                f"Misaligned partition: {name}")
        end = start + size
    require(end == 0x1000000, "Layout must end at the existing 16 MiB flash boundary")
    print("PASS CSV/defaults: two 0x330000 OTA slots; NVS/otadata/PHY/FAT unchanged")


def check_build(build):
    build = build.resolve()
    description = json.loads((build / "project_description.json").read_text())
    config_path = Path(description["config_file"])
    config = sdkconfig(config_path if config_path.is_absolute() else build / config_path)
    check_config(config, "generated sdkconfig")
    require(config.get("CONFIG_IDF_TARGET") == '"esp32p4"', "Wrong build target")
    require(config.get("CONFIG_ESPTOOLPY_FLASHSIZE") == '"16MB"', "Wrong flash capacity")
    require(binary_layout(build / "partition_table" / "partition-table.bin") == EXPECTED,
            "Generated binary does not match the approved layout")
    # Check bootloader's independently generated settings too, when present.
    boot_config = build / "bootloader" / "config" / "sdkconfig.json"
    boot = json.loads(boot_config.read_text())
    for key in ("BOOTLOADER_FACTORY_RESET", "BOOTLOADER_APP_TEST"):
        require(not boot.get(key, False), f"Bootloader requires disabled option {key}")

    manifest = json.loads((build / "flasher_args.json").read_text())
    files = {int(address, 0): build / name for address, name
             in manifest["flash_files"].items()}
    require(len(manifest["flash_files"]) == 4
            and set(files) == {0x2000, 0x8000, 0xF000, 0x20000},
            "Full flash must contain only bootloader, table, initial OTA data and ota_0")
    require(files[0x8000].resolve()
            == (build / "partition_table" / "partition-table.bin").resolve(),
            "Flash plan uses a different partition table")
    require(files[0xF000].read_bytes() == b"\xff" * EXPECTED["otadata"][3],
            "Initial OTA metadata must be erased so the first OTA slot boots")
    for address, path in files.items():
        # esptool erases whole sectors, not just the exact image bytes.
        start = address & ~0xFFF
        end = (address + path.stat().st_size + 0xFFF) & ~0xFFF
        for name in ("nvs", "phy_init", "storage"):
            _, _, protected, length = EXPECTED[name]
            require(end <= protected or start >= protected + length,
                    f"Flash file {path.name} would erase/write protected {name}")
    app_size = files[0x20000].stat().st_size
    for name in ("ota_0", "ota_1"):
        free = EXPECTED[name][3] - app_size
        require(free >= MIN_HEADROOM,
                f"{name}: only {free} bytes free, need at least {MIN_HEADROOM}")
    print(f"PASS native build/flash plan: app {app_size:#x} ({app_size:,} bytes); "
          f"headroom {EXPECTED['ota_0'][3] - app_size:,} bytes per slot; "
          "initial OTA data selects ota_0; protected regions untouched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", nargs="?", type=Path, help="Completed idf.py build directory")
    args = parser.parse_args()
    check_layout()
    if args.build is not None:
        check_build(args.build)
