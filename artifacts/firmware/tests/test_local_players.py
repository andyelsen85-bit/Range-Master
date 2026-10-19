#!/usr/bin/env python3
"""Exercise the actual local-player CREATE state machine and RAM remap.

Deterministic host fault tests; no emulator or physical timing claims.
"""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",Path(__file__).with_name("test_machine_batch.py"))
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
http=(ROOT/"main/net/http_sync.cpp").read_text()
store=(ROOT/"main/store/game_store.cpp").read_text()
functions="\n".join([
    fixture.function(store,"bool store_save_player_identity("),
    fixture.function(store,"void store_remap_spieler_id("),
    fixture.function(store,"void store_add_lokal_spieler("),
    fixture.function(http,"static void log_waiting_local_player("),
    fixture.function(http,"void http_sync_copy_last_error("),
    fixture.function(http,"static esp_err_t sync_create_local_player("),
])
STUBS=r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cctype>
#include <vector>
#include <string>
#include <atomic>
#include "game_store.h"
#include "spieler_update_compat.h"
#include "payment_receipts.h"
GameStore g_store{};
static PaymentReceipt s_payment_receipts[2];
static int s_payment_receipt_count=1;
static bool s_spieler_updates_unreadable=false;
static int s_next_local_id=-1;
using esp_err_t=int;using TickType_t=unsigned;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=-2,ESP_ERR_TIMEOUT=-3;
constexpr int OFFLINE_CACHE_ROSTER=0,OFFLINE_CACHE_HISTORY=1,OFFLINE_CACHE_CREDITS=2,
 OFFLINE_CACHE_SALES=3,OFFLINE_CACHE_BILLS=4;
