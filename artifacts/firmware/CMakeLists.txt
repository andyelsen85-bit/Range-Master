cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(trapmaster_firmware)

# ── IDF 5.3.x / ESP32-P4 PSRAM-overflow linker-script patch ──────────────
# Injects a PSRAM output section before .dram0.bss so that toolchain BSS
# (libstdc++.a, libc.a) and selected IDF archives are routed to PSRAM,
# keeping internal SRAM within its region limit.  See tools/patch_sections_ld.py.
idf_build_get_property(python PYTHON)
add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}.elf PRE_LINK
    COMMAND ${python}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_sections_ld.py"
            "${CMAKE_BINARY_DIR}/esp-idf/esp_system/ld/sections.ld"
    COMMENT "Patching sections.ld: inject PSRAM BSS overflow section"
    VERBATIM
)
