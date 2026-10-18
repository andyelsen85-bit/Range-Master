#include "terminal_logs.h"
#include "hourly_log_gate.h"
#include "failure_log_gate.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include "esp_attr.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static constexpr int RAM_CAP = 300;
// Six full-size diagnostics fit the existing 24-KiB shared NVS partition.
// The requested persistence limit is at most 20, not a second unbounded outbox.
static constexpr int CRITICAL_CAP = 6;
static EXT_RAM_BSS_ATTR TerminalLogEntry s_ring[RAM_CAP];
static TerminalLogEntry s_critical[CRITICAL_CAP];
static int s_count, s_critical_count;
static unsigned s_boot_hi, s_boot_lo, s_sequence;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_io_lock;
static bool s_dirty;
static nvs_handle_t s_nvs;
static bool s_nvs_ready;
static void copy(char *out, size_t size, const char *input) {
    snprintf(out, size, "%s", input ? input : "");
}
void terminal_logs_flush_critical() {
    if (!s_lock || !s_nvs_ready) return;
    // Diagnostics worker and upload ACKs only; never LVGL or the radio path.
    xSemaphoreTake(s_io_lock,portMAX_DELAY);
    TerminalLogEntry *snapshot=(TerminalLogEntry *)heap_caps_malloc(
        sizeof(TerminalLogEntry)*CRITICAL_CAP,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!snapshot) { xSemaphoreGive(s_io_lock); return; }
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool dirty = s_dirty;
    int count = s_critical_count;
    if (dirty) memcpy(snapshot,s_critical,count*sizeof(*snapshot));
    s_dirty = false;
    xSemaphoreGive(s_lock);
    if (dirty) {
        esp_err_t result = count ? nvs_set_blob(s_nvs,"critical",snapshot,count*sizeof(*snapshot))
                                 : nvs_erase_key(s_nvs,"critical");
        if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
        if (result == ESP_OK) result = nvs_commit(s_nvs);
        if (result != ESP_OK) {
            xSemaphoreTake(s_lock,portMAX_DELAY); s_dirty=true; xSemaphoreGive(s_lock);
        }
    }
    heap_caps_free(snapshot);
    xSemaphoreGive(s_io_lock);
}
static void diagnostics_worker(void *) {
    for (;;) { terminal_logs_flush_critical(); vTaskDelay(pdMS_TO_TICKS(250)); }
}
void terminal_logs_init() {
    s_lock=xSemaphoreCreateMutex();
    s_io_lock=xSemaphoreCreateMutex();
    s_boot_hi=esp_random(); s_boot_lo=esp_random();
    s_nvs_ready=nvs_open("diagnostics",NVS_READWRITE,&s_nvs)==ESP_OK;
    if (s_nvs_ready) {
        size_t bytes=sizeof(s_critical);
        if (nvs_get_blob(s_nvs,"critical",s_critical,&bytes)==ESP_OK &&
            bytes%sizeof(TerminalLogEntry)==0) {
            s_critical_count=(int)(bytes/sizeof(TerminalLogEntry));
            for(int i=0;i<s_critical_count;i++) {
                s_critical[i].id[63]=0; s_critical[i].ts[31]=0;
                s_critical[i].type[19]=0; s_critical[i].level[5]=0;
                s_critical[i].msg[200]=0; s_critical[i].data[512]=0;
            }
        }
    }
    xTaskCreate(diagnostics_worker,"diagnostics",4096,nullptr,3,nullptr);
}
void terminal_log_add(const char *type,const char *level,const char *message,const cJSON *data) {
    const char *local_type=type;
    type=terminal_log_wire_type(type);
    if(type&&!strcmp(type,"data_loss_risk"))level="ERROR";
    if(!s_lock) return;
    TerminalLogEntry entry{};
    time_t now=time(nullptr); struct tm utc; gmtime_r(&now,&utc);
    strftime(entry.ts,sizeof(entry.ts),"%Y-%m-%dT%H:%M:%S.000Z",&utc);
    copy(entry.type,sizeof(entry.type),type); copy(entry.level,sizeof(entry.level),level);
    copy(entry.msg,sizeof(entry.msg),message);
    char *json=data?cJSON_PrintUnformatted(data):nullptr;
    if(json && strlen(json)>500) { cJSON_free(json); return; }
    copy(entry.data,sizeof(entry.data),json?json:"{}"); cJSON_free(json);
    xSemaphoreTake(s_lock,portMAX_DELAY);
    static FailureLogGate failures;
    if(type&&!strcmp(type,"data_loss_risk")) {
        uint32_t key=2166136261u;
        for(const char *p=entry.msg;*p;p++)key=(key^(unsigned char)*p)*16777619u;
        for(const char *p=entry.data;*p;p++)key=(key^(unsigned char)*p)*16777619u;
        FailureLogCount count;
        if(!failures.admit(key,esp_timer_get_time(),&count)){xSemaphoreGive(s_lock);return;}
        char detail[360];snprintf(detail,sizeof(detail),"%.350s",entry.data);
        size_t length=strlen(entry.data);
        if(length<350&&length>=2&&detail[0]=='{'&&detail[length-1]=='}') {
            detail[length-1]=0;
            snprintf(entry.data,sizeof(entry.data),"%s%s\"count\":%u,\"firstUptimeMs\":%lld,\"lastUptimeMs\":%lld}",
                detail,length>2?",":"",(unsigned)count.count,(long long)(count.first/1000),(long long)(count.last/1000));
        }
        else
            snprintf(entry.data,sizeof(entry.data),"{\"count\":%u,\"firstUptimeMs\":%lld,\"lastUptimeMs\":%lld,\"detailHash\":%u}",
                (unsigned)count.count,(long long)(count.first/1000),(long long)(count.last/1000),(unsigned)key);
    }
    static HourlyLogGate archive_errors,identity_errors,glyph_errors;
    const bool archive_error=message && strstr(message,"Spielarchiv") &&
        strstr(message,"nicht") && (strstr(message,"verfügbar") || strstr(message,"geladen"));
    const bool identity_error=message && strstr(message,"Terminal-ID nicht verfügbar");
    const bool glyph_error=local_type && !strcmp(local_type,"unsupported_glyph");
    HourlyLogGate *gate=archive_error?&archive_errors:(identity_error?&identity_errors:(glyph_error?&glyph_errors:nullptr));
    if(gate && !gate->admit(esp_timer_get_time())) {xSemaphoreGive(s_lock);return;}
    snprintf(entry.id,sizeof(entry.id),"%08x%08x-%08x",s_boot_hi,s_boot_lo,++s_sequence);
    if(s_count==RAM_CAP) { memmove(s_ring,s_ring+1,(RAM_CAP-1)*sizeof(entry)); --s_count; }
    s_ring[s_count++]=entry;
    if(!strcmp(level,"ERROR") || (!strcmp(type,"sync") && !strcmp(level,"WARN"))) {
        if(s_critical_count==CRITICAL_CAP) {
            memmove(s_critical,s_critical+1,(CRITICAL_CAP-1)*sizeof(entry)); --s_critical_count;
        }
        s_critical[s_critical_count++]=entry; s_dirty=true;
    }
    xSemaphoreGive(s_lock);
}
int terminal_logs_snapshot(TerminalLogEntry *out,int max) {
    if(!s_lock || !out || max<=0) return 0;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    int count=0;
    for(int i=0;i<s_critical_count && count<max;i++) out[count++]=s_critical[i];
    for(int i=0;i<s_count && count<max;i++) {
        bool duplicate=false;
        for(int j=0;j<count;j++) if(!strcmp(out[j].id,s_ring[i].id)) { duplicate=true; break; }
        if(!duplicate) out[count++]=s_ring[i];
    }
    xSemaphoreGive(s_lock);
    return count;
}
void terminal_logs_ack(const char *const *ids,int count) {
    if(!s_lock || !ids) return;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    auto compact=[&](TerminalLogEntry *entries,int &length) {
        int kept=0;
        for(int j=0;j<length;j++) {
            bool acknowledged=false;
            for(int i=0;i<count;i++) if(ids[i] && !strcmp(entries[j].id,ids[i])) {
                acknowledged=true;break;
            }
            if(!acknowledged) {
                if(kept!=j) entries[kept]=entries[j];
                ++kept;
            }
        }
        bool changed=kept!=length;length=kept;return changed;
    };
    // One compaction, not one large RAM memmove for every ACK in the batch.
    compact(s_ring,s_count);
    if(compact(s_critical,s_critical_count)) s_dirty=true;
    xSemaphoreGive(s_lock);
    // Upload runs on the sync worker. Persist the exact removal before it
    // returns, serialized against any earlier background critical snapshot.
    terminal_logs_flush_critical();
}
