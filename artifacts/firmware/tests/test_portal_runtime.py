#!/usr/bin/env python3
"""Real terminal queue codec/slot I/O, live JSON builder, coalescing and FIRE isolation."""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",Path(__file__).with_name("test_machine_batch.py"))
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
source=(ROOT/"main/net/portal_runtime.cpp").read_text()
idf=Path(os.environ.get("IDF_PATH","/tmp/esp-idf"))
cjson=idf/"components/json/cJSON/cJSON.c"
assert cjson.exists(),"Host JSON test needs the installed ESP-IDF cJSON sources"
common=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>
#include "game_store.h"
#include "portal_model.h"
#include "portal_storage.h"
#include "portal_queue_json.h"
#include "cJSON.h"
bool http_terminal_id(char *out,size_t n){snprintf(out,n,"ESP32-P4-AABBCCDDEEFF");return true;}
void utf8_to_display(char *out,const char *value,size_t n){snprintf(out,n,"%s",value);}
static const char *modes[]={"NORMAL","HARAKIRI","CUSTOM_1","CUSTOM_2","CUSTOM_3","CUSTOM_4","HARAKIRI_CUSTOM"};
'''
copy=source[source.index("struct LiveCopy {"):source.index("void portal_live_changed(")]
builder=fixture.function(source,"static void liveJson(")
main=r'''
int main(int argc,char **argv) {
 assert(argc==2);
 const char *valid=R"({"version":"opaque-hash","settings":{"queueEnabled":true},"games":[{"queueGameId":17,"status":"OPEN","modus":"HARAKIRI_CUSTOM","players":[{"entryId":12,"spielerId":1,"name":"Anna"}]}]})";
 PortalQueueSnapshot parsed={};assert(portal_queue_parse(valid,&parsed));
 assert(parsed.count==1&&parsed.total==1&&parsed.enabled&&parsed.games[0].mode==6&&parsed.games[0].players[0].entryId==12);
 auto original=parsed;
 for(const auto &bad:{"not json",R"({"version":"x","settings":{"queueEnabled":true},"games":[{"queueGameId":1,"status":"OPEN","modus":"NORMAL","players":[]}]})",
  R"({"version":"x","settings":{"queueEnabled":true},"games":[{"queueGameId":1,"status":"OPEN","modus":"NORMAL","players":[{"entryId":1,"spielerId":1,"name":"A"},{"entryId":2,"spielerId":1,"name":"B"}]}]})",
  R"({"version":"x","settings":{"queueEnabled":true},"games":[{"queueGameId":1,"status":"OPEN","modus":"NORMAL","players":[{"spielerId":1,"name":"A"}]}]})",
  R"({"version":"x","settings":{"queueEnabled":true},"games":[{"queueGameId":1,"status":"OPEN","modus":"UNKNOWN","players":[{"entryId":1,"spielerId":1,"name":"A"}]}]})"}) {
  assert(!portal_queue_parse(bad,&parsed));assert(!memcmp(&parsed,&original,sizeof(parsed)));
 }
 assert(portal_queue_parse(R"({"version":"next","settings":{"queueEnabled":false},"games":[]})",&parsed)&&parsed.count==0&&!parsed.enabled);
 PortalQueueGame game={};game.id=17;game.count=2;game.players[0].id=1;game.players[1].id=2;
 int previous[6]={0,8,0,9,0,0},result[6]={};
 assert(portal_queue_merge(previous,game,true,result));
 assert(result[0]==1&&result[1]==8&&result[2]==2&&result[3]==9);
 assert(portal_queue_merge(previous,game,false,result)&&result[0]==1&&result[1]==2&&result[2]==0);
 int full[6]={7,8,9,10,11,12};assert(!portal_queue_merge(full,game,true,result));
 game.count=7;assert(!portal_queue_merge(previous,game,true,result));game.count=2;
 PortalLiveGate gate;gate.changed();gate.changed();gate.changed();
 assert(!gate.ready(0,false,false,false));assert(!gate.ready(0,true,true,false));
 assert(!gate.ready(0,true,false,true));assert(gate.ready(0,true,false,false));
 gate.claim(0);assert(gate.consumed==3&&!gate.ready(3000000,true,false,false));
 gate.changed();assert(!gate.ready(1499999,true,false,false));
 assert(gate.ready(1500000,true,false,false));gate.claim(1500000);
 PortalQueueRecord record={};record.magic=0x51554555;record.version=1;record.generation=1;
 record.reserved=game;record.count=1;record.events[0]={17,1,"11111111-1111-4111-8111-111111111111"};
 record.checksum=portal_queue_checksum(record);assert(portal_queue_valid(record));
 std::string first=std::string(argv[1])+"/queue0.bin",second=std::string(argv[1])+"/queue1.bin";
 assert(portal_queue_write_file(first.c_str(),record));
 PortalQueueRecord rebooted={};assert(portal_queue_read_file(first.c_str(),&rebooted));
 assert(!memcmp(&record,&rebooted,sizeof(record)));
 auto immutable=rebooted.events[0];record.generation++;record.count=0;record.checksum=portal_queue_checksum(record);
 assert(portal_queue_write_file(second.c_str(),record));
 FILE *f=fopen(second.c_str(),"r+b");assert(f);assert(fseek(f,24,SEEK_SET)==0);fputc(0xff,f);fclose(f);
 assert(!portal_queue_read_file(second.c_str(),&rebooted));assert(portal_queue_read_file(first.c_str(),&rebooted));
 assert(rebooted.count==1&&!memcmp(&immutable,&rebooted.events[0],sizeof(immutable))); // lost ACK safely replays
 assert(!portal_queue_write_file("/no-such-terminal-directory/queue.bin",record));
 LiveCopy s={};s.seq=17;s.state=1;s.mode=0;s.run=1;s.clays=9;s.started=1792310400;
 s.currentId=1;s.currentClay=2;s.count=2;s.resultCount=3;
 snprintf(s.externalId,sizeof(s.externalId),"11111111-1111-4111-8111-111111111111");
 s.players[0].id=1;s.players[0].startPosten=1;strcpy(s.players[0].name,"Anna \"Ä\"\n");
 s.players[1].id=2;s.players[1].startPosten=2;strcpy(s.players[1].name,"Ben");
 s.results[0]={1,1,1,MASCHINE_A,1,true,false,2,false};
 s.results[1]={1,1,2,MASCHINE_B,2,false,false,0,true};
 s.results[2]={2,1,1,MASCHINE_C,2,false,false,0,false};
 char output[65536];liveJson(s,output,sizeof(output));auto *json=cJSON_Parse(output);assert(json);
 assert(cJSON_GetNumberValue(cJSON_GetObjectItem(json,"seq"))==17);
 auto *players=cJSON_GetObjectItem(json,"players");assert(cJSON_GetArraySize(players)==2);
 assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(players,0),"name")),"Anna \"Ä\"\n"));
 assert(cJSON_GetArraySize(cJSON_GetObjectItem(json,"results"))==2);cJSON_Delete(json);
 s.count=6;s.clays=32;s.resultCount=MAX_ERGEBNISSE;s.mode=5;
 for(int p=0;p<6;p++){s.players[p].id=p+1;s.players[p].startPosten=p+1;strcpy(s.players[p].name,"Langer Spielername ÄÖÜ");}
 for(int i=0;i<MAX_ERGEBNISSE;i++)s.results[i]={i%6+1,1+i/192,1+(i/6)%32,MASCHINE_H,1,true,false,2,false};
 liveJson(s,output,sizeof(output));assert(strlen(output)>4096&&strlen(output)<sizeof(output));
 json=cJSON_Parse(output);assert(json&&cJSON_GetArraySize(cJSON_GetObjectItem(json,"results"))==MAX_ERGEBNISSE);cJSON_Delete(json);
 char tiny[128];liveJson(s,tiny,sizeof(tiny));assert(tiny[0]==0);
 s.state=3;s.count=0;s.resultCount=0;liveJson(s,output,sizeof(output));json=cJSON_Parse(output);
 assert(json&&!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(json,"state")),"ABORTED"));cJSON_Delete(json);
 puts("queue slot durability, immutable replay, merge, live coalescing/rate/JSON bounds/escaping passed");
}
'''
with tempfile.TemporaryDirectory(prefix="terminal-portal-") as td:
 temp=Path(td);cpp=temp/"test.cpp";cpp.write_text(common+copy+builder+main)
 subprocess.run(["cc","-c",str(cjson),"-I",str(cjson.parent),"-o",str(temp/"json.o")],check=True)
 subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT/"main/store"),"-I",str(ROOT/"main/net"),"-I",str(ROOT/"main/ui"),"-I",str(cjson.parent),
   str(cpp),str(ROOT/"main/net/portal_queue_json.cpp"),str(temp/"json.o"),"-o",str(temp/"test")],check=True)
 subprocess.run([str(temp/"test"),td],check=True)
fire=(ROOT/"lora_stub/lora_stub.cpp").read_text() if (ROOT/"lora_stub/lora_stub.cpp").exists() else (ROOT/"main/lora_stub/lora_stub.cpp").read_text()
for signature in ["static void fire_action_cb(","void store_account_acknowledged_clays("]:
 path=ROOT/"main/ui/screen_spiel.cpp" if "fire_action" in signature else ROOT/"main/store/game_store.cpp"
 text=path.read_text()
 if signature in text:assert "portal_live" not in fixture.function(text,signature)
assert "portal_live" not in fire
assert "store_start_spiel(" not in source,"Queue takeover must never auto-start a game"
print("FIRE isolation and manual start policy passed")
