#pragma once
// Pure layout/text helpers shared by LVGL screens; no LVGL dependency so the
// host regression test (tests/test_ui_layout.py) can exercise them directly.
#include <stdio.h>
#include <stddef.h>

struct UiGridCell { int x, y, w, h; };

// Even grid: `count` items, `columns` per row, equal width/height/gap.
// Remaining pixels are distributed to no item, so every cell is identical.
static inline UiGridCell ui_grid_cell(int width, int columns, int item_h, int gap, int index)
{
    if (columns < 1) columns = 1;
    int w = (width - gap * (columns - 1)) / columns;
    if (w < 1) w = 1;
    UiGridCell c;
    c.x = (index % columns) * (w + gap);
    c.y = (index / columns) * (item_h + gap);
    c.w = w;
    c.h = item_h;
    return c;
}

// "Offline-Warteschlange: 0 Aktionen / 1 Aktion / n Aktionen"
static inline void ui_pending_actions_text(int count, char *dst, size_t len)
{
    if (count < 0) count = 0;
    snprintf(dst, len, "Offline-Warteschlange: %d %s", count, count == 1 ? "Aktion" : "Aktionen");
}

// Queue button label: bold uppercase, as the other buttons.
static inline void ui_queue_button_text(int total, char *dst, size_t len)
{
    if (total < 0) total = 0;
    snprintf(dst, len, "WARTESCHLANGE (%d)", total);
}

// Approximate advance width of uppercase text at a pixel font size; used to
// decide TOTAL vs. sigma before LVGL measuring is available.
static inline int ui_estimate_text_w(int chars, int font_px) { return (chars * font_px * 7 + 9) / 10; }

// Dashboard sync chip: short, fixed wording that always fits one line.
// The full error text stays in the existing sync error UI (Einstellungen).
enum UiSyncChip { UI_SYNC_IDLE, UI_SYNC_RUNNING, UI_SYNC_OK, UI_SYNC_ERROR };
static inline const char *ui_sync_chip_text(int chip, bool cache_ok)
{
    if (chip == UI_SYNC_ERROR) return "SYNC FEHLER";
    if (!cache_ok) return "CACHE FEHLT";
    if (chip == UI_SYNC_RUNNING) return "SYNC LÄUFT …";
    if (chip == UI_SYNC_OK) return "SYNCHRONISIERT";
    return "WARTET AUF SYNC";
}

// Line 3: strictly "Letzter Sync DD.MM.YYYY HH:MM" (or "noch nie").
static inline void ui_last_sync_text(const char *stamp, char *dst, size_t len)
{
    snprintf(dst, len, "Letzter Sync %s", stamp && stamp[0] ? stamp : "noch nie");
}
