cmake_minimum_required(VERSION 3.16)

# ── Managed components (LVGL v9, esp_lvgl_port) ───────────────────────────
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/components"
)

# Pull in managed components declared in idf_component.yml
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(trapmaster_firmware)
