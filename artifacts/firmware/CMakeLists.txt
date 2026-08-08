cmake_minimum_required(VERSION 3.16)

# ── Managed components (LVGL v9, esp_lvgl_port) ───────────────────────────
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/components"
)

# Pull in managed components declared in idf_component.yml
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(trapmaster_firmware)

# ── IDF 5.3.x / ESP32-P4 linker-script patch ─────────────────────────────
# IDF unconditionally passes --enable-non-contiguous-regions for ESP32-P4+PSRAM.
# The IDF 5.3.x sections.ld.in is missing *(.sbss.*) / *(.bss.*) placement
# (fixed in IDF 5.4.x).  We patch the GENERATED sections.ld right before the
# link step so those wildcard subsections land in dram0_0_seg instead of being
# silently discarded.
idf_build_get_property(python PYTHON)
set(_sections_ld
    "${CMAKE_BINARY_DIR}/esp-idf/esp_system/ld/sections.ld")
set(_patch_script
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_sections_ld.py")

add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}.elf PRE_LINK
    COMMAND ${python} ${_patch_script} ${_sections_ld}
    COMMENT "Patching sections.ld: add .sbss.*/.bss.* placement (IDF 5.3.x fix)"
    VERBATIM
)
