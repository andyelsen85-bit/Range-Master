#!/usr/bin/env python3
"""Run actual firmware reconciliation against controlled portal/outbox adapters."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/"main/net/http_sync.cpp").read_text()
start=source.index("static esp_err_t http_reconcile_day_players()")
end=source.index("\nstatic esp_err_t http_sync_billing_impl",start)
STUBS=r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <initializer_list>
using esp_err_t=int;using TickType_t=unsigned;
constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_ERR_NO_MEM=2,ESP_ERR_INVALID_RESPONSE=3,ESP_ERR_TIMEOUT=4;
constexpr int HTTP_BUF_SIZE=32768,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,MAX_PORTAL_SPIELER=20;
const char *TAG="test";int64_t s_cycle_deadline_us=0;
int64_t esp_timer_get_time(){return 100;}
void log(const char*,const char*,...){}
#define ESP_LOGI(...) log(__VA_ARGS__)
struct cJSON{
 int type=0,valueint=0;std::string text;char *valuestring=nullptr;
 std::map<std::string,cJSON*>fields;std::vector<cJSON*>items;
}root,players,rows[6],ids[6],owners[6];
cJSON*cJSON_Parse(const char*){return &root;}
cJSON*cJSON_GetObjectItem(cJSON*p,const char*k){return p&&p->fields.count(k)?p->fields[k]:nullptr;}
bool cJSON_IsArray(cJSON*p){return p&&p->type==1;}
bool cJSON_IsNumber(cJSON*p){return p&&p->type==2;}
bool cJSON_IsString(cJSON*p){return p&&p->type==3;}
double cJSON_GetNumberValue(cJSON*p){return p?p->valueint:0;}
void cJSON_Delete(cJSON*){}
#define cJSON_ArrayForEach(p,a) for(size_t j=0;j<(a)->items.size()&&((p)=(a)->items[j],true);j++)
struct{char kreditDatum[11]="2026-10-18";int kreditPlayerIds[20]={1,4,-7};}g_store;
bool day_inventory_today(char*out,size_t n){snprintf(out,n,"2026-10-18");return true;}
bool terminal_id(char*out,size_t n){snprintf(out,n,"SELF");return true;}
void*heap_caps_malloc(size_t n,int){return malloc(n);}void heap_caps_free(void*p){free(p);}
bool sync_commit_begin(const char*,TickType_t*t){*t=0;return true;}
void sync_commit_end(const char*,TickType_t){}
int http_get_json(const char*path,char*out,size_t n){
 assert(strstr(path,"gameday-players?datum=2026-10-18"));snprintf(out,n,"{}");return 0;
}
std::vector<std::pair<int,bool>> events;std::set<int>pending;int pushes=0;
bool day_players_has_event(int id,const char*){return pending.count(id);}
bool day_players_prepare(int id,bool add){events.push_back({id,add});pending.insert(id);return true;}
void day_players_finish(bool committed){assert(committed);}
int day_players_pending(){return (int)pending.size();}
int http_push_day_players(){pushes++;return 0;}
void terminal_log_add(const char*,const char*,const char*){}
void remote(std::initializer_list<std::pair<int,const char*>> values){
 root.fields={{"players",&players}};players.type=1;players.items.clear();int i=0;
 for(auto [id,owner]:values){
  ids[i].type=2;ids[i].valueint=id;owners[i].type=3;owners[i].text=owner;
  owners[i].valuestring=owners[i].text.data();
  rows[i].fields={{"spielerId",&ids[i]},{"terminalId",&owners[i]}};
  players.items.push_back(&rows[i]);i++;
 }
}
'''
MAIN=r'''
int main(){
 remote({{2,"SELF"},{3,"OTHER"},{4,"OTHER"}});
 assert(http_reconcile_day_players()==0);
 assert((events==std::vector<std::pair<int,bool>>{{1,true},{2,false}}));
 assert(pushes==1); // preexisting local entry imported; foreign registrations retained
 assert(http_reconcile_day_players()==0&&events.size()==2); // pending intents suppress duplicates
 pending.clear();events.clear();strcpy(g_store.kreditDatum,"2026-10-17");
 assert(http_reconcile_day_players()==0);
 assert((events==std::vector<std::pair<int,bool>>{{2,false}})); // no yesterday ADDs
 pending.clear();events.clear();strcpy(g_store.kreditDatum,"2026-10-18");remote({});
 assert(http_reconcile_day_players()==0&&events.size()==2);
 assert(events[0].first==1&&events[1].first==4); // negative IDs wait for remap
 pending.clear();events.clear();s_cycle_deadline_us=50;
 assert(http_reconcile_day_players()==0&&events.empty()); // no boot-budget overrun loop
 puts("PASS real day-player reconciliation: preexisting import, own-only REMOVE, pending suppression, date fence, negative IDs and deadline.");
}
'''
with tempfile.TemporaryDirectory(prefix="rm-day-reconcile-") as temp:
 p=Path(temp);(p/"test.cpp").write_text(STUBS+source[start:end]+MAIN)
 subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",str(p/"test.cpp"),"-o",str(p/"test")],check=True)
 subprocess.run([str(p/"test")],check=True)
