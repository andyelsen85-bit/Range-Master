cmake_minimum_required(VERSION 3.16)

# ── Managed components (LVGL v9, esp_lvgl_port) ───────────────────────────
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/components"
)

# Pull in managed components declared in idf_component.yml
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(trapmaster_firmware)

# ── IDF 5.3.x / ESP32-P4 linker fix ──────────────────────────────────────
# On ESP32-P4 + PSRAM, IDF unconditionally injects --enable-non-contiguous-
# regions via esp_psram's INTERFACE_LINK_OPTIONS.  The IDF 5.3.x linker
# script is missing .sbss.* / .bss.* wildcard placement rules, so the linker
# silently discards those sections and the link fails with 70 KB of errors.
# This is fixed in IDF 5.4.x; the workaround here is to remove the flag.
# Our PSRAM usage is heap-only (frame buffers via heap_caps_malloc), so
# we do not need XIP-from-PSRAM and the flag is safe to drop.
cmake_policy(SET CMP0079 NEW)
foreach(_tgt esp_psram esp_system esp_mm)
    if(TARGET ${_tgt})
        get_target_property(_opts ${_tgt} INTERFACE_LINK_OPTIONS)
        if(_opts)
            list(REMOVE_ITEM _opts "-Wl,--enable-non-contiguous-regions")
            set_target_properties(${_tgt} PROPERTIES
                INTERFACE_LINK_OPTIONS "${_opts}")
        endif()
    endif()
endforeach()
