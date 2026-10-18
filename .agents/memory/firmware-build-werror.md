---
name: Firmware warning policy
description: Portability constraints from the Windows ESP-IDF firmware toolchain.
---

The target firmware build treats all warnings as errors, including GCC's restrict-overlap and format-truncation diagnostics. Formatting data copied from one member of a large shared store into another should use a separate temporary buffer, and fixed-format timestamps should be assembled with explicitly bounded pieces.

**Why:** The Windows ESP-IDF build rejected otherwise valid behavior because GCC could not prove that a store-member source and destination did not overlap, and could not prove that unconstrained integer format arguments fit the timestamp buffer.

**How to apply:** Preserve the strict warning policy; fix the proof boundary in code rather than weakening compiler flags. Re-run the Windows target build after firmware changes because the local environment may not contain `idf.py`.

For small firmware image overruns, prefer scoped size optimization before
changing the flash partition layout. Do not move app slots or the FAT storage
region without explicit migration approval.

**Why:** Physical terminals already use this layout. A build-size fix must not
silently change where their existing OTA images and persisted cache data live.

**How to apply:** Preserve global performance optimization for display/DMA hot
paths. Apply size optimization to non-hot-path modules through build options
that also take effect with an existing sdkconfig; do not require deleting it.

The user explicitly approved removing the unused factory app and enlarging
the two OTA slots, but requires the existing FAT offset/size and other data
partitions to stay unchanged. Never erase or format storage for this migration.

**Why:** FAT holds unsynced game-journal data, not merely rebuildable cache.

**How to apply:** First synchronize all games, then use full USB flashing of
the new table and matching app/initial OTA metadata, never app-only OTA or
erase-flash. Preserve NVS/PHY/FAT; verify the generated flash plan and at least
500 KiB app headroom with the native build before delivering changes.

Host tests that extract production functions must preserve helper declaration
order or explicitly check forward declarations. Avoid supplying early mock
definitions for real helpers whose placement matters to C++ compilation.

**Why:** A host mock defined a helper before its callers and masked a missing
production declaration, so regression tests passed while the native build failed.

**How to apply:** Compile the real helper where practical, keeping its
declaration and definition in production order. Host regression success does
not replace a complete native firmware build.

Hosted/Remote optional Kconfig includes need the SDK series (for example 5.3),
not its full patch version. Preserve it in the generated Kconfig environment
as well as CMake's environment.

**Why:** A missing/wrong series silently skipped Remote's optional configuration
and produced missing WiFi buffer symbols. CMake-only environment changes did
not survive Ninja's later Kconfig regeneration.

**How to apply:** Derive the series from the actual SDK's version.cmake.
Use the pinned SDK and exported tool environment, including ROM ELF files.
Host libc compatibility is insufficient: target newlib also lacks timegm.

The container's native IDF invocation needs `IDF_PYTHON_ENV_PATH` pointing to
the existing workspace `.pythonlibs`; IDF's default Python environment path
does not exist. Use the installed ROM ELF directory matching the pinned SDK.

**Why:** Calling the existing IDF tool with workspace Python still fails before
compilation if IDF tries to validate its nonexistent default environment.

**How to apply:** Reuse the installed workspace Python and SDK tools; do not
create a virtual environment or guess a newer ROM asset version.

In this workspace, a minimal native P4 build can use the existing RISC-V
compiler and `.pythonlibs` without sourcing the SDK export script. Set
`IDF_PYTHON_CHECK_CONSTRAINTS=no` when the SDK tries to open a nonexistent
default constraints file; keep using the pinned SDK and installed dependencies.

**Why:** The export script checks unrelated Xtensa/debugger tools that are not
installed here, and the default constraints path can reject the valid workspace
Python environment before compilation starts.

**How to apply:** Supply the installed RISC-V compiler, Python, IDF and ROM-ELF
paths explicitly for native P4 checks. Do not install unrelated toolchains or
create a virtual environment just to satisfy the export script.