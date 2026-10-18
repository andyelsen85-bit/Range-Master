"""Execute the production recovery authorization/confirmation/removal code.

Filesystem durability is tested separately by test_game_journal.py. These tests
deliberately make UI publication and deletion fail to prove safe ordering.
"""
from pathlib import Path
import importlib.util, subprocess, tempfile, re
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",Path(__file__).with_name("test_machine_batch.py"))
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
SOURCE=(ROOT/"main/net/http_sync.cpp").read_text()
STUBS=r'''
#include "game_store.h"
#include "storage_budget.h"
#include <cassert>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
using esp_err_t=int;using TickType_t=int;
constexpr int ESP_OK=0,SYNC_RUNNING_TEST=0;
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void *heap_caps_calloc(size_t n,size_t bytes,int){return calloc(n,bytes);}
void heap_caps_free(void *p){free(p);}
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
GameStore g_store{};
struct RecoveryReceipt {char id[40];uint32_t checksum;int portalState;};
RecoveryReceipt s_exports[STORAGE_JOURNAL_SLOTS]{};
std::atomic_flag s_network_cycle_busy=ATOMIC_FLAG_INIT;
char s_discard_id[40]{},s_discard_token[24]{};
uint32_t s_discard_checksum;
int64_t s_discard_until,now_us=1000000;
int64_t esp_timer_get_time(){return now_us;}
uint32_t esp_random(){static uint32_t nonce=0x12345678;return ++nonce;}
bool storage_admin_verify(const char *pin){return pin&&!strcmp(pin,"2468");}
bool offline_cache_cleanup_busy(){return false;}
FinishedGame record{};
bool on_disk=true,commit_ok=true,delete_ok=true;
int deletes,audits,commits,saves;
bool ui_manager_snapshot_lock(bool *acquired){*acquired=commit_ok;return commit_ok;}
void ui_manager_unlock(){commits++;}
bool pending_record(const char *id,FinishedGame *out){
 if((on_disk||g_store.pendingGamesCount)&&!strcmp(id,record.base.externalId)){*out=record;return true;}return false;
}
bool sync_commit_begin(const char*,TickType_t *started){*started=0;return commit_ok;}
void sync_commit_end(const char*,TickType_t){commits++;}
void game_store_save(){saves++;}
bool game_journal_discard(const char *id){
 assert(!strcmp(id,record.base.externalId));assert(g_store.pendingGamesCount==0);
 deletes++;if(!delete_ok)return false;on_disk=false;return true;
}
int game_journal_reload_pending(){if(on_disk){g_store.pendingGamesCount=1;g_store.pendingGames[0]=record.base;}return 1;}
void game_journal_maintenance(){}
bool game_journal_recheck_ready(){return !on_disk&&!g_store.pendingGamesCount;}
struct cJSON {std::map<std::string,std::string> text;};
cJSON *cJSON_CreateObject(){return new cJSON;}
void cJSON_AddStringToObject(cJSON *o,const char *key,const char *value){o->text[key]=value;}
void cJSON_AddNumberToObject(cJSON*,const char*,double){}
void cJSON_AddBoolToObject(cJSON*,const char*,bool){}
void cJSON_Delete(cJSON *o){delete o;}
void terminal_log_add(const char *type,const char *level,const char*,const cJSON *data){
 assert(!strcmp(type,"data_loss_risk")&&!strcmp(level,"ERROR"));
 assert(data->text.at("externalId")==record.base.externalId);audits++;
}
void terminal_logs_flush_critical(){}
'''
FUNCTIONS=[
 "static uint32_t record_hash(","static bool remove_ram_pending(",
 "void http_pending_game_export_delivered(","bool http_pending_game_prepare_discard(",
 "bool http_pending_game_discard(",
]
MAIN=r'''
static void setup(){
 g_store={};record={};s_network_cycle_busy.clear();memset(s_exports,0,sizeof(s_exports));
 strcpy(record.base.externalId,"11111111-2222-5333-8444-555555555555");
 g_store.pendingGamesCount=1;g_store.pendingGames[0]=record.base;
 on_disk=commit_ok=delete_ok=true;deletes=audits=commits=saves=0;now_us=1000000;
}
static std::string prepare(){
 http_pending_game_export_delivered(record.base.externalId,record_hash(&record));
 char token[24]{};assert(http_pending_game_prepare_discard(record.base.externalId,"2468",token,sizeof(token)));return token;
}
static bool discard(const std::string &token,const char *pin="2468"){
 std::string confirm=std::string("VERWERFEN ")+record.base.externalId;
 return http_pending_game_discard(record.base.externalId,pin,token.c_str(),confirm.c_str());
}
int main(){
 setup();char token[24]{};
 assert(!http_pending_game_prepare_discard(record.base.externalId,"wrong",token,sizeof(token)));
 assert(!http_pending_game_prepare_discard(record.base.externalId,"2468",token,sizeof(token)));
 auto first=prepare();assert(!discard(first,"wrong")&&deletes==0);
 for(auto &entry:s_exports){strcpy(entry.id,"old-game");entry.checksum=123;}
 first=prepare(); // full receipt cache must not disable recovery until reboot
 assert(!discard("forged")&&deletes==0);
 assert(!http_pending_game_discard(record.base.externalId,"2468",first.c_str(),"YES")&&deletes==0);
 now_us+=61000000;assert(!discard(first)&&deletes==0);
 first=prepare();record.base.lauf++;assert(!discard(first)&&deletes==0);
 first=prepare();g_store.syncStatus=SYNC_RUNNING;assert(!discard(first)&&deletes==0);
 setup();first=prepare();commit_ok=false;
 assert(!discard(first)&&on_disk&&g_store.pendingGamesCount==1&&deletes==0);
 setup();first=prepare();delete_ok=false;
 assert(!discard(first)&&on_disk&&g_store.pendingGamesCount==1&&deletes==1);
 setup();first=prepare();assert(discard(first));
 assert(!on_disk&&g_store.pendingGamesCount==0&&audits==1&&commits==1&&saves==1);
 assert(!discard(first)); // one-use challenge and no record left
 puts("PASS real recovery: PIN, export receipt, two confirmations, expiry, mutation, busy, UI timeout, delete rollback and successful audited discard.");
}
'''
with tempfile.TemporaryDirectory(prefix="storage-recovery-") as directory:
    path=Path(directory);source=path/"test.cpp"
    source.write_text(STUBS+"\n".join(fixture.function(SOURCE,s) for s in FUNCTIONS)+MAIN)
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT/"main/store"),str(source),"-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)

# A diagnostic batch error (including HTTP 400) must not override the result
# already obtained for games, credits and atomic dataset publication.
cycle_source=SOURCE[SOURCE.rfind("static esp_err_t http_run_sync_cycle("):]
cycle=fixture.function(cycle_source,"static esp_err_t http_run_sync_cycle(")
after_logs=cycle.split("upload_terminal_logs();",1)[1]
assert re.search(r"return\s+sync_err\s*;",after_logs)
assert not re.search(r"sync_err\s*=(?!=)",after_logs)
print("PASS log-upload result cannot overwrite the completed sync result.")
