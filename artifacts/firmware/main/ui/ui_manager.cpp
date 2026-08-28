// ============================================================
// UI manager — LVGL screen router
// ============================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"

#include "ui_manager.h"
#include "game_store.h"

#include "screen_dashboard.h"
#include "screen_start.h"
#include "screen_spiel.h"
#include "screen_resultate.h"
#include "screen_geschichte.h"
#include "screen_kredite.h"
#include "screen_einstellungen.h"
#include "screen_spiller.h"
#include "screen_wifi.h"
#include "screen_bluetooth.h"

static const char *TAG = "ui_manager";

// Shared styles
lv_style_t g_style_card;
lv_style_t g_style_btn_primary;
lv_style_t g_style_btn_secondary;
lv_style_t g_style_btn_danger;
lv_style_t g_style_label_title;
lv_style_t g_style_label_mono;
lv_style_t g_style_sidebar;

static lv_obj_t *s_screens[SCREEN_COUNT];
static Screen    s_current = SCREEN_COUNT; // invalid → force first load

// ── Style init ────────────────────────────────────────────────
static void styles_init(void)
{
    // Card
    lv_style_init(&g_style_card);
    lv_style_set_bg_color(&g_style_card, lv_color_hex(CLR_CARD));
    lv_style_set_bg_opa(&g_style_card, LV_OPA_COVER);
    lv_style_set_border_color(&g_style_card, lv_color_hex(CLR_BORDER));
    lv_style_set_border_width(&g_style_card, 1);
    lv_style_set_radius(&g_style_card, 10);
    lv_style_set_pad_all(&g_style_card, 12);

    // Primary button
    lv_style_init(&g_style_btn_primary);
    lv_style_set_bg_color(&g_style_btn_primary, lv_color_hex(CLR_PRIMARY));
    lv_style_set_bg_opa(&g_style_btn_primary, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_btn_primary, lv_color_hex(CLR_TEXT));
    lv_style_set_radius(&g_style_btn_primary, 8);
    lv_style_set_border_width(&g_style_btn_primary, 0);
    lv_style_set_pad_hor(&g_style_btn_primary, 20);
    lv_style_set_pad_ver(&g_style_btn_primary, 12);

    // Secondary button
    lv_style_init(&g_style_btn_secondary);
    lv_style_set_bg_color(&g_style_btn_secondary, lv_color_hex(CLR_BORDER));
    lv_style_set_bg_opa(&g_style_btn_secondary, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_btn_secondary, lv_color_hex(CLR_TEXT));
    lv_style_set_radius(&g_style_btn_secondary, 8);
    lv_style_set_border_width(&g_style_btn_secondary, 1);
    lv_style_set_border_color(&g_style_btn_secondary, lv_color_hex(CLR_MUTED));
    lv_style_set_pad_hor(&g_style_btn_secondary, 20);
    lv_style_set_pad_ver(&g_style_btn_secondary, 12);

    // Danger button
    lv_style_init(&g_style_btn_danger);
    lv_style_set_bg_color(&g_style_btn_danger, lv_color_hex(CLR_DANGER));
    lv_style_set_bg_opa(&g_style_btn_danger, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_btn_danger, lv_color_hex(CLR_TEXT));
    lv_style_set_radius(&g_style_btn_danger, 8);
    lv_style_set_border_width(&g_style_btn_danger, 0);
    lv_style_set_pad_hor(&g_style_btn_danger, 20);
    lv_style_set_pad_ver(&g_style_btn_danger, 12);

    // Title label
    lv_style_init(&g_style_label_title);
    lv_style_set_text_color(&g_style_label_title, lv_color_hex(CLR_TEXT));
    lv_style_set_text_font(&g_style_label_title, &lv_font_montserrat_24);

    // Mono label
    lv_style_init(&g_style_label_mono);
    lv_style_set_text_color(&g_style_label_mono, lv_color_hex(CLR_MUTED));
    lv_style_set_text_font(&g_style_label_mono, &lv_font_montserrat_14);

    // Sidebar
    lv_style_init(&g_style_sidebar);
    lv_style_set_bg_color(&g_style_sidebar, lv_color_hex(CLR_SIDEBAR));
    lv_style_set_bg_opa(&g_style_sidebar, LV_OPA_COVER);
    lv_style_set_border_color(&g_style_sidebar, lv_color_hex(CLR_BORDER));
    lv_style_set_border_width(&g_style_sidebar, 0);
    lv_style_set_pad_all(&g_style_sidebar, 12);
    lv_style_set_radius(&g_style_sidebar, 0);
}

