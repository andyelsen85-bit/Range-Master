cmake_minimum_required(VERSION 3.16)

# ── Managed components (LVGL v9, esp_lvgl_port) ───────────────────────────
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/components"
)

# Pull in managed components declared in idf_component.yml
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(trapmaster_firmware)

# ── IDF 5.3.x / ESP32-P4 + PSRAM linker flag fix ─────────────────────────
#
# IDF 5.3.x unconditionally passes -Wl,--enable-non-contiguous-regions when
# SPIRAM is enabled on ESP32-P4.  Under this flag the linker silently discards
# any .sbss.* section it cannot fit; IDF 5.3.x sections.ld.in for ESP32-P4 has
# a matching bug where those placements are wrong, causing ~70 KB of core BSS
# (libc, libstdc++, newlib, pthread, esp_timer) to be discarded.  The bug is
# fixed in IDF 5.4.x, but 5.4.x breaks DSI bus init on ECO2 hardware.
#
# Fix: strip the flag from the link command after IDF has injected it.
# The flag is set as INTERFACE on the idf::esp_mm component target; we also
# check the ELF target's own LINK_OPTIONS as a fallback.
#
# cmake_language(DEFER DIRECTORY ...) runs at the very end of this directory
# scope — after project() and all IDF component processing has completed.

function(_strip_non_contiguous_regions)
    set(_flag "-Wl,--enable-non-contiguous-regions")
    set(_found FALSE)

    # Check the esp_mm component under all names IDF might use
    foreach(_t idf::esp_mm __idf_component_esp_mm esp_mm idf_component_esp_mm)
        if(TARGET ${_t})
            foreach(_prop INTERFACE_LINK_OPTIONS LINK_OPTIONS)
                get_target_property(_opts ${_t} ${_prop})
                if(_opts)
                    list(FIND _opts "${_flag}" _idx)
                    if(NOT _idx EQUAL -1)
                        list(REMOVE_ITEM _opts "${_flag}")
                        set_target_properties(${_t} PROPERTIES ${_prop} "${_opts}")
                        message(STATUS
                            "[trapmaster] Removed ${_flag} from ${_t}.${_prop}")
                        set(_found TRUE)
                    endif()
                endif()
            endforeach()
        endif()
    endforeach()

    # Also strip from the ELF target itself (covers the case where IDF sets it
    # directly via target_link_options on the main target)
    set(_elf "${CMAKE_PROJECT_NAME}.elf")
    foreach(_prop LINK_OPTIONS INTERFACE_LINK_OPTIONS)
        get_target_property(_opts ${_elf} ${_prop})
        if(_opts)
            list(FIND _opts "${_flag}" _idx)
            if(NOT _idx EQUAL -1)
                list(REMOVE_ITEM _opts "${_flag}")
                set_target_properties(${_elf} PROPERTIES ${_prop} "${_opts}")
                message(STATUS
                    "[trapmaster] Removed ${_flag} from ${_elf}.${_prop}")
                set(_found TRUE)
            endif()
        endif()
    endforeach()

    if(NOT _found)
        message(STATUS
            "[trapmaster] ${_flag} not found in any target — already absent or"
            " injected via a different mechanism (check the link command)")
    endif()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    CALL _strip_non_contiguous_regions)
