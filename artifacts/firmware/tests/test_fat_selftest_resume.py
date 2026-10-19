#!/usr/bin/env python3
"""Exercise the production background worker with deterministic idle/NVS faults.

These are logic regressions, not real-terminal timing or flash measurements.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/store/fat_selftest.cpp").read_text()
MOCKS = {
    "runtime_safety.h": """#pragma once
enum class RuntimeTask {FatSelftest};
inline void runtime_task_watch(RuntimeTask){}
inline void runtime_task_finished(RuntimeTask){}
inline bool runtime_optional_tasks_allowed(){return true;}
""",
    "active_game.h": """#pragma once
bool active_game_checkpoint_pending();
bool active_game_writer_idle_for(unsigned);
""",
    "game_store.h": """#pragma once
constexpr int SCREEN_SPIEL=10;
struct Store {bool activeGameOpen=false,harakiriOrdersReady=false;int screen=0;};
extern Store g_store;
bool game_store_finish_pending();
""",
    "game_journal.h": "#pragma once\nbool game_journal_background_self_test();\n",
    "offline_cache.h": """#pragma once
bool offline_cache_cleanup_busy();
bool offline_cache_recheck(bool=true);
""",
    "ui_manager.h": """#pragma once
inline bool ui_manager_snapshot_lock(bool *acquired){*acquired=false;return true;}
inline void ui_manager_unlock(){}
""",
    "terminal_logs.h": """#pragma once
void terminal_log_add(const char*,const char*,const char*);
""",
    "esp_app_desc.h": """#pragma once
struct App {unsigned char app_elf_sha256[32]={};};
const App *esp_app_get_description();
""",
    "esp_timer.h": "#pragma once\nint64_t esp_timer_get_time();\n",
    "esp_heap_caps.h": """#pragma once
constexpr int MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
""",
    "esp_log.h": """#pragma once
void capture(const char*,...);
#define ESP_LOGI(tag,fmt,...) capture(fmt,##__VA_ARGS__)
#define ESP_LOGW(tag,fmt,...) capture(fmt,##__VA_ARGS__)
""",
    "nvs.h": """#pragma once
using nvs_handle_t=unsigned;
constexpr int NVS_READWRITE=1,ESP_OK=0;
int nvs_open(const char*,int,nvs_handle_t*);
int nvs_get_u32(nvs_handle_t,const char*,uint32_t*);
int nvs_set_u32(nvs_handle_t,const char*,uint32_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
""",
    "freertos/FreeRTOS.h": """#pragma once
#define pdMS_TO_TICKS(ms) (ms)
constexpr int pdPASS=1;
""",
    "freertos/task.h": """#pragma once
void vTaskDelay(unsigned);
void vTaskDeleteWithCaps(void*);
int xTaskCreateWithCaps(void(*)(void*),const char*,unsigned,void*,int,void*,int);
""",
}
HARNESS = r'''
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "fat_selftest.cpp"

Store g_store;
App app;
int64_t now=0,writer_at=0;
bool dirty=false,interrupt=true;
unsigned pauses=0,deferred=0;
std::map<std::string,uint32_t> committed,staged;
std::vector<unsigned> patterns;
std::vector<std::string> logs;
void (*task)(void*);
struct PowerLoss {};
void capture(const char *fmt,...) {
    char text[512];va_list args;va_start(args,fmt);
    vsnprintf(text,sizeof(text),fmt,args);va_end(args);logs.emplace_back(text);
}
void terminal_log_add(const char*,const char*,const char *text){logs.emplace_back(text);}
const App *esp_app_get_description(){return &app;}
int64_t esp_timer_get_time(){return now;}
bool active_game_checkpoint_pending(){return dirty;}
bool active_game_writer_idle_for(unsigned ms){return !dirty&&now-writer_at>=ms*1000;}
bool game_store_finish_pending(){return false;}
bool offline_cache_cleanup_busy(){return false;}
bool game_journal_background_self_test(){++deferred;return true;}
bool offline_cache_recheck(bool){return true;}
void vTaskDelay(unsigned ms) {
    now+=ms*1000;
    if(interrupt&&patterns.size()==1) {
        ++pauses;
        if(pauses>=3)g_store.activeGameOpen=false;
        if(pauses==5){dirty=false;writer_at=now-1000000;}
    }
}
void vTaskDeleteWithCaps(void*){}
int xTaskCreateWithCaps(void(*fn)(void*),const char*,unsigned stack,void*,int priority,void*,int caps) {
    assert(stack>=8192&&priority==1&&(caps&MALLOC_CAP_INTERNAL));task=fn;return pdPASS;
}
int nvs_open(const char *name,int,nvs_handle_t *n){assert(!strcmp(name,"fat_probe"));*n=1;return ESP_OK;}
int nvs_get_u32(nvs_handle_t,const char *key,uint32_t *value) {
    auto found=committed.find(key);
    if(found==committed.end())return -1;
    *value=found->second;return ESP_OK;
}
int nvs_set_u32(nvs_handle_t,const char *key,uint32_t value){staged[key]=value;return ESP_OK;}
int nvs_commit(nvs_handle_t){for(auto &p:staged)committed[p.first]=p.second;staged.clear();return ESP_OK;}
void nvs_close(nvs_handle_t){}
bool offline_cache_pattern_step(int index,bool durable,char*,unsigned) {
    assert(durable);
    if(interrupt&&index==2)throw PowerLoss{};
    patterns.push_back(index);
    if(interrupt&&index==0){g_store.activeGameOpen=true;dirty=true;writer_at=now;}
    return true;
}
bool contains(const char *part) {
    for(auto &line:logs)if(line.find(part)!=std::string::npos)return true;
    return false;
}
void boot() {started=false;fat_selftest_after_ui();assert(task);task(nullptr);}
int main() {
    try {boot();assert(false);}catch(const PowerLoss&){}
    assert(deferred==1&&patterns.size()==2);
    assert(committed.at("next")==2&&!committed.count("version"));
    assert(contains("reason=active_game")&&contains("reason=checkpoint_dirty")&&
           contains("reason=writer_idle_under_2s"));
    assert(!contains("complete=1"));
    interrupt=false;patterns.clear();logs.clear();boot();
    constexpr unsigned count=sizeof(FAT_PATTERNS)/sizeof(FAT_PATTERNS[0]);
    assert(patterns.size()==count-2&&patterns.front()==2&&patterns.back()==count-1);
    assert(committed.at("next")==count&&committed.at("version")==committed.at("progress_ver"));
    assert(contains("resume mode=full next=2/")&&contains("complete=1"));
    patterns.clear();logs.clear();boot();
    assert(patterns.empty()&&contains("mode=current"));
    app.app_elf_sha256[0]=1;patterns.clear();logs.clear();boot();
    assert(patterns.size()==count&&patterns.front()==0&&contains("resume mode=full next=0/"));
    puts("PASS FAT worker: named pause reasons, 2s idle, persisted resume, unchanged skip, changed-build reset");
}
'''

with tempfile.TemporaryDirectory(prefix="tm-fat-resume-") as folder:
    path = Path(folder)
    for name, content in MOCKS.items():
        file = path / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(content)
    for name in ("fat_selftest.h", "fat_patterns.h", "fat_name.h"):
        (path / name).write_text((ROOT / "main/store" / name).read_text())
    (path / "fat_selftest.cpp").write_text(SOURCE)
    (path / "test.cpp").write_text(HARNESS)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(path), str(path / "test.cpp"), "-o", str(path / "test")],
                   check=True)
    subprocess.run([str(path / "test")], check=True, timeout=10)

# Structural checks complement the exercised worker and restart regressions.
fire = (ROOT / "main/lora_stub/lora_stub.cpp").read_text()
assert "active_game_flush()" not in fire
assert fire.index("active_game_request_flush()") > fire.index('ESP_LOGI(TAG,"fire_ms=')
assert "start_to_fire_ms=" in fire
main = (ROOT / "main/main.cpp").read_text()
assert "offline_cache_recheck(false)" in main
assert main.index("fat_selftest_after_ui()") > main.index('"UI ready')
cache = (ROOT / "main/store/offline_cache.cpp").read_text()
quick = cache[cache.index("static bool check_path_patterns()"):cache.index("static wl_handle_t")]
assert "fat_patterns_self_test(" not in quick
buttons = (ROOT / "main/lora_stub/button_events.cpp").read_text()
worker = buttons[buttons.index("void worker(void *)"):]
assert worker.index("if(!cop_wifi_is_connected())") < worker.index("esp_websocket_client_start")
assert "1000,2000,5000,10000" in worker
print("PASS boot/FIRE/WiFi ordering guards")