// ── Helper: set display bg to dark ────────────────────────────
// Called by each screen's _create() right after lv_obj_create(NULL),
// so screens come up fully styled in a single build pass.
void screen_base_init(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

// ── ui_manager_init ───────────────────────────────────────────
void ui_manager_init(void)
{
    ESP_LOGI(TAG, "Building LVGL screens...");
    styles_init();

    // Create persistent WiFi worker tasks NOW, before any screen_*_create()
    // call consumes internal RAM.  After all 10 screens are built, internal
    // RAM drops to ~52 bytes — far too little for a FreeRTOS TCB (~200 B).
    // Workers created here (while ~93 KB is still free) block on queues and
    // cost no allocations when scan_cb / connect_cb fire at tap time.
    screen_wifi_create_workers();
    screen_bluetooth_create_workers();
    store_create_workers();

    // Yield 5 ms between each screen build so FreeRTOS IDLE1 can run and
    // reset the task watchdog.  Without this, 10 consecutive heavy screen
    // constructions (widget creation events + flush_cb rotation loops) can
    // hold CPU1 for > 5 s — the default WDT timeout — tripping a false alarm.
    s_screens[SCREEN_DASHBOARD]    = screen_dashboard_create();    vTaskDelay(pdMS_TO_TICKS(5));
    s_screens[SCREEN_START]        = screen_start_create();        vTaskDelay(pdMS_TO_TICKS(5));
    s_screens[SCREEN_SPIEL]        = screen_spiel_create();        vTaskDelay(pdMS_TO_TICKS(5));
    s_screens[SCREEN_RESULTATE]    = screen_resultate_create();    vTaskDelay(pdMS_TO_TICKS(5));
    s_screens[SCREEN_GESCHICHTE]   = screen_geschichte_create();   vTaskDelay(pdMS_TO_TICKS(5));
    s_screens[SCREEN_KREDITE]      = screen_kredite_create();      vTaskDelay(pdMS_TO_TICKS(5));
    s_screens[SCREEN_SPILLER]      = screen_spiller_create();      vTaskDelay(pdMS_TO_TICKS(5));
    s_screens[SCREEN_EINSTELLUNGEN]= screen_einstellungen_create();vTaskDelay(pdMS_TO_TICKS(5));
    s_screens[SCREEN_WIFI]         = screen_wifi_create();         vTaskDelay(pdMS_TO_TICKS(5));
    s_screens[SCREEN_BLUETOOTH]    = screen_bluetooth_create();

    // Register a timer to poll store.screen changes
    lv_timer_create([](lv_timer_t *t) {
        ui_manager_tick();
    }, 50, NULL);

    ui_manager_show(SCREEN_DASHBOARD);
    ESP_LOGI(TAG, "UI ready");
}

void ui_manager_show(Screen s)
{
    if (s >= SCREEN_COUNT || !s_screens[s]) return;
    if (s == s_current) return;

    // Refresh dynamic data before showing
    switch (s) {
        case SCREEN_DASHBOARD:    screen_dashboard_refresh();    break;
        case SCREEN_START:        screen_start_refresh();        break;
        case SCREEN_SPIEL:        screen_spiel_refresh();        break;
        case SCREEN_RESULTATE:    screen_resultate_refresh();    break;
        case SCREEN_GESCHICHTE:   screen_geschichte_refresh();   break;
        case SCREEN_KREDITE:      screen_kredite_refresh();      break;
        case SCREEN_SPILLER:      screen_spiller_refresh();      break;
        case SCREEN_EINSTELLUNGEN:screen_einstellungen_refresh();break;
        case SCREEN_WIFI:         screen_wifi_refresh();         break;
        case SCREEN_BLUETOOTH:    screen_bluetooth_refresh();    break;
        default: break;
    }

    lv_scr_load_anim(s_screens[s], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    s_current = s;
    g_store.screen = s;
    ESP_LOGI(TAG, "Showing screen %d", (int)s);
}

void ui_manager_tick(void)
{
    // React to store.screen changes triggered by C logic
    if (g_store.screen != s_current) {
        ui_manager_show(g_store.screen);
    }
    // Let the active screen update live data
    switch (s_current) {
        case SCREEN_SPIEL:      screen_spiel_tick();      break;
        case SCREEN_EINSTELLUNGEN: screen_einstellungen_tick(); break;
        case SCREEN_DASHBOARD:  screen_dashboard_tick();  break;
        case SCREEN_SPILLER:    screen_spiller_tick();    break;
        case SCREEN_WIFI:       screen_wifi_tick();       break;
        default: break;
    }
}
