#include "fat_selftest.h"
#include "runtime_safety.h"
#include "fat_patterns.h"
#include "active_game.h"
#include "game_store.h"
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
static bool busy() {
    bool acquired=false;if(!ui_manager_snapshot_lock(&acquired))return true;
    bool active=g_store.activeGameOpen||g_store.harakiriOrdersReady||g_store.screen==SCREEN_SPIEL;
    if(acquired)ui_manager_unlock();
    return active||active_game_checkpoint_pending();
}
static void run(void*) {
    runtime_task_watch(RuntimeTask::FatSelftest);
    vTaskDelay(pdMS_TO_TICKS(1000)); // let the first UI frame reach the panel
    int64_t began=esp_timer_get_time();
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
    nvs_handle_t n=0;uint32_t previous=0;
    bool opened=nvs_open("fat_probe",NVS_READWRITE,&n)==ESP_OK;
    if(opened)(void)nvs_get_u32(n,"version",&previous);
    bool full=previous!=hash,ok=true;char error[256]={};
    if(full)for(unsigned i=0;i<sizeof(FAT_PATTERNS)/sizeof(FAT_PATTERNS[0]);i++) {
        if(busy()){ok=false;snprintf(error,sizeof(error),"FAT-Selbsttest zurückgestellt: Spiel/Checkpoint aktiv");break;}
        if(!offline_cache_pattern_step(i,true,error,sizeof(error))){ok=false;break;}
        vTaskDelay(pdMS_TO_TICKS(25)); // yield between patterns and release FS locks
    }
    if(full&&ok&&opened) {
        if(nvs_set_u32(n,"version",hash)!=ESP_OK||nvs_commit(n)!=ESP_OK)
            terminal_log_add("storage","WARN","FAT-Selbsttestversion konnte nicht gespeichert werden");
    }
    if(opened)nvs_close(n);
    if(!ok)terminal_log_add("storage","WARN",error);
    if(full)ESP_LOGI("fat_selftest","fat selftest done in %lld ms mode=full complete=%d",
        (long long)((esp_timer_get_time()-began)/1000),ok);
    runtime_task_finished(RuntimeTask::FatSelftest);
    vTaskDeleteWithCaps(nullptr);
}
void fat_selftest_after_ui() {
    if(!runtime_optional_tasks_allowed())return;
    if(started.exchange(true))return;
    // This worker commits NVS/flash. Keep its enlarged stack internal;
    // enabling external stacks is not proof that cache-off flash paths are safe.
    if(xTaskCreateWithCaps(run,"fat_selftest",8192,nullptr,1,nullptr,
                          MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)!=pdPASS)
        terminal_log_add("storage","WARN","FAT-Selbsttest: Hintergrundtask nicht verfügbar");
}
