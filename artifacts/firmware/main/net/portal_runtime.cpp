#include "portal_runtime.h"
#include "portal_storage.h"
#include "portal_queue_json.h"
#include "http_sync.h"
#include "game_store.h"
#include "game_journal.h"
#include "ui_manager.h"
#include "display_text.h"
#include "terminal_logs.h"
#include "coprocessor.h"
#include "lora_stub.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <atomic>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
static PortalQueueRecord record;
static int slot=-1;
static bool durable=false;
static PortalQueueSnapshot cache;
static SemaphoreHandle_t stateLock,cacheLock;
static std::atomic<bool> refresh{true},taking{false};
static std::atomic<unsigned> appliedGeneration{0};
static PortalQueueGame requested;
static bool requestedMerge;
static std::atomic<bool> requestPending{false};
static std::atomic<uint64_t> liveVersion{0};
static std::atomic<int> liveState{0};
static char abortedId[40];
static int abortedClays,abortedMode;
static char statusText[128]="";
static const char *modes[]={"NORMAL","HARAKIRI","CUSTOM_1","CUSTOM_2","CUSTOM_3","CUSTOM_4","HARAKIRI_CUSTOM"};
static void status(const char *text) {
 if(!stateLock)return;
 xSemaphoreTake(stateLock,portMAX_DELAY);snprintf(statusText,sizeof(statusText),"%.127s",text);xSemaphoreGive(stateLock);
}
static void filePath(int s,char path[48]){snprintf(path,48,"/fatfs/queue%d.bin",s);}
// Caller owns stateLock. Only the older slot is ever rewritten.
static bool save(PortalQueueRecord next) {
 if(!durable)return false;
 next.magic=0x51554555;next.version=1;next.generation=record.generation+1;next.checksum=portal_queue_checksum(next);
 int target=slot==0?1:0;char path[48];filePath(target,path);
 game_journal_storage_lock();
 bool ok=portal_queue_write_file(path,next);
 game_journal_storage_unlock();
 if(ok){record=next;slot=target;}
 return ok;
}
bool portal_queue_reserved() {
 if(!stateLock)return false;
 if(xSemaphoreTake(stateLock,0)!=pdTRUE)return false;
 bool active=record.reserved.id!=0;xSemaphoreGive(stateLock);return active;
}
bool portal_queue_release() {
 if(!stateLock)return true;
 if(xSemaphoreTake(stateLock,0)!=pdTRUE)return false;
 bool ok=true;
 if(record.reserved.id) {
  if(record.count==8)ok=false;
  else {auto next=record;next.events[next.count++]={record.reserved.id,2,{0}};next.reserved={};ok=save(next);}
 }
 xSemaphoreGive(stateLock);return ok;
}
bool portal_queue_game_started() {
 if(!stateLock)return true;
 if(xSemaphoreTake(stateLock,0)!=pdTRUE)return false;
 bool ok=true;
 if(record.reserved.id) {
  if(record.count==8||!g_store.spielId[0]||(int)g_store.modus!=record.reserved.mode)ok=false;
  for(int i=0;i<record.reserved.count&&ok;i++) {
   bool included=false;for(int p=0;p<g_store.spielerCount;p++)if(g_store.spieler[p].id==record.reserved.players[i].id)included=true;
   if(!included)ok=false;
  }
  if(ok) {
   auto next=record;auto &event=next.events[next.count++];event={};event.queueId=record.reserved.id;event.kind=1;
   snprintf(event.externalId,sizeof(event.externalId),"%.39s",g_store.spielId);next.reserved={};ok=save(next);
  }
 }
 xSemaphoreGive(stateLock);return ok;
}
void portal_queue_refresh(){refresh=true;}
bool portal_queue_snapshot(PortalQueueSnapshot *out) {
 if(!cacheLock||!out||xSemaphoreTake(cacheLock,0)!=pdTRUE)return false;
 *out=cache;xSemaphoreGive(cacheLock);return true;
}
void portal_queue_status(char *out,size_t size) {
 if(!out||!size)return;
 out[0]=0;
 if(stateLock&&xSemaphoreTake(stateLock,0)==pdTRUE){snprintf(out,size,"%s",statusText);xSemaphoreGive(stateLock);}
}
bool portal_queue_take(const PortalQueueGame *game,bool merge) {
 if(!game||!cop_wifi_is_connected()||taking.exchange(true))return false;
 bool allowed=false;
 if(stateLock&&xSemaphoreTake(stateLock,0)==pdTRUE) {
  allowed=durable&&record.count<8&&(!record.reserved.id||record.reserved.id==game->id);xSemaphoreGive(stateLock);
 }
 if(!allowed){taking=false;status("Reservierte Warteschlange zuerst freigeben.");return false;}
 status("Warteschlange wird übernommen …");
 requested=*game;requestedMerge=merge;requestPending=true;return true;
}
bool portal_queue_busy(){return taking.load();}
unsigned portal_queue_generation(){return appliedGeneration.load();}
bool portal_queue_current(PortalQueueGame *out) {
 if(!out||!stateLock||xSemaphoreTake(stateLock,0)!=pdTRUE)return false;
 *out=record.reserved;xSemaphoreGive(stateLock);return out->id>0;
}
static cJSON *command(int queueId) {
 char id[40];if(!http_terminal_id(id,sizeof(id)))return nullptr;
 cJSON *root=cJSON_CreateObject();cJSON_AddNumberToObject(root,"queueGameId",queueId);cJSON_AddStringToObject(root,"terminalId",id);return root;
}
static bool send(const char *path,cJSON *body,char *response,size_t size) {
 if(!body)return false;
 char *json=cJSON_PrintUnformatted(body);cJSON_Delete(body);
 if(!json)return false;
 bool ok=http_optional_json(path,json,response,size)==ESP_OK;cJSON_free(json);return ok;
}
static void fetchQueue(char *response,size_t size) {
 char id[40],path[96];
 if(!http_terminal_id(id,sizeof(id)))return;
 snprintf(path,sizeof(path),"/api/sync/queue?terminalId=%.39s",id);
 if(http_optional_json(path,nullptr,response,size)!=ESP_OK){status("Warteschlange nicht verfügbar");return;}
 PortalQueueSnapshot next={};
 if(!portal_queue_parse(response,&next)){status("Ungültige Warteschlangendaten; Aufstellung bleibt unverändert.");return;}
 xSemaphoreTake(cacheLock,portMAX_DELAY);if(strcmp(cache.version,next.version))cache=next;xSemaphoreGive(cacheLock);
}
static void takeQueue(char *response,size_t size) {
 PortalQueueGame game=requested;bool merge=requestedMerge;int lineup[6]={},previous[6]={};int previousMode=-1;bool valid=false;
 if(ui_manager_lock(0)) {
  valid=portal_queue_merge(g_store.lineupIds,game,merge,lineup);
  memcpy(previous,g_store.lineupIds,sizeof(previous));previousMode=(int)g_store.modus;
  valid=valid&&g_store.screen!=SCREEN_SPIEL&&!g_store.harakiriOrdersReady;
  for(int i=0;i<game.count&&valid;i++) {
   bool found=false;
   for(int p=0;p<g_store.portalSpielerCount;p++) {
    if(g_store.portalSpieler[p].id==game.players[i].id&&g_store.portalSpieler[p].aktiv)found=true;
   }
   if(!found)valid=false;
  }
  ui_manager_unlock();
 }
 if(!valid){status("Spieler noch nicht synchronisiert oder mehr als 6 Spieler.");return;}
 cJSON *root=command(game.id);if(!root)return;
 cJSON *ids=cJSON_AddArrayToObject(root,"entryIds");for(int i=0;i<game.count;i++)cJSON_AddItemToArray(ids,cJSON_CreateNumber(game.players[i].entryId));
 if(!send("/api/sync/queue/take",root,response,size)){status("Warteschlange nicht verfügbar");refresh=true;return;}
 xSemaphoreTake(stateLock,portMAX_DELAY);
 auto next=record;next.reserved=game;bool saved=save(next);xSemaphoreGive(stateLock);
 if(!saved){
  // No local change is allowed without durable ownership. Best-effort release;
  // if the response is lost, the server's 15-minute lease still recovers it.
  (void)send("/api/sync/queue/release",command(game.id),response,size);
  status("Reservierung nicht gespeichert. Aufstellung unverändert.");refresh=true;return;
 }
 if(!ui_manager_lock(50)){status("Reserviert. Bitte erneut öffnen oder freigeben.");return;}
 // Never overwrite a lineup changed while the request was in flight.
 int finalLineup[6]={};valid=g_store.screen!=SCREEN_SPIEL&&!g_store.harakiriOrdersReady&&
   !memcmp(previous,g_store.lineupIds,sizeof(previous))&&previousMode==(int)g_store.modus&&portal_queue_merge(g_store.lineupIds,game,merge,finalLineup);
 if(valid){memcpy(g_store.lineupIds,finalLineup,sizeof(finalLineup));g_store.modus=(Modus)game.mode;game_store_save();g_store.screen=SCREEN_START;appliedGeneration++;}
 ui_manager_unlock();
 if(!valid){(void)portal_queue_release();status("Mehr als 6 Spieler. Aufstellung bleibt unverändert.");}
 else status("Warteschlange übernommen");
 refresh=true;
}
struct LiveCopy {
 uint64_t seq;int state,mode,run,clays,currentId,currentClay,count,resultCount;
 int64_t started;char externalId[40];Spieler players[MAX_SPIELER];Ergebnis results[MAX_ERGEBNISSE];
};
void portal_live_changed(int state) {
 if(state==3) {
  memcpy(abortedId,g_store.spielId,sizeof(abortedId));abortedClays=g_store.sequenzLen;abortedMode=(int)g_store.modus;
 }
 liveState=state;++g_store.liveSequence;liveVersion.fetch_add(1);
}
static void liveJson(const LiveCopy &s,char *buffer,size_t capacity) {
 buffer[0]=0;char id[40],date[32];if(!http_terminal_id(id,sizeof(id)))return;
 time_t stamp=(time_t)s.started;struct tm utc;gmtime_r(&stamp,&utc);strftime(date,sizeof(date),"%Y-%m-%dT%H:%M:%S.000Z",&utc);
 cJSON *root=cJSON_CreateObject();cJSON_AddStringToObject(root,"terminalId",id);
 cJSON_AddNumberToObject(root,"seq",(double)s.seq);cJSON_AddStringToObject(root,"state",s.state==1?"RUNNING":s.state==2?"FINISHED":"ABORTED");
 cJSON_AddStringToObject(root,"spielExternalId",s.externalId);cJSON_AddStringToObject(root,"modus",modes[s.mode]);
 cJSON_AddNumberToObject(root,"lauf",s.run);cJSON_AddNumberToObject(root,"taubenProLauf",s.clays);cJSON_AddStringToObject(root,"startedAt",date);
 cJSON_AddNumberToObject(root,"currentSpielerId",s.currentId);cJSON_AddNumberToObject(root,"currentTaube",s.currentClay);
 auto *players=cJSON_AddArrayToObject(root,"players");
 for(int i=0;i<s.count;i++){const auto &p=s.players[i];auto *row=cJSON_CreateObject();cJSON_AddNumberToObject(row,"spielerId",p.id);
  cJSON_AddStringToObject(row,"name",p.name);cJSON_AddNumberToObject(row,"posten",p.startPosten);cJSON_AddItemToArray(players,row);}
 auto *results=cJSON_AddArrayToObject(root,"results");
 for(int i=0;i<s.resultCount;i++){const auto &r=s.results[i];if(r.wiederholt)continue;auto *row=cJSON_CreateObject();char machine[2]={(char)('A'+r.maschine),0};
  cJSON_AddNumberToObject(row,"spielerId",r.spielerId);cJSON_AddNumberToObject(row,"lauf",r.lauf);cJSON_AddNumberToObject(row,"taube",r.taube);
  cJSON_AddStringToObject(row,"maschine",machine);cJSON_AddBoolToObject(row,"schuss1",r.schuss1);cJSON_AddBoolToObject(row,"schuss2",r.schuss2);
  cJSON_AddNumberToObject(row,"punkte",r.punkte);cJSON_AddItemToArray(results,row);}
 if(!cJSON_PrintPreallocated(root,buffer,(int)capacity,false))buffer[0]=0;
 cJSON_Delete(root);
}
static void worker(void*) {
 char *response=(char*)heap_caps_malloc(16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 char *body=(char*)heap_caps_malloc(65536,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 auto *snapshot=(LiveCopy*)heap_caps_malloc(sizeof(LiveCopy),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!response||!body||!snapshot){
  status("Portal-Zusatzkanal: nicht genug Speicher");
  heap_caps_free(response);heap_caps_free(body);heap_caps_free(snapshot);vTaskDelete(nullptr);return;
 }
 PortalLiveGate gate;int64_t lastOutbox=-5000000,lastLog=0;
 unsigned sent=0,failed=0,skipped=0;
 for(;;) {
  vTaskDelay(pdMS_TO_TICKS(100));
  if(!cop_wifi_is_connected()||!http_optional_ready()||lora_request_busy()){skipped++;continue;}
  if(requestPending.exchange(false)){takeQueue(response,16384);taking=false;}
  if(refresh.exchange(false))fetchQueue(body,65536);
  int64_t now=esp_timer_get_time();
  if(now-lastOutbox>=5000000) {
   lastOutbox=now;PortalQueueEvent event={};
   xSemaphoreTake(stateLock,portMAX_DELAY);if(record.count)event=record.events[0];xSemaphoreGive(stateLock);
   if(event.queueId) {
    cJSON *root=command(event.queueId);
    if(root&&event.kind==1)cJSON_AddStringToObject(root,"spielExternalId",event.externalId);
    if(send(event.kind==1?"/api/sync/queue/started":"/api/sync/queue/release",root,response,16384)) {
     xSemaphoreTake(stateLock,portMAX_DELAY);auto next=record;
     memmove(next.events,next.events+1,(next.count-1)*sizeof(next.events[0]));next.count--;(void)save(next);xSemaphoreGive(stateLock);refresh=true;
    }
   }
  }
  now=esp_timer_get_time();
  gate.version=liveVersion.load();
  if(gate.ready(now,true,false,false)&&ui_manager_lock(0)) {
   gate.version=liveVersion.load();
   const auto &g=g_store;*snapshot={};snapshot->seq=g.liveSequence;snapshot->state=liveState.load();
   snapshot->mode=(int)g.modus;snapshot->run=g.lauf;snapshot->clays=g.sequenzLen;snapshot->started=g.liveStartedAt;
   snapshot->count=g.spielerCount;snapshot->resultCount=g.ergebnisseCount;
   snapshot->currentId=g.spielerIndex<g.spielerCount?g.spieler[g.spielerIndex].id:0;snapshot->currentClay=g.taubeIndex+1;
   memcpy(snapshot->externalId,g.spielId,sizeof(snapshot->externalId));
   memcpy(snapshot->players,g.spieler,sizeof(snapshot->players));memcpy(snapshot->results,g.ergebnisse,sizeof(snapshot->results));
   if(snapshot->state==2&&g.hasLastFinished) {
    const auto &f=g.lastFinished;snapshot->count=f.spieler_count;snapshot->mode=(int)f.base.modus;
    snapshot->clays=f.base.taubenProLauf;snapshot->run=f.base.lauf;snapshot->resultCount=f.base.ergebnisse_count;
    memcpy(snapshot->externalId,f.base.externalId,sizeof(snapshot->externalId));
    memcpy(snapshot->results,f.base.ergebnisse,sizeof(snapshot->results));
    for(int p=0;p<f.spieler_count&&p<MAX_SPIELER;p++) {
     snapshot->players[p]={};snapshot->players[p].id=f.spielerIds[p];snapshot->players[p].startPosten=p+1;
     memcpy(snapshot->players[p].name,f.spielerNamen[p],MAX_NAME_LEN);
     for(int t=0;t<f.base.teilnahmen_count;t++)if(f.base.teilnahmen[t].spielerId==f.spielerIds[p])
      snapshot->players[p].startPosten=f.base.teilnahmen[t].startPosten;
    }
    snapshot->currentId=0;snapshot->currentClay=0;
   }
   if(snapshot->state==3) {
    memcpy(snapshot->externalId,abortedId,sizeof(abortedId));snapshot->clays=abortedClays;snapshot->mode=abortedMode;
    snapshot->count=0;snapshot->resultCount=0;snapshot->currentId=0;snapshot->currentClay=0;
   }
   ui_manager_unlock();gate.claim(now);
   // Completed/aborted snapshots are display-only; empty aborted games can just expire.
   if((snapshot->count>0||snapshot->state==3)&&snapshot->externalId[0]&&snapshot->clays>0&&snapshot->count<=6&&snapshot->resultCount<=MAX_ERGEBNISSE&&snapshot->mode>=0&&snapshot->mode<7) {
    liveJson(*snapshot,body,65536);
    if(body[0]&&http_optional_ready()&&!lora_request_busy()&&http_optional_json("/api/sync/live",body,response,16384)==ESP_OK)sent++;else failed++;
   }
  }
  if(now-lastLog>=3600000000LL){lastLog=now;char text[120];snprintf(text,sizeof(text),"Live-Kanal: gesendet=%u fehlgeschlagen=%u übersprungen=%u",sent,failed,skipped);terminal_log_add("live","INFO",text);}
 }
}
void portal_runtime_init() {
 stateLock=xSemaphoreCreateMutex();cacheLock=xSemaphoreCreateMutex();
 if(!stateLock||!cacheLock)return;
 bool present=false;game_journal_storage_lock();
 for(int s=0;s<2;s++) {
  char path[48];filePath(s,path);FILE *f=fopen(path,"rb");if(!f)continue;present=true;
  fclose(f);PortalQueueRecord candidate={};bool valid=portal_queue_read_file(path,&candidate);
  if(valid&&(slot<0||candidate.generation>record.generation)){record=candidate;slot=s;}
 }
 game_journal_storage_unlock();durable=slot>=0||!present;
 if(!durable)status("Warteschlangenarchiv beschädigt; manuelle Aufstellung verwenden.");
 if(g_store.harakiriOrdersReady) {
  if(record.reserved.id)(void)portal_queue_game_started();
  portal_live_changed(1);
 }
 if(xTaskCreatePinnedToCore(worker,"portal_optional",12288,nullptr,1,nullptr,0)!=pdPASS)
  status("Portal-Zusatzkanal konnte nicht gestartet werden.");
}
