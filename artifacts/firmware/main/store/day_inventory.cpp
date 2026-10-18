#include "day_inventory.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
// Compact, bounded immutable outbox in the existing terminal NVS namespace.
// No bespoke network worker and no large rebuildable cache in NVS.
static constexpr int MAX_DAY_EVENTS=8;
static DayInventory state{};
static DayEvent pending[MAX_DAY_EVENTS]{};
static uint8_t count;
static nvs_handle_t handle;
static SemaphoreHandle_t mutex;
static void copy_text(char *out,size_t capacity,const char *in) {
    size_t used=0;
    while(in&&in[used]&&used+1<capacity){
        unsigned char c=(unsigned char)in[used];size_t bytes=c<0x80?1:(c&0xE0)==0xC0?2:(c&0xF0)==0xE0?3:(c&0xF8)==0xF0?4:0;
        if(!bytes||used+bytes>=capacity)break;
        bool valid=true;for(size_t i=1;i<bytes;i++)if(!in[used+i]||((unsigned char)in[used+i]&0xC0)!=0x80){valid=false;break;}
        if(!valid)break;
        used+=bytes;
    }
    if(used)memmove(out,in,used);
    out[used]=0;
}
bool day_inventory_today(char *out,size_t capacity) {
    time_t now=time(NULL);struct tm t;localtime_r(&now,&t);
    if(t.tm_year<124)return false;
    return strftime(out,capacity,"%Y-%m-%d",&t)>0;
}
static void current_day() {
    char date[11];if(!day_inventory_today(date,sizeof(date))||!strcmp(date,state.datum))return;
    if(state.hasOpening) {
        memcpy(state.previousDate,state.datum,sizeof(state.previousDate));
        state.previous12=state.hasClosing?state.closing12:state.opening12;
        state.previous20=state.hasClosing?state.closing20:state.opening20;
        state.previousValid=true;state.fromOpening=!state.hasClosing;
    }
    snprintf(state.datum,sizeof(state.datum),"%s",date);
    state.hasOpening=state.hasClosing=state.takenOver=false;state.maitreCount=0;
    state.openingAt[0]=state.closingAt[0]=state.openingTerminal[0]=state.closingTerminal[0]=0;
    state.closingNote[0]=0;
}
static void overlay(const DayEvent &e) {
    if(strcmp(e.datum,state.datum))return;
    if(e.kind==DAY_MAITRES) {state.maitreCount=e.maitreCount;memcpy(state.maitreIds,e.maitreIds,sizeof(e.maitreIds));}
    else if(e.kind==DAY_OPENING) {state.hasOpening=true;state.opening12=e.c12;state.opening20=e.c20;state.takenOver=e.takenOver;snprintf(state.openingTerminal,sizeof(state.openingTerminal),"lokal, ausstehend");}
    else {state.hasClosing=true;state.closing12=e.c12;state.closing20=e.c20;snprintf(state.closingTerminal,sizeof(state.closingTerminal),"lokal, ausstehend");memcpy(state.closingNote,e.note,sizeof(e.note));}
}
static bool persist() {
    uint8_t blob[sizeof(DayInventory)+2+MAX_DAY_EVENTS*sizeof(DayEvent)];
    blob[0]=1;blob[1]=count;memcpy(blob+2,&state,sizeof(state));
    memcpy(blob+2+sizeof(state),pending,count*sizeof(DayEvent));
    esp_err_t err=nvs_set_blob(handle,"day_outbox",blob,2+sizeof(state)+count*sizeof(DayEvent));
    return err==ESP_OK&&nvs_commit(handle)==ESP_OK;
}
bool day_inventory_persist() {
    if(!mutex)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);bool ok=persist();xSemaphoreGive(mutex);return ok;
}
void day_inventory_init(nvs_handle_t h) {
    handle=h;if(!mutex)mutex=xSemaphoreCreateMutex();
    uint8_t blob[sizeof(DayInventory)+2+MAX_DAY_EVENTS*sizeof(DayEvent)];size_t length=sizeof(blob);
    if(nvs_get_blob(handle,"day_outbox",blob,&length)==ESP_OK&&length>=2+sizeof(state)&&blob[0]==1&&blob[1]<=MAX_DAY_EVENTS
       &&length==2+sizeof(state)+blob[1]*sizeof(DayEvent)) {
        count=blob[1];memcpy(&state,blob+2,sizeof(state));memcpy(pending,blob+2+sizeof(state),count*sizeof(DayEvent));
        state.datum[10]=state.previousDate[10]=state.backupDate[10]=0;
        state.openingAt[31]=state.closingAt[31]=0;state.openingTerminal[64]=state.closingTerminal[64]=0;
        if(state.maitreCount<0||state.maitreCount>4){state={};count=0;}
        for(int i=0;i<count;i++){pending[i].externalId[39]=pending[i].datum[10]=pending[i].note[240]=0;
            if(pending[i].kind<DAY_MAITRES||pending[i].kind>DAY_CLOSING||pending[i].maitreCount<0||pending[i].maitreCount>4){state={};count=0;break;}}
    }
}
void day_inventory_snapshot(DayInventory *out) {
    if(!mutex){*out={};return;}xSemaphoreTake(mutex,portMAX_DELAY);current_day();*out=state;xSemaphoreGive(mutex);
}
bool day_inventory_queue(DayEventKind kind,int c12,int c20,const int *ids,int n,bool taken,const char *note,const char *expectedDate) {
    if(!mutex||n<0||n>4||c12<0||c20<0||c12>1000000||c20>1000000)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);current_day();
    if(expectedDate&&strcmp(expectedDate,state.datum)){xSemaphoreGive(mutex);return false;}
    if(count>=MAX_DAY_EVENTS||state.datum[0]==0||(kind==DAY_MAITRES&&n<1)||(kind==DAY_OPENING&&state.maitreCount<1)) {xSemaphoreGive(mutex);return false;}
    DayInventory old=state;DayEvent &e=pending[count];e={};e.kind=kind;e.c12=c12;e.c20=c20;e.maitreCount=n;e.takenOver=taken;
    snprintf(e.datum,sizeof(e.datum),"%s",state.datum);copy_text(e.note,sizeof(e.note),note);
    snprintf(e.externalId,sizeof(e.externalId),"%08lx-%08lx-%08lx-%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
    if(n&&ids)memcpy(e.maitreIds,ids,n*sizeof(int));
    count++;overlay(e);bool ok=persist();if(!ok){count--;state=old;}
    xSemaphoreGive(mutex);return ok;
}
bool day_inventory_peek(DayEvent *out) {
    if(!mutex)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);bool ok=count>0;if(ok&&out)*out=pending[0];xSemaphoreGive(mutex);return ok;
}
bool day_inventory_ack(const char *id) {
    xSemaphoreTake(mutex,portMAX_DELAY);if(!count||strcmp(id,pending[0].externalId)){xSemaphoreGive(mutex);return false;}
    DayEvent old=pending[0];memmove(pending,pending+1,(count-1)*sizeof(DayEvent));count--;
    bool ok=persist();if(!ok){memmove(pending+1,pending,count*sizeof(DayEvent));pending[0]=old;count++;}
    xSemaphoreGive(mutex);return ok;
}
static bool counts(cJSON *j,int &a,int &b) {
    cJSON *x=cJSON_GetObjectItem(j,"c12"),*y=cJSON_GetObjectItem(j,"c20");
    if(!cJSON_IsNumber(x)||!cJSON_IsNumber(y)||x->valuedouble<0||y->valuedouble<0||x->valuedouble>1000000||y->valuedouble>1000000)return false;
    a=(int)x->valuedouble;b=(int)y->valuedouble;return x->valuedouble==a&&y->valuedouble==b;
}
static void text(cJSON *j,const char *key,char *out,size_t n) {
    cJSON *s=cJSON_GetObjectItem(j,key);copy_text(out,n,cJSON_IsString(s)?s->valuestring:"");
}
bool day_inventory_apply(cJSON *root,const char *requestedDate) {
    cJSON *today=cJSON_GetObjectItem(root,"today");if(!cJSON_IsObject(today))return false;
    xSemaphoreTake(mutex,portMAX_DELAY);current_day();
    if(requestedDate&&strcmp(requestedDate,state.datum)){xSemaphoreGive(mutex);return false;}
    cJSON *previous=cJSON_GetObjectItem(root,"lastClosing");
    state.previousValid=counts(previous,state.previous12,state.previous20);
    state.fromOpening=cJSON_IsTrue(cJSON_GetObjectItem(previous,"fromOpening"));text(previous,"datum",state.previousDate,sizeof(state.previousDate));
    cJSON *opening=cJSON_GetObjectItem(today,"opening"),*closing=cJSON_GetObjectItem(today,"closing");
    state.hasOpening=counts(opening,state.opening12,state.opening20);state.hasClosing=counts(closing,state.closing12,state.closing20);
    state.takenOver=cJSON_IsTrue(cJSON_GetObjectItem(opening,"takenOver"));
    text(opening,"at",state.openingAt,sizeof(state.openingAt));text(closing,"at",state.closingAt,sizeof(state.closingAt));
    text(opening,"terminal",state.openingTerminal,sizeof(state.openingTerminal));text(closing,"terminal",state.closingTerminal,sizeof(state.closingTerminal));
    text(closing,"note",state.closingNote,sizeof(state.closingNote));
    state.maitreCount=0;cJSON *id;cJSON_ArrayForEach(id,cJSON_GetObjectItem(today,"maitres")) {
        if(state.maitreCount<4&&cJSON_IsNumber(id))state.maitreIds[state.maitreCount++]=(int)id->valuedouble;
    }
    for(int i=0;i<count;i++)overlay(pending[i]);
    xSemaphoreGive(mutex);return true; // RAM publication only; no flash under UI guard.
}
bool day_inventory_mark_backup(const char *date) {
    if(!mutex)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);char old[11];memcpy(old,state.backupDate,11);
    snprintf(state.backupDate,sizeof(state.backupDate),"%s",date);bool ok=persist();if(!ok)memcpy(state.backupDate,old,11);
    xSemaphoreGive(mutex);return ok;
}
bool day_inventory_closed_today(){DayInventory s;day_inventory_snapshot(&s);return s.hasClosing;}
