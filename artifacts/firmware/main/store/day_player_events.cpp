#include "day_player_events.h"
#include "game_store.h"
#include "game_journal.h"
#include "storage_write.h"
#include "terminal_logs.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <dirent.h>
#include <algorithm>
#include <atomic>
#ifndef DAY_PLAYERS_ROOT
#define DAY_PLAYERS_ROOT "/fatfs"
#endif
namespace {
struct Record { uint32_t magic,version,generation,checksum;int64_t lastOccurredMs;int count;DayPlayerEvent events[DAY_PLAYER_EVENTS_MAX]; };
static_assert(sizeof(Record)<12288,"Day roster outbox: two slots and one bounded write <36 KiB");
Record *data;
SemaphoreHandle_t guard;
int slot=-1;
bool blocked=true,dirty=false;
std::atomic<int> pending{-1};
std::atomic<bool> initialized{false};
uint32_t checksum(const Record &r) {
    uint32_t h=2166136261u;auto *bytes=(const uint8_t*)&r;
    for(size_t i=0;i<sizeof(r);i++)if(i<offsetof(Record,checksum)||i>=offsetof(Record,checksum)+4)h=(h^bytes[i])*16777619u;
    return h;
}
void path(int n,char *out,size_t cap){snprintf(out,cap,DAY_PLAYERS_ROOT "/dayp%d.bin",n);}
bool valid(const Record &r) {
    if(r.magic!=0x44504c59||r.version!=1||r.count<0||r.count>DAY_PLAYER_EVENTS_MAX||r.checksum!=checksum(r))return false;
    for(int i=0;i<r.count;i++) {
        const auto &e=r.events[i];
        if(!e.externalId[0]||e.externalId[39]||e.datum[10]||e.occurredAt[24]||e.state>2||e.add>1||!e.spielerId)return false;
    }
    return true;
}
bool save() {
    if(blocked||!data)return false;
    int next=slot==0?1:0;char name[96];path(next,name,sizeof(name));
    uint32_t previous=data->generation;
    data->magic=0x44504c59;data->version=1;data->generation++;data->checksum=checksum(*data);
    game_journal_storage_lock();
    FILE *f=fopen(name,"wb");
    bool ok=f&&storage_write_committed(f,data,sizeof(*data));
    if(f&&fclose(f)!=0)ok=false;
    if(!ok)(void)unlink(name);
    game_journal_storage_unlock();
    if(ok){slot=next;dirty=false;}else{data->generation=previous;dirty=true;}
    return ok;
}
bool present(int player,const char *date) {
    if(strcmp(g_store.kreditDatum,date))return false;
    for(int id:g_store.kreditPlayerIds)if(id==player)return true;
    return false;
}
}
void day_players_init() {
    if(!guard)guard=xSemaphoreCreateMutex();
    if(!data)data=(Record*)heap_caps_calloc(1,sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!guard||!data){terminal_log_add("gameday","ERROR","Spieler-des-Tages Speicher nicht verfügbar");return;}
    xSemaphoreTake(guard,portMAX_DELAY);
    *data={};slot=-1;
    auto *candidate=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool any=false,found=false;
    if(!candidate){xSemaphoreGive(guard);return;}
    game_journal_storage_lock();
    for(int n=0;n<2;n++) {
        char name[96];path(n,name,sizeof(name));errno=0;FILE *f=fopen(name,"rb");
        if(!f){if(errno!=ENOENT)any=true;continue;}
        any=true;
        bool ok=fread(candidate,1,sizeof(*candidate),f)==sizeof(*candidate)&&fgetc(f)==EOF&&!ferror(f);
        if(fclose(f)!=0)ok=false;
        if(ok&&valid(*candidate)&&(!found||candidate->generation>data->generation)){*data=*candidate;slot=n;found=true;}
    }
    game_journal_storage_unlock();heap_caps_free(candidate);
    blocked=any&&!found;
    if(!blocked) {
        game_journal_storage_lock();
        for(int i=0;i<data->count;i++) {
            int resolved=game_journal_resolve_player_locked(data->events[i].spielerId);
            if(resolved!=data->events[i].spielerId){data->events[i].spielerId=resolved;dirty=true;}
        }
        game_journal_storage_unlock();
        // Finish a cut between durable intent and atomic NVS roster commit.
        for(int i=data->count-1;i>=0;i--)if(!data->events[i].state) {
            auto &e=data->events[i];
            if(present(e.spielerId,e.datum)==(bool)e.add)e.state=1;
            else {memmove(&e,&e+1,(data->count-i-1)*sizeof(e));data->count--;}
            dirty=true;
        }
        if(dirty)(void)save();
    } else terminal_log_add("gameday","ERROR","Spieler-des-Tages Outbox unlesbar; keine Daten gelöscht");
    pending.store(blocked?-1:data->count);initialized.store(true);
    xSemaphoreGive(guard);
}
bool day_players_prepare(int player,bool add) {
    if(!guard||!data)return false;
    xSemaphoreTake(guard,portMAX_DELAY);
    struct timeval tv{};gettimeofday(&tv,nullptr);
    int64_t ms=std::max((int64_t)tv.tv_sec*1000+tv.tv_usec/1000,data->lastOccurredMs+1);
    time_t now=ms/1000;bool ok=!blocked&&now>1700000000&&data->count<DAY_PLAYER_EVENTS_MAX;
    for(int i=0;ok&&i<data->count;i++)if(!data->events[i].state)ok=false;
    if(ok) {
        auto &e=data->events[data->count++];e={};e.spielerId=player;e.add=add;
        struct tm local{},utc{};localtime_r(&now,&local);gmtime_r(&now,&utc);
        strftime(e.datum,sizeof(e.datum),"%Y-%m-%d",&local);
        char stamp[24];strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%S",&utc);
        snprintf(e.occurredAt,sizeof(e.occurredAt),"%.19s.%03uZ",stamp,(unsigned)(ms%1000));
        data->lastOccurredMs=ms;
        uint8_t b[16];esp_fill_random(b,sizeof(b));b[6]=(b[6]&15)|64;b[8]=(b[8]&63)|128;
        snprintf(e.externalId,sizeof(e.externalId),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
        pending.store(data->count);
        ok=save();if(!ok){data->count--;pending.store(data->count);}
    }
    xSemaphoreGive(guard);
    if(!ok)terminal_log_add("gameday","ERROR","Tageseintrag nicht geändert: Uhrzeit, Speicher oder volle Outbox prüfen");
    return ok;
}
void day_players_finish(bool committed) {
    if(!guard||!data)return;
    xSemaphoreTake(guard,portMAX_DELAY);
    for(int i=data->count-1;i>=0;i--)if(!data->events[i].state) {
        if(committed)data->events[i].state=1;
        else {memmove(&data->events[i],&data->events[i+1],(data->count-i-1)*sizeof(DayPlayerEvent));data->count--;}
    }
    (void)save();pending.store(data->count);xSemaphoreGive(guard);
}
int day_players_snapshot(DayPlayerEvent *out,int capacity) {
    if(!guard||!data)return 0;
    xSemaphoreTake(guard,portMAX_DELAY);
    // A prepared NVS commit may have completed even if its final slot write failed.
    int n=0;
    for(int i=0;i<data->count&&n<capacity;i++)
        if(data->events[i].state==1&&data->events[i].spielerId>0)out[n++]=data->events[i];
    xSemaphoreGive(guard);return n;
}
bool day_players_ack(const char *id,bool rejected) {
    if(!guard||!data)return false;
    xSemaphoreTake(guard,portMAX_DELAY);
    bool ok=false;
    for(int i=0;i<data->count;i++)if(!strcmp(id,data->events[i].externalId)) {
        Record *prior=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!prior)break;
        *prior=*data;
        if(rejected)data->events[i].state=2;
        else {memmove(&data->events[i],&data->events[i+1],(data->count-i-1)*sizeof(DayPlayerEvent));data->count--;}
        ok=save();if(!ok)*data=*prior;heap_caps_free(prior);pending.store(data->count);break;
    }
    xSemaphoreGive(guard);return ok;
}
void day_players_remap(int oldId,int newId) {
    if(!guard||!data)return;
    xSemaphoreTake(guard,portMAX_DELAY);
    for(int i=0;i<data->count;i++)if(data->events[i].spielerId==oldId){data->events[i].spielerId=newId;dirty=true;}
    xSemaphoreGive(guard);
}
bool day_players_flush() {
    if(!guard||!data)return false;
    xSemaphoreTake(guard,portMAX_DELAY);
    bool ok=!blocked&&(!dirty||save());xSemaphoreGive(guard);return ok;
}
int day_players_pending() {
    return pending.load();
}
bool day_players_has_event(int player,const char *date) {
    if(!guard||!data)return false;
    xSemaphoreTake(guard,portMAX_DELAY);bool found=false;
    for(int i=0;i<data->count;i++)if(data->events[i].spielerId==player&&!strcmp(data->events[i].datum,date))found=true;
    xSemaphoreGive(guard);return found;
}
bool day_players_retry_rejected() {
    if(!guard||!data)return false;
    xSemaphoreTake(guard,portMAX_DELAY);bool changed=false;
    for(int i=0;i<data->count;i++)if(data->events[i].state==2){data->events[i].state=1;changed=true;}
    bool ok=!changed||save();xSemaphoreGive(guard);return ok;
}
bool day_players_repair_safe(){
    if(initialized.load())return day_players_pending()==0;
    // Mount-time repair precedes initialization. Prove accessible root plus
    // absent/valid-empty slots; never equate an unreadable slot with emptiness.
    DIR *dir=opendir(DAY_PLAYERS_ROOT);if(!dir)return false;
    if(closedir(dir)!=0)return false;
    auto *record=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record)return false;
    bool safe=true,found=false;uint32_t latest=0;int pending=0;
    for(int n=0;n<2;n++) {
        char name[96];path(n,name,sizeof(name));errno=0;FILE *file=fopen(name,"rb");
        if(!file){if(errno!=ENOENT)safe=false;continue;}
        bool ok=fread(record,1,sizeof(*record),file)==sizeof(*record)&&fgetc(file)==EOF&&!ferror(file);
        if(fclose(file)!=0)ok=false;
        if(!ok||!valid(*record)){safe=false;continue;}
        if(!found||record->generation>latest){found=true;latest=record->generation;pending=record->count;}
    }
    heap_caps_free(record);return safe&&pending==0;
}
