#include "fat_selftest.h"
#include "runtime_safety.h"
#include "fat_patterns.h"
#include "active_game.h"
#include "game_store.h"
#include "game_journal.h"
#include "offline_cache.h"
#include "ui_manager.h"
#include "terminal_logs.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

static std::atomic<bool> started{false};
static const char *busy_reason() {
    bool acquired=false;
    if(!ui_manager_snapshot_lock(&acquired))return "ui_lock";
    const char *reason=g_store.activeGameOpen?"active_game":
        g_store.harakiriOrdersReady?"harakiri_orders":
        g_store.screen==SCREEN_SPIEL?"game_screen":nullptr;
    if(acquired)ui_manager_unlock();
    if(reason)return reason;
    if(offline_cache_cleanup_busy())return "storage_cleanup";
    if(game_store_finish_pending())return "game_finish";
    if(active_game_checkpoint_pending())return "checkpoint_dirty";
    if(!active_game_writer_idle_for(2000))return "writer_idle_under_2s";
    return nullptr;
}
static void run(void*) {
    runtime_task_watch(RuntimeTask::FatSelftest);
    vTaskDelay(pdMS_TO_TICKS(1000));
    const int64_t began=esp_timer_get_time();
    uint32_t hash=2166136261u;
    auto add=[&](const void *data,size_t n) {
        auto *bytes=(const uint8_t*)data;
        for(size_t i=0;i<n;i++)hash=(hash^bytes[i])*16777619u;
    };
    for(const auto &p:FAT_PATTERNS) {
        add(p.base,strlen(p.base)+1);add(p.extension,strlen(p.extension)+1);add(&p.cache,sizeof(p.cache));
    }
    add(&FAT_NAME_VERSION,sizeof(FAT_NAME_VERSION));
    add(esp_app_get_description()->app_elf_sha256,32);
    nvs_handle_t n=0;
    uint32_t previous=0,progress_hash=0,next=0;
    const bool opened=nvs_open("fat_probe",NVS_READWRITE,&n)==ESP_OK;
    if(opened) {
        (void)nvs_get_u32(n,"version",&previous);
        (void)nvs_get_u32(n,"progress_ver",&progress_hash);
        (void)nvs_get_u32(n,"next",&next);
    }
    constexpr unsigned count=sizeof(FAT_PATTERNS)/sizeof(FAT_PATTERNS[0]);
    if(progress_hash!=hash||next>count)next=0;
    bool full=previous!=hash,maintenance=false,complete=!full;
    char error[256]={};
    const char *last_reason=nullptr;
    int64_t last_report=0;
    ESP_LOGI("fat_selftest","resume mode=%s next=%lu/%u",
        full?"full":"current",(unsigned long)next,count);
    for(;;) {
        const char *reason=busy_reason();
        if(reason) {
            if(!last_reason||strcmp(last_reason,reason)||esp_timer_get_time()-last_report>=60000000) {
                ESP_LOGI("fat_selftest","paused reason=%s next=%lu/%u",reason,(unsigned long)next,count);
                last_reason=reason;last_report=esp_timer_get_time();
            }
            vTaskDelay(pdMS_TO_TICKS(500));continue;
        }
        last_reason=nullptr;
        if(!maintenance) {
            // These used to block UI construction. Never run them under LVGL.
            int64_t step=esp_timer_get_time();
            bool journal=game_journal_background_self_test();
            bool volume=offline_cache_recheck();
            ESP_LOGI("fat_selftest","deferred_probe_ms=%lld journal=%d volume=%d",
                (long long)((esp_timer_get_time()-step)/1000),journal,volume);
            if(!journal||!volume) {
                terminal_log_add("storage","WARN","FAT-Selbsttest zurückgestellt: Journal-/Volumenprüfung fehlgeschlagen");
                vTaskDelay(pdMS_TO_TICKS(30000));continue;
            }
            maintenance=true;
            continue; // recheck idle/game flags after the slow probes
        }
        if(!full)break;
        if(next<count) {
            if(!offline_cache_pattern_step(next,true,error,sizeof(error))) {
                ESP_LOGW("fat_selftest","paused reason=pattern_io next=%lu detail=%.180s",
                    (unsigned long)next,error);
                terminal_log_add("storage","WARN",error[0]?error:"FAT-Selbsttest: Speicher gesperrt");
                vTaskDelay(pdMS_TO_TICKS(30000));continue;
            }
            // Advance only after the entire pattern passed. A power loss before
            // commit merely repeats this harmless scratch pattern next boot.
            ++next;
            if(opened&&(nvs_set_u32(n,"progress_ver",hash)!=ESP_OK||
                        nvs_set_u32(n,"next",next)!=ESP_OK||nvs_commit(n)!=ESP_OK)) {
                ESP_LOGW("fat_selftest","progress_save_failed next=%lu",(unsigned long)next);
            }
            ESP_LOGI("fat_selftest","progress next=%lu/%u",(unsigned long)next,count);
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }
        complete=opened&&nvs_set_u32(n,"version",hash)==ESP_OK&&nvs_commit(n)==ESP_OK;
        if(!complete)terminal_log_add("storage","WARN","FAT-Selbsttestversion konnte nicht gespeichert werden");
        break;
    }
    if(opened)nvs_close(n);
    ESP_LOGI("fat_selftest","fat selftest done in %lld ms mode=%s complete=%d next=%lu/%u",
        (long long)((esp_timer_get_time()-began)/1000),full?"full":"current",complete,
        (unsigned long)next,count);
    runtime_task_finished(RuntimeTask::FatSelftest);
    vTaskDeleteWithCaps(nullptr);
}
void fat_selftest_after_ui() {
    if(!runtime_optional_tasks_allowed())return;
    if(started.exchange(true))return;
    // Flash/NVS paths require the enlarged internal stack even with external
    // stack support enabled.
    if(xTaskCreateWithCaps(run,"fat_selftest",8192,nullptr,1,nullptr,
                          MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)!=pdPASS) {
        started=false;
        terminal_log_add("storage","WARN","FAT-Selbsttest: Hintergrundtask nicht verfügbar");
    }
}
