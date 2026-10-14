#include "game_journal.h"
#include "terminal_logs.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <atomic>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct Record { uint32_t magic,version,bytes,checksum; bool accepted; FinishedGame game; };
static constexpr uint32_t MAGIC=0x47414d45;
static SemaphoreHandle_t s_lock;
static struct {int old_id,new_id;} s_remaps[MAX_PORTAL_SPIELER];
static int s_remap_count;
static std::atomic<bool> s_ready{false},s_has_unloaded{false};
static uint32_t checksum(const FinishedGame &game,bool accepted=false) {
    uint32_t hash=(2166136261u^(accepted?1u:0u))*16777619u;
    const uint8_t *bytes=(const uint8_t *)&game;
    for(size_t i=0;i<sizeof(game);i++) hash=(hash^bytes[i])*16777619u;
    return hash;
}
static bool filename(const char *id,char *out,size_t size) {
    size_t length=strnlen(id,40);
    if(!length || length>=40 || strspn(id,"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-")!=length) return false;
    return snprintf(out,size,"/fatfs/g_%s",id)>0;
}
static bool read_record(const char *file,Record *record);
static bool replace_record(const char *temp,const char *file) {
    char backup[326];snprintf(backup,sizeof(backup),"%.319s.bak",file);
    bool moved=false;
    if(access(file,F_OK)==0) {
        // FatFs does not replace an existing rename destination.
        if(rename(file,backup)!=0) return false;
        moved=true;
    }
    if(rename(temp,file)!=0) {
        if(moved) (void)rename(backup,file);
        return false; // Keep the complete temporary record for reboot recovery.
    }
    if(moved) (void)unlink(backup);
    return true;
}
static bool write_record(const char *file,const Record *record) {
    char temp[326]; snprintf(temp,sizeof(temp),"%.319s.part",file);
    FILE *fp=fopen(temp,"wb"); if(!fp) return false;
    bool ok=fwrite(record,1,sizeof(*record),fp)==sizeof(*record);
    if(ok) ok=fflush(fp)==0 && fsync(fileno(fp))==0;
    if(fclose(fp)!=0) ok=false;
    if(!ok) return false;
    return replace_record(temp,file);
}
static bool read_record(const char *file,Record *record) {
    FILE *fp=fopen(file,"rb"); if(!fp) return false;
    bool ok=fread(record,1,sizeof(*record),fp)==sizeof(*record) && fgetc(fp)==EOF;
    fclose(fp);
    return ok && record->magic==MAGIC && record->version==1 &&
        record->bytes==sizeof(FinishedGame) && record->game.base.externalId[39]==0 &&
        record->game.base.teilnahmen_count>=0 && record->game.base.teilnahmen_count<=MAX_SPIELER &&
        record->game.base.ergebnisse_count>=0 && record->game.base.ergebnisse_count<=MAX_ERGEBNISSE &&
        record->checksum==checksum(record->game,record->accepted);
}
bool game_journal_ready() {
    uint64_t total=0,free_bytes=0;
    return s_ready && !s_has_unloaded && esp_vfs_fat_info("/fatfs",&total,&free_bytes)==ESP_OK &&
        free_bytes>sizeof(Record)*3+65536;
}
bool game_journal_save(const FinishedGame *game) {
    if(!s_lock || !game) return false;
    char file[96]; if(!filename(game->base.externalId,file,sizeof(file))) return false;
    Record *record=(Record *)heap_caps_calloc(1,sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record) return false;
    record->magic=MAGIC;record->version=1;record->bytes=sizeof(FinishedGame);
    record->game=*game;record->checksum=checksum(record->game);
    xSemaphoreTake(s_lock,portMAX_DELAY);
    // A finish may race the disk-first identity remap. Future writes must use
    // the mapped IDs even before the roster's short RAM publication window.
    for(int r=0;r<s_remap_count;r++) {
        for(int i=0;i<record->game.base.teilnahmen_count;i++)
            if(record->game.base.teilnahmen[i].spielerId==s_remaps[r].old_id)
                record->game.base.teilnahmen[i].spielerId=s_remaps[r].new_id;
        for(int i=0;i<record->game.base.ergebnisse_count;i++)
            if(record->game.base.ergebnisse[i].spielerId==s_remaps[r].old_id)
                record->game.base.ergebnisse[i].spielerId=s_remaps[r].new_id;
        for(int i=0;i<record->game.spieler_count;i++)
            if(record->game.spielerIds[i]==s_remaps[r].old_id)
                record->game.spielerIds[i]=s_remaps[r].new_id;
    }
    record->checksum=checksum(record->game,record->accepted);
    bool ok=write_record(file,record);
    xSemaphoreGive(s_lock);heap_caps_free(record);
    if(!ok) {
        s_ready=false;
        terminal_log_add("data_loss_risk","ERROR","Spielarchiv konnte nicht dauerhaft schreiben");
    }
    return ok;
}
bool game_journal_ack(const char *id) {
    if(!s_lock) return false;
    char file[96];if(!filename(id,file,sizeof(file))) return false;
    Record *record=(Record *)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record) return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool ok=read_record(file,record);
    if(ok) {
        record->accepted=true;record->checksum=checksum(record->game,true);
        ok=write_record(file,record);
    }
    xSemaphoreGive(s_lock);heap_caps_free(record);
    return ok;
}
static int scan(unsigned days,bool recover,int old_id=0,int new_id=0) {
    Record *record=(Record *)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record) return -1;
    int queued=0; bool corrupt=false,unloaded=false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    DIR *dir=opendir("/fatfs");
    if(!dir) { xSemaphoreGive(s_lock);heap_caps_free(record);return -1; }
    time_t now=time(nullptr); struct dirent *entry;
    while((entry=readdir(dir))) {
        if(strncmp(entry->d_name,"g_",2)) continue;
        char file[320];snprintf(file,sizeof(file),"/fatfs/%s",entry->d_name);
        if(strstr(entry->d_name,".part")) {
            // A power cut may have happened after fsync but before rename.
            // Recover complete temporary records; preserve incomplete evidence.
            char final_file[320];snprintf(final_file,sizeof(final_file),"%s",file);
            char *suffix=strstr(final_file,".part");*suffix=0;
            char backup[326];snprintf(backup,sizeof(backup),"%s.bak",final_file);
            if(!read_record(file,record)) {
                if(read_record(final_file,record)||read_record(backup,record)) {
                    // An interrupted update still has its previous good copy.
                    (void)unlink(file);continue;
                }
                corrupt=true;continue;
            }
            if(read_record(final_file,record)) (void)unlink(backup);
            if(!replace_record(file,final_file)) {corrupt=true;continue;}
            snprintf(file,sizeof(file),"%s",final_file);
        } else if(strstr(entry->d_name,".bak")) {
            if(!read_record(file,record)) {corrupt=true;continue;}
            char final_file[320];snprintf(final_file,sizeof(final_file),"%s",file);
            *strstr(final_file,".bak")=0;
            char temp[326];snprintf(temp,sizeof(temp),"%s.part",final_file);
            if(read_record(temp,record)) continue; // New complete copy wins.
            if(read_record(final_file,record)) {(void)unlink(file);continue;}
            if(access(final_file,F_OK)==0 && unlink(final_file)!=0) {corrupt=true;continue;}
            if(rename(file,final_file)!=0) {corrupt=true;continue;}
            snprintf(file,sizeof(file),"%s",final_file);
        }
        if(!read_record(file,record)) { corrupt=true;continue; }
        struct tm utc{};time_t finished=0;
        const char *stamp=record->game.finishedAt;
        if(strptime(stamp,"%Y-%m-%dT%H:%M:%S",&utc)) finished=timegm(&utc);
        // Unsynced games never expire; only portal-acknowledged history does.
        if(record->accepted && now>1700000000 && finished>0 && finished<now-7*86400) {
            unlink(file);continue;
        }
        if(!recover && !old_id && g_store.syncStatus!=SYNC_RUNNING) {
            bool in_history=false;
            for(int i=0;i<g_store.historyCount;i++)
                if(!strcmp(g_store.history[i].base.externalId,record->game.base.externalId)) in_history=true;
            if(!in_history) {
                int slot=g_store.historyCount;
                if(slot>=MAX_HISTORY) {
                    slot=0;
                    for(int i=1;i<MAX_HISTORY;i++)
                        if(strcmp(g_store.history[i].finishedAt,g_store.history[slot].finishedAt)<0) slot=i;
                    if(strcmp(record->game.finishedAt,g_store.history[slot].finishedAt)<=0) slot=-1;
                } else ++g_store.historyCount;
                if(slot>=0) g_store.history[slot]=record->game;
            }
            if(!g_store.hasLastFinished || strcmp(record->game.finishedAt,g_store.lastFinished.finishedAt)>0) {
                g_store.lastFinished=record->game;g_store.hasLastFinished=true;
            }
        }
        if(old_id) {
            bool changed=false;
            for(int i=0;i<record->game.base.teilnahmen_count;i++)
                if(record->game.base.teilnahmen[i].spielerId==old_id) {record->game.base.teilnahmen[i].spielerId=new_id;changed=true;}
            for(int i=0;i<record->game.base.ergebnisse_count;i++)
                if(record->game.base.ergebnisse[i].spielerId==old_id) {record->game.base.ergebnisse[i].spielerId=new_id;changed=true;}
            for(int i=0;i<record->game.spieler_count && i<MAX_SPIELER;i++)
                if(record->game.spielerIds[i]==old_id) {record->game.spielerIds[i]=new_id;changed=true;}
            if(changed) {record->checksum=checksum(record->game,record->accepted);if(!write_record(file,record)) corrupt=true;}
            continue;
        }
        if(record->accepted && !recover) continue;
        if(recover && (finished<=0 || now<1700000000 || finished<now-(time_t)days*86400)) continue;
        bool present=false;
        for(int i=0;i<g_store.pendingGamesCount;i++)
            if(!strcmp(g_store.pendingGames[i].externalId,record->game.base.externalId)) present=true;
        if(recover && record->accepted) {
            record->accepted=false;
            record->checksum=checksum(record->game,false);
            if(!write_record(file,record)) {corrupt=true;continue;}
        }
        if(present) continue;
        if(g_store.pendingGamesCount>=MAX_PENDING_GAMES) {unloaded=true;continue;}
        bool commit=g_store.syncStatus==SYNC_RUNNING;
        // Never wait for LVGL while holding the filesystem lock: finishing a
        // game on LVGL may need that same lock to save its immutable record.
        if(commit) xSemaphoreGive(s_lock);
        bool acquired=!commit || store_sync_commit_begin();
        if(!acquired) {xSemaphoreTake(s_lock,portMAX_DELAY);unloaded=true;break;}
        present=false;
        for(int i=0;i<g_store.pendingGamesCount;i++)
            if(!strcmp(g_store.pendingGames[i].externalId,record->game.base.externalId)) present=true;
        if(!present && g_store.pendingGamesCount<MAX_PENDING_GAMES) {
            g_store.pendingGames[g_store.pendingGamesCount++]=record->game.base;++queued;
        }
        if(commit) {store_sync_commit_end();xSemaphoreTake(s_lock,portMAX_DELAY);}
    }
    closedir(dir);xSemaphoreGive(s_lock);heap_caps_free(record);
    s_has_unloaded=unloaded;
    if(corrupt) {
        s_ready=false;
        terminal_log_add("data_loss_risk","ERROR","Spielarchiv beschädigt; neue Spiele gesperrt");
        return -1;
    }
    return queued;
}
bool game_journal_init() {
    s_lock=xSemaphoreCreateMutex();
    if(!s_lock || !g_store.offlineCacheHealthy) return false;
    nvs_handle_t n;
    if(nvs_open("tm_cache",NVS_READWRITE,&n)!=ESP_OK) return false;
    bool protected_volume=nvs_set_u8(n,"game_journal",1)==ESP_OK && nvs_commit(n)==ESP_OK;
    nvs_close(n);
    if(!protected_volume) return false;
    s_ready=true;
    return scan(0,false)>=0;
}
int game_journal_recover(unsigned days) {
    if(!s_ready) return -1;
    return scan(days,true);
}
bool game_journal_remap(int old_id,int new_id) {
    if(!s_ready || !s_lock || !old_id || !new_id || old_id==new_id) return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool recorded=false;
    for(int i=0;i<s_remap_count;i++) if(s_remaps[i].old_id==old_id) {
        if(s_remaps[i].new_id!=new_id) {xSemaphoreGive(s_lock);return false;}
        recorded=true;break;
    }
    if(!recorded && s_remap_count<MAX_PORTAL_SPIELER) {
        s_remaps[s_remap_count++]={old_id,new_id};recorded=true;
    }
    xSemaphoreGive(s_lock);
    return recorded && scan(0,false,old_id,new_id)>=0;
}
int game_journal_reload_pending() {return s_ready?scan(0,false):-1;}
int game_journal_manifest(const char *day,char (*ids)[40],int capacity) {
    if(!s_ready) return -1;
    Record *record=(Record *)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record) return -1;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    DIR *dir=opendir("/fatfs");int count=0;struct dirent *entry;
    if(dir) {
        while((entry=readdir(dir)) && count<capacity) {
            if(strncmp(entry->d_name,"g_",2) || strstr(entry->d_name,".part")) continue;
            char file[320];snprintf(file,sizeof(file),"/fatfs/%s",entry->d_name);
            if(read_record(file,record) && !strcmp(record->game.base.datum,day))
                snprintf(ids[count++],40,"%.39s",record->game.base.externalId);
        }
        closedir(dir);
    }
    xSemaphoreGive(s_lock);heap_caps_free(record);
    return dir?count:-1;
}
