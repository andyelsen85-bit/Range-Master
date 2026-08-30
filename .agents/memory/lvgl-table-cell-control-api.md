---
name: LVGL table crop API
description: Resolved LVGL table-cell control naming for this firmware dependency
---

Use `lv_table_set_cell_ctrl` with `LV_TABLE_CELL_CTRL_TEXT_CROP` for player-name table cells in this project. Do not substitute the similarly named `lv_table_add_cell_ctrl` without compiling against the actual managed LVGL headers.

**Why:** The Windows ESP-IDF 5.3.5 build resolved LVGL 9.5.0 and rejected `lv_table_add_cell_ctrl`, explicitly suggesting `lv_table_set_cell_ctrl`.

**How to apply:** When adding bounded text to LVGL tables, verify the symbol in the resolved managed component or run the Windows firmware build before pushing.