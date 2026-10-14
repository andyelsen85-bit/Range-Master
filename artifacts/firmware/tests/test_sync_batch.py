#!/usr/bin/env python3
"""Real staging/publisher functions: atomic RAM boundary, current outbox replay."""
from pathlib import Path
import subprocess
import tempfile
from test_offline_cache import function

ROOT=Path(__file__).resolve().parents[1]
CPP=r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
using esp_err_t=int;using TickType_t=unsigned;
constexpr int ESP_OK=0,ESP_ERR_NO_MEM=1,ESP_ERR_TIMEOUT=2,ESP_ERR_INVALID_STATE=3;
enum OfflineCacheSection {ROSTER,CREDITS,OFFLINE_CACHE_SECTION_COUNT};
struct SyncBatch {
 std::function<void()> apply[8];unsigned count=0;bool active=true;
 bool cache[2]{},tokenSet[2]{};char tokens[2][96]{},day[11]="2026-10-15";
 uint64_t commitUs=0;
};
static SyncBatch *s_batch;
static bool locked=false,allowed=true;
static int windows=0,nvs=0,fat=0,liveRoster=1,liveCredit=2,pending=0;
static int64_t clockUs=1000;
bool batching(){return s_batch&&s_batch->active;}
int64_t esp_timer_get_time(){return clockUs;}
bool day_inventory_today(char *out,size_t n){snprintf(out,n,"2026-10-15");return true;}
bool sync_commit_begin(const char*,TickType_t *t){if(!allowed)return false;locked=true;++windows;*t=0;return true;}
void sync_commit_end(const char*,TickType_t){assert(locked);locked=false;s_batch->commitUs+=10;}
void game_store_save(){assert(!locked);++nvs;}
bool day_inventory_persist(){assert(!locked);return true;}
bool offline_cache_save(OfflineCacheSection){assert(!locked);++fat;return true;}
void cache_pulled_section(OfflineCacheSection s){(void)offline_cache_save(s);}
bool offline_cache_set_manifest_token(OfflineCacheSection,const char*){assert(!locked);return true;}
void store_rebuild_bill_projection(){assert(locked);clockUs+=10;}
struct cJSON {};
cJSON *cJSON_CreateObject(){return new cJSON;}
void cJSON_AddNumberToObject(cJSON*,const char*,double){}
void cJSON_Delete(cJSON *p){delete p;}
void terminal_log_add(const char*,const char*,const char*,cJSON*){}
#define ESP_LOGW(...) do {} while(0)
"""
MAIN=r"""
int main(){
 SyncBatch b;s_batch=&b;
 assert(stage_dataset("roster",ROSTER,[]{assert(locked);liveRoster=10;})==ESP_OK);
 assert(stage_dataset("credits",CREDITS,[]{assert(locked);liveCredit=20+pending;})==ESP_OK);
 assert(liveRoster==1&&liveCredit==2&&!windows&&!nvs&&!fat);
 // Operator earns/grants credit while the private server data is being parsed.
 pending=7;allowed=false;
 assert(publish_sync_batch()==ESP_ERR_TIMEOUT);
 assert(liveRoster==1&&liveCredit==2&&!windows&&!nvs&&!fat);
 allowed=true;assert(publish_sync_batch()==ESP_OK);
 assert(liveRoster==10&&liveCredit==27&&windows==1&&nvs==1&&fat==2&&!locked);
 assert(publish_sync_batch()==ESP_OK&&windows==1); // never double apply
 SyncBatch old;s_batch=&old;strcpy(old.day,"2026-10-14");
 assert(stage_dataset("roster",ROSTER,[]{liveRoster=999;})==ESP_OK);
 assert(publish_sync_batch()==ESP_ERR_INVALID_STATE&&liveRoster==10&&windows==1);
 SyncBatch full;s_batch=&full;
 for(int i=0;i<8;++i)assert(stage_dataset("roster",ROSTER,[]{})==ESP_OK);
 assert(stage_dataset("overflow",ROSTER,[]{liveRoster=999;})==ESP_ERR_NO_MEM);
 assert(liveRoster==10&&!locked);
 puts("PASS actual sync: private staging, one atomic boundary, live outbox replay, deferred commit, durable work outside lock, day guard and capacity");
}
"""
if __name__=="__main__":
    source=(ROOT/"main/net/http_sync.cpp").read_text()
    code=CPP+"\n"+function(source,"static esp_err_t stage_dataset")+"\n"+function(source,"static esp_err_t publish_sync_batch")+"\n"+MAIN
    with tempfile.TemporaryDirectory(prefix="tm-sync-batch-") as temp:
        p=Path(temp);(p/"test.cpp").write_text("#include <cstdio>\n"+code)
        subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",str(p/"test.cpp"),"-o",str(p/"test")],check=True)
        subprocess.run([str(p/"test")],check=True)
