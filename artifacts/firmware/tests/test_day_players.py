#!/usr/bin/env python3
"""Real durable day-player outbox, host filesystem only; no UI/emulator."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
STUBS={
"game_store.h":"""#pragma once
struct Store {int kreditPlayerIds[16]{};char kreditDatum[11]{};};extern Store g_store;""",
"freertos/FreeRTOS.h":"""#pragma once
#define portMAX_DELAY 0
""",
"freertos/semphr.h":"""#pragma once
#include <mutex>
using SemaphoreHandle_t=std::recursive_mutex*;
inline auto xSemaphoreCreateMutex(){return new std::recursive_mutex;}
inline void xSemaphoreTake(SemaphoreHandle_t m,int){m->lock();}
inline void xSemaphoreGive(SemaphoreHandle_t m){m->unlock();}
""",
"esp_heap_caps.h":"""#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void *heap_caps_malloc(size_t n,int){return malloc(n);}
inline void *heap_caps_calloc(size_t n,size_t s,int){return calloc(n,s);}
inline void heap_caps_free(void *p){free(p);}
""",
"esp_random.h":"""#pragma once
#include <random>
inline void esp_fill_random(void *p,size_t n){static std::mt19937 r(42);auto *b=(unsigned char*)p;while(n--)*b++=r();}
""",
"game_journal.h":"""#pragma once
inline void game_journal_storage_lock(){}
inline void game_journal_storage_unlock(){}
inline int game_journal_resolve_player_locked(int id){return id;}
""",
"storage_write.h":"""#pragma once
#include <cstdio>
#include <unistd.h>
extern bool failWrite;
inline bool storage_write_committed(FILE*f,const void*p,size_t n){return !failWrite&&fwrite(p,1,n,f)==n&&fflush(f)==0&&fsync(fileno(f))==0;}
""",
"terminal_logs.h":"""#pragma once
#include <string>
#include <vector>
extern std::vector<std::string> logs;
inline void terminal_log_add(const char*,const char*,const char *s,const void* =nullptr){logs.emplace_back(s);}
""",
}
CPP=r'''
#include <cassert>
#include <cstring>
#include <ctime>
#include <cstdio>
#include "game_store.h"
#include "terminal_logs.h"
#include "day_player_events.h"
Store g_store{};bool failWrite=false;std::vector<std::string> logs;
int main(int argc,char **argv) {
 setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();
 time_t now=time(nullptr);tm local{};localtime_r(&now,&local);
 strftime(g_store.kreditDatum,sizeof(g_store.kreditDatum),"%Y-%m-%d",&local);
 if(argc>1&&!strcmp(argv[1],"proof")){
  assert(day_players_repair_safe());puts("PASS mount-time proof uses latest empty slot, not older acknowledged events.");return 0;
 }
 day_players_init();assert(day_players_pending()==0&&day_players_repair_safe());
 if(argc>1&&!strcmp(argv[1],"seed")){
  assert(day_players_prepare(123,true));g_store.kreditPlayerIds[0]=123;day_players_finish(true);
  DayPlayerEvent item{};assert(day_players_snapshot(&item,1)==1);assert(day_players_ack(item.externalId,false));return 0;
 }
 assert(day_players_prepare(123,true));g_store.kreditPlayerIds[0]=123;day_players_finish(true);
 DayPlayerEvent out[128];assert(day_players_snapshot(out,128)==1);
 DayPlayerEvent added=out[0];assert(added.add&&strlen(added.externalId)==36&&strlen(added.occurredAt)==24);
 day_players_init();assert(day_players_snapshot(out,128)==1&&!strcmp(out[0].externalId,added.externalId));
 assert(day_players_prepare(123,false));g_store.kreditPlayerIds[0]=0;day_players_finish(true);
 assert(day_players_snapshot(out,128)==2&&!out[1].add&&strcmp(out[0].occurredAt,out[1].occurredAt)<0);
 assert(day_players_prepare(-7,true));g_store.kreditPlayerIds[1]=-7;day_players_finish(true);
 assert(day_players_snapshot(out,128)==2);day_players_remap(-7,456);g_store.kreditPlayerIds[1]=456;
 assert(day_players_flush()&&day_players_snapshot(out,128)==3&&out[2].spielerId==456);
 assert(day_players_ack(added.externalId,true));assert(day_players_snapshot(out,128)==2&&day_players_pending()==3);
 failWrite=true;assert(!day_players_ack(out[0].externalId));failWrite=false;
 day_players_init();assert(day_players_pending()==3&&day_players_snapshot(out,128)==2);
 assert(day_players_retry_rejected()&&day_players_snapshot(out,128)==3);
 for(int i=0;i<3;i++)assert(day_players_ack(out[i].externalId));
 assert(day_players_repair_safe());
 // Prepared intent with no NVS commit is discarded on reboot.
 assert(day_players_prepare(900,true));day_players_init();assert(day_players_pending()==0);
 // NVS committed, but power cut before finish: promote the durable intent.
 assert(day_players_prepare(901,true));g_store.kreditPlayerIds[2]=901;
 day_players_init();assert(day_players_snapshot(out,128)==1&&out[0].spielerId==901);
 assert(day_players_ack(out[0].externalId));
 // Bounded queue refuses changes; never silently drops its oldest event.
 for(int i=0;i<DAY_PLAYER_EVENTS_MAX;i++){assert(day_players_prepare(1000+i,true));day_players_finish(true);}
 assert(!day_players_prepare(9999,true)&&day_players_pending()==DAY_PLAYER_EVENTS_MAX);
 assert(!day_players_repair_safe());
 for(int i=0;i<2;i++){char path[512];snprintf(path,sizeof(path),DAY_PLAYERS_ROOT "/DAYP%d.BIN",i);FILE*f=fopen(path,"wb");assert(f);fputs("corrupt",f);fclose(f);}
 day_players_init();assert(day_players_pending()==-1&&!day_players_flush()&&!day_players_repair_safe());
 assert(!logs.empty());
 puts("PASS durable day-player ADD/REMOVE order, reboot intent recovery, ID remap, 4xx retention/manual retry, failed ack, capacity and corruption refusal.");
}
'''
with tempfile.TemporaryDirectory(prefix="rm-day-players-") as tmp:
 p=Path(tmp);inc=p/"inc";fat=p/"fat";fat.mkdir()
 for name,text in STUBS.items():
  dest=inc/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(text)
 source=(ROOT/"main/store/day_player_events.cpp").read_text()
 # Quoted includes otherwise prefer the production header beside the .cpp.
 (p/"outbox.cpp").write_text(source);(p/"test.cpp").write_text(CPP)
 subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",f'-DDAY_PLAYERS_ROOT="{fat}"',
  "-I"+str(inc),"-I"+str(ROOT/"main/store"),str(p/"test.cpp"),str(p/"outbox.cpp"),"-o",str(p/"test")],check=True)
 subprocess.run([str(p/"test")],check=True)
 for f in fat.iterdir():f.unlink()
 subprocess.run([str(p/"test"),"seed"],check=True)
 subprocess.run([str(p/"test"),"proof"],check=True)
