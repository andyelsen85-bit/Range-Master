#include "runtime_safety.h"
#include "terminal_logs.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cstdint>

namespace {
constexpr uint32_t MAGIC=0x544D504Eu;
constexpr int64_t MINUTE_US=60000000;
struct PanicState { uint32_t magic,streak,inverse; };
// NOINIT, not RTC_DATA: retain panic history over warm resets without NVS
// writes. Reject uninitialized/corrupt contents on a cold boot.
RTC_NOINIT_ATTR PanicState panic_state;
bool recovery=false,stable=false;
std::atomic<bool> ui_ready{false};
int64_t ui_ready_at=0;
StaticSemaphore_t registry_storage;
SemaphoreHandle_t registry_mutex=nullptr;
struct Watch {
    TaskHandle_t handle=nullptr;
    int64_t started=0;
    uint32_t watermark=0;
    bool registered=false,finished=false,reported=false;
};
constexpr unsigned COUNT=static_cast<unsigned>(RuntimeTask::Count);
Watch watches[COUNT];
const char *const names[COUNT]={"socket_census","game_writer","fat_selftest","network_test","gateway_http"};
struct Report { const char *name; uint32_t bytes; bool finished; };

uint32_t watermark_bytes(TaskHandle_t handle) {
    // ESP-IDF's RISC-V StackType_t is byte-sized. Keep the conversion explicit
    // rather than assuming vanilla FreeRTOS's 32-bit stack words.
    return (uint32_t)uxTaskGetStackHighWaterMark(handle)*sizeof(StackType_t);
}
void clear_streak() {
    panic_state={MAGIC,0,~uint32_t(0)};
}
bool capture(RuntimeTask task,bool first,Report *out) {
    unsigned index=static_cast<unsigned>(task);
    if(!registry_mutex||index>=COUNT)return false;
    xSemaphoreTake(registry_mutex,portMAX_DELAY);
    auto &watch=watches[index];
    bool due=watch.registered&&!watch.reported&&
        (first||esp_timer_get_time()-watch.started>=MINUTE_US);
    if(due) {
        if(watch.handle)watch.watermark=watermark_bytes(watch.handle);
        *out={names[index],watch.watermark,watch.finished};
        watch.reported=true;
    }
    xSemaphoreGive(registry_mutex);
    return due;
}
void emit(const Report &report) {
    if(report.bytes<1024)
        ESP_LOGW("task_stack","task=%s stack_hwm=%u bytes (<1024) state=%s",
            report.name,(unsigned)report.bytes,report.finished?"finished":"running");
    else
        ESP_LOGI("task_stack","task=%s stack_hwm=%u bytes state=%s",
            report.name,(unsigned)report.bytes,report.finished?"finished":"running");
}
}

void runtime_boot_begin() {
    registry_mutex=xSemaphoreCreateMutexStatic(&registry_storage);
    esp_reset_reason_t reason=esp_reset_reason();
    bool valid=panic_state.magic==MAGIC&&panic_state.streak<=3&&
        panic_state.inverse==~panic_state.streak;
    uint32_t streak=valid?panic_state.streak:0;
    if(reason==ESP_RST_PANIC)streak=streak<3?streak+1:3;
    else streak=0;
    panic_state={MAGIC,streak,~streak};
    recovery=streak>=3;
    ESP_LOGI("boot","reset_reason=%d panic_streak=%u optional_background=%s",
        (int)reason,(unsigned)streak,recovery?"disabled":"enabled");
}
void runtime_ui_ready() {
    ui_ready_at=esp_timer_get_time();
    ui_ready=true;
    if(recovery) {
        ESP_LOGW("boot","panic_streak=3: optional diagnostics disabled; essential game workers retained");
        terminal_log_add("boot","WARN","Schutzstart: Hintergrunddiagnose nach drei Abstürzen deaktiviert; Spielspeicherung bleibt aktiv");
    }
}
bool runtime_optional_tasks_allowed() {return ui_ready.load()&&!recovery;}

void runtime_task_watch(RuntimeTask task) {
    unsigned index=static_cast<unsigned>(task);
    if(!registry_mutex||index>=COUNT)return;
    xSemaphoreTake(registry_mutex,portMAX_DELAY);
    watches[index]={xTaskGetCurrentTaskHandle(),esp_timer_get_time(),0,true,false,false};
    xSemaphoreGive(registry_mutex);
}
void runtime_task_finished(RuntimeTask task) {
    unsigned index=static_cast<unsigned>(task);
    if(!registry_mutex||index>=COUNT)return;
    xSemaphoreTake(registry_mutex,portMAX_DELAY);
    auto &watch=watches[index];
    watch.watermark=watermark_bytes(nullptr);
    watch.finished=true;
    watch.handle=nullptr; // invalidate while locked, BEFORE vTaskDelete()
    xSemaphoreGive(registry_mutex);
}
void runtime_task_first_report(RuntimeTask task) {
    Report report{};
    if(capture(task,true,&report))emit(report);
}
void runtime_safety_tick() {
    if(!ui_ready.load())return;
    static int64_t next_poll=0;
    int64_t now=esp_timer_get_time();
    if(now<next_poll)return;
    next_poll=now+1000000; // no per-frame mutex/stack scans in the UI loop
    if(!stable&&now-ui_ready_at>=MINUTE_US) {
        clear_streak();stable=true;
        ESP_LOGI("boot","UI stable for 60 s; panic streak cleared (no NVS write)");
    }
    for(unsigned index=0;index<COUNT;index++) {
        Report report{};
        if(capture(static_cast<RuntimeTask>(index),false,&report))emit(report);
    }
}