static unsigned calls=0,remaps=0,save_calls=0,fail_save_call=0,pushed=0;
static bool journal_ok=true,ledgers_ok=true,ui_ok=true,http_ok=true;
static bool day_flush_ok=true,payment_ok=true;
static int sale_result=ESP_OK,payment_locks=0;
static int day_id=-7,ui_id=-7;
static SpielerUpdateEntry disk{};
static std::vector<std::string> logs;
static int s_error_lock=0;
static std::atomic<int> s_waiting_local_games{0};
static char s_last_error[384]={},s_contract_error[160]={};
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
void terminal_log_add(const char*,const char*,const char *message){logs.push_back(message);}
bool offline_cache_save(int){return true;}
void game_store_save(){}
unsigned esp_random(){static unsigned n=100;return ++n;}
void day_players_remap(int old_id,int new_id){if(day_id==old_id)day_id=new_id;}
void ui_manager_remap_player_identity(int old_id,int new_id){if(ui_id==old_id)ui_id=new_id;}
void store_remap_lineup_spieler(int old_id,int new_id){
 for(auto &id:g_store.lineupIds)if(id==old_id)id=new_id;
}
bool store_save_spieler_updates(){
 ++save_calls;if(save_calls==fail_save_call)return false;
 disk=g_store.spielerUpdates[0];return true;
}
void payment_state_lock(){++payment_locks;}
void payment_state_unlock(){--payment_locks;}
bool save_kredit_state_unlocked(){return ledgers_ok;}
esp_err_t save_sale_state_unlocked(){return sale_result;}
bool save_payment_state_unlocked(){return payment_ok;}
bool day_players_flush(){return day_flush_ok;}
bool game_journal_remap(int old_id,int new_id){
 assert(disk.used&&disk.portalId==new_id);
 assert(old_id==-7&&new_id==52);++remaps;return journal_ok;
}
bool sync_commit_begin(const char*,TickType_t *t){*t=0;return ui_ok;}
bool ui_manager_snapshot_lock(bool *acquired){*acquired=false;return true;}
void ui_manager_unlock(){}
void sync_commit_end(const char*,TickType_t){}
void note_pushed(const char*,int n){pushed+=n;}
void set_http_error(const char*,const char*,const char*){}
struct cJSON{double valuedouble=52;};
cJSON *cJSON_CreateObject(){return new cJSON;}
void cJSON_AddStringToObject(cJSON*,const char*,const char*){}
char *cJSON_PrintUnformatted(cJSON*){return strdup("{}");}
void cJSON_Delete(cJSON *p){delete p;}
cJSON *cJSON_Parse(const char*){return new cJSON;}
const cJSON *cJSON_GetObjectItem(const cJSON *p,const char*){return p;}
bool cJSON_IsNumber(const cJSON *p){return p!=nullptr;}
esp_err_t http_post_json(const char *path,const char*,char *out,size_t n){
 assert(!strcmp(path,"/api/sync/spieler-neu"));++calls;
 if(!http_ok)return ESP_FAIL;
 snprintf(out,n,"{\"id\":52}");return ESP_OK;
}
'''
MAIN=r'''
void setup() {
 g_store={};calls=remaps=save_calls=pushed=fail_save_call=0;
 s_next_local_id=-1;
 journal_ok=ledgers_ok=ui_ok=http_ok=true;day_id=ui_id=-7;disk={};
 day_flush_ok=payment_ok=true;sale_result=ESP_OK;payment_locks=0;
 auto &e=g_store.spielerUpdates[0];g_store.spielerUpdateCount=1;
 e.used=true;e.typ=SPIELER_CREATE;e.spielerId=-7;
 strcpy(e.name,"Local Player");strcpy(e.externalId,"new-stable-create-identity");
 g_store.portalSpielerCount=1;g_store.portalSpieler[0].id=-7;
 g_store.portalSpieler[0].lokal=true;strcpy(g_store.portalSpieler[0].name,e.name);
 g_store.spielerCount=1;g_store.spieler[0].id=-7;g_store.lineupIds[0]=-7;
 g_store.kreditPlayerIds[0]=-7;g_store.kredite[0]={5,1};
 g_store.pendingKreditEventCount=1;g_store.pendingKreditEvents[0].spielerId=-7;
 g_store.pendingVerkaufEventCount=1;g_store.pendingVerkaufEvents[0].spielerId=-7;
 g_store.pendingPaymentEventCount=1;g_store.pendingPaymentEvents[0].spielerId=-7;
 g_store.activeGameCreditCount=1;g_store.activeGameCreditPlayerIds[0]=-7;
 g_store.ergebnisseCount=1;g_store.ergebnisse[0].spielerId=-7;
 g_store.pendingGamesCount=1;g_store.pendingGames[0].teilnahmen_count=1;
 g_store.pendingGames[0].teilnahmen[0].spielerId=-7;
 g_store.pendingGames[0].ergebnisse_count=1;g_store.pendingGames[0].ergebnisse[0].spielerId=-7;
 g_store.historyCount=1;g_store.history[0].spieler_count=1;g_store.history[0].spielerIds[0]=-7;
 g_store.hasLastFinished=true;g_store.lastFinished.spieler_count=1;g_store.lastFinished.spielerIds[0]=-7;
 s_payment_receipts[0].spielerId=-7;
}
int main() {
 LegacySpielerUpdateEntry legacy{};legacy.used=true;legacy.spielerId=-7;legacy.typ=SPIELER_CREATE;
 strcpy(legacy.externalId,"new-original-event");strcpy(legacy.name,"Old queued player");
 SpielerUpdateEntry upgraded{};spieler_update_upgrade(legacy,&upgraded);
 assert(upgraded.used&&upgraded.spielerId==-7&&upgraded.portalId==0);
 assert(!strcmp(upgraded.externalId,legacy.externalId)&&!strcmp(upgraded.name,legacy.name));
 setup();journal_ok=false;
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_FAIL);
 assert(calls==1&&disk.used&&disk.portalId==52&&disk.spielerId==-7);
 // Reboot after the HTTP response but before disk remap: receipt survives.
 g_store.spielerUpdates[0]=disk;journal_ok=true;
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK);
 assert(calls==1&&remaps==2&&!disk.used&&pushed==1);
 assert(g_store.portalSpieler[0].id==52&&!g_store.portalSpieler[0].lokal);
 assert(g_store.spieler[0].id==52&&g_store.lineupIds[0]==52&&day_id==52&&ui_id==52);
 assert(g_store.kreditPlayerIds[0]==52&&g_store.kredite[0].gewaehrt==5&&g_store.kredite[0].verbraucht==1);
 assert(g_store.pendingKreditEvents[0].spielerId==52&&g_store.pendingVerkaufEvents[0].spielerId==52);
 assert(g_store.pendingPaymentEvents[0].spielerId==52&&s_payment_receipts[0].spielerId==52);
 assert(g_store.activeGameCreditPlayerIds[0]==52&&g_store.ergebnisse[0].spielerId==52);
 assert(g_store.pendingGames[0].teilnahmen[0].spielerId==52&&g_store.pendingGames[0].ergebnisse[0].spielerId==52);
 assert(g_store.history[0].spielerIds[0]==52&&g_store.lastFinished.spielerIds[0]==52);
 setup();ledgers_ok=false;
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_FAIL);
 assert(g_store.spielerUpdates[0].used&&g_store.spielerUpdates[0].spielerId==-7);
 ledgers_ok=true;assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK&&calls==1);
 setup();fail_save_call=1; // receipt write fails
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_FAIL&&remaps==0);
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK&&calls==1);
 setup();fail_save_call=3; // receipt, identity flush, then final create ACK
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_FAIL);
 assert(g_store.spielerUpdates[0].used&&disk.used&&disk.portalId==52);
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK&&calls==1);
 setup();sale_result=ESP_FAIL;
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_FAIL&&payment_locks==0);
 assert(g_store.spielerUpdates[0].used&&disk.used);
 sale_result=ESP_OK;assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK&&calls==1);
 setup();payment_ok=false;
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_FAIL&&payment_locks==0);
 payment_ok=true;assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK&&calls==1);
 setup();day_flush_ok=false;
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_FAIL&&disk.used);
 day_flush_ok=true;assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK&&calls==1);
 setup();fail_save_call=2; // CREATE checkpoint within identity persistence
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_FAIL&&disk.used);
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK&&calls==1);
 setup();ui_ok=false;
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_ERR_TIMEOUT);
 ui_ok=true;assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK&&calls==1);
 setup();http_ok=false;
 assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_FAIL&&g_store.spielerUpdates[0].portalId==0);
 http_ok=true;assert(sync_create_local_player(&g_store.spielerUpdates[0])==ESP_OK&&calls==2);
 setup();int id=0;store_add_lokal_spieler("Another local",&id);
 assert(id==-8&&g_store.spielerUpdates[1].portalId==0&&strlen(g_store.spielerUpdates[1].externalId)==35);
 assert(g_store.portalSpieler[1].aktiv);
 g_store.portalSpieler[1].id=52;g_store.spielerUpdates[1].used=false;
 store_add_lokal_spieler("After a remap",&id);assert(id==-9);
 g_store.spielerUpdateCount=MAX_SPIELER_UPDATES;store_add_lokal_spieler("Overflow",&id);assert(id==0);
 setup();fail_save_call=1;store_add_lokal_spieler("Unsaved local",&id);
 assert(id==0&&g_store.portalSpielerCount==1&&g_store.spielerUpdateCount==1);
 setup();store_add_lokal_spieler("   ",&id);assert(id==0);
 store_add_lokal_spieler("  Trimmed Local  ",&id);
 assert(id==-8&&!strcmp(g_store.portalSpieler[1].name,"Trimmed Local"));
 setup();logs.clear();int seen[MAX_PORTAL_SPIELER]={},seen_count=0;
 log_waiting_local_player(-7,seen,&seen_count);log_waiting_local_player(-7,seen,&seen_count);
 assert(logs.size()==1&&logs[0].find("Local Player")!=std::string::npos);
 assert(logs[0].find("Portal-ID")!=std::string::npos);
 g_store.spielerUpdates[0].portalId=52;seen_count=0;
 log_waiting_local_player(-7,seen,&seen_count);
 assert(logs.size()==2&&logs[1].find("dauerhaft")!=std::string::npos);
 s_waiting_local_games=3;char status[128];http_sync_copy_last_error(status,sizeof(status));
 assert(!strcmp(status,"3 Spiele warten auf neue Spieler"));
 puts("PASS local CREATE: durable portalId, remap/storage/UI/ACK retries without duplicate POST, legacy ABI, all RAM ledgers, negative-ID allocation");
}
'''
with tempfile.TemporaryDirectory(prefix="tm-local-players-") as directory:
    path=Path(directory);(path/"test.cpp").write_text(STUBS+functions+MAIN)
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-unused-function",
                    "-I",str(ROOT/"main/store"),str(path/"test.cpp"),"-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True,timeout=20)
assert "entry.spielerId>0" in (ROOT/"main/store/day_player_events.cpp").read_text() or \
       "data->events[i].spielerId>0" in (ROOT/"main/store/day_player_events.cpp").read_text()
assert "active_game_remap()" in (ROOT/"main/store/game_journal.cpp").read_text()
assert '"%d Spiele warten auf neue Spieler' in http
print("PASS queue waits for positive IDs, active-slot remap included, waiting count exposed in sync status")
