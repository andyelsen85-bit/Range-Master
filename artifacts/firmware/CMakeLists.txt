cmake_minimum_required(VERSION 3.16)

# ── Managed components (LVGL v9, esp_lvgl_port) ───────────────────────────
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/components"
)

# Pull in managed components declared in idf_component.yml
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Recent Hosted/Remote Kconfig files require major.minor (not the patch
# version). IDF 5.3 does not publish this automatically. Derive it from the
# actual SDK rather than silently skipping WiFi Remote's optional Kconfig.
include("$ENV{IDF_PATH}/tools/cmake/version.cmake")
set(ENV{ESP_IDF_VERSION} "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}")

project(trapmaster_firmware)

# Ninja's Kconfig regeneration runs after this CMake process has exited.
# Persist the same SDK series in its generated environment, too.
idf_build_get_property(TRAPMASTER_CONFIG_ENV CONFIG_ENV_PATH)
file(READ "${TRAPMASTER_CONFIG_ENV}" TRAPMASTER_CONFIG_JSON)
string(JSON TRAPMASTER_CONFIG_JSON SET "${TRAPMASTER_CONFIG_JSON}"
       ESP_IDF_VERSION "\"${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}\"")
file(WRITE "${TRAPMASTER_CONFIG_ENV}" "${TRAPMASTER_CONFIG_JSON}")

# Shrink widget/control and layout code, not the pixel renderers, DMA/PPA,
# display drivers, LVGL draw pipelines or FreeRTOS scheduling.
if(TARGET __idf_lvgl__lvgl)
    get_target_property(TRAPMASTER_LVGL_SOURCES __idf_lvgl__lvgl SOURCES)
    foreach(TRAPMASTER_LVGL_SOURCE IN LISTS TRAPMASTER_LVGL_SOURCES)
        if(TRAPMASTER_LVGL_SOURCE MATCHES "/(widgets|layouts|core|misc)/")
            set_source_files_properties("${TRAPMASTER_LVGL_SOURCE}"
                TARGET_DIRECTORY __idf_lvgl__lvgl PROPERTIES COMPILE_OPTIONS "-Os")
        endif()
    endforeach()
endif()

# Configuration/sync HTTP and JSON parsing are also outside the display and
# socket FIRE hot paths. Do not alter TLS/AES/SHA or radio driver optimization.
foreach(TRAPMASTER_CONTROL_COMPONENT json esp_http_client esp_http_server esp_https_ota app_update)
    if(TARGET __idf_${TRAPMASTER_CONTROL_COMPONENT})
        target_compile_options(__idf_${TRAPMASTER_CONTROL_COMPONENT} PRIVATE -Os)
    endif()
endforeach()
