#include "game_journal.h"
#include "terminal_logs.h"
#include "offline_cache.h"
#include "storage_budget.h"
#include "active_game.h"
#include "storage_write.h"
#include "hourly_log_gate.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <atomic>
#include <initializer_list>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct Record { uint32_t magic,version,bytes,checksum; bool accepted; FinishedGame game; };
static_assert(storage_allocated(sizeof(Record)) * (STORAGE_JOURNAL_SLOTS + 2) <
              9842688u * STORAGE_JOURNAL_PERCENT / 100 - 2*12288u,
              "Journal plus replacement scratch must fit in half of usable FAT");
static constexpr uint32_t MAGIC=0x47414d45;
static SemaphoreHandle_t s_lock;
static struct {int old_id,new_id;} s_remaps[MAX_PORTAL_SPIELER];
static int s_remap_count;
static std::atomic<bool> s_ready{false},s_has_unloaded{false};
static bool s_protected;
static int64_t s_retry_at;
static unsigned s_retry_attempt;
// Called under the journal lock. Distinct failing stages each retain their first
// diagnostic, without flooding diagnostics/NVS on repeated syncs.
static void journal_failure(const char *step,const char *file,int error) {
    static const char *steps[]={"allocation","trim/read_record","trim/opendir","trim/readdir",
        "trim/unlink","trim/capacity","esp_vfs_fat_info","free-space threshold","fopen(.part)",
        "fwrite","fflush","fsync","fclose","rename/backup","rename/commit","NVS fallback"};
    static HourlyLogGate gates[sizeof(steps)/sizeof(*steps)];
    unsigned slot=0;
    for(unsigned i=0;i<sizeof(steps)/sizeof(*steps);i++)if(!strcmp(step,steps[i]))slot=i;
    if(!gates[slot].admit(esp_timer_get_time()))return;
    uint64_t total=0,available=0;
    esp_err_t info=esp_vfs_fat_info("/fatfs",&total,&available);
    char message[201];
    snprintf(message,sizeof(message),
        "Spielarchiv: %s errno=%d frei=%llu Grenze=%llu info=%d Datei=%.48s",
        step,error,(unsigned long long)available,
        (unsigned long long)(total*STORAGE_FREE_PERCENT/100+storage_allocated(sizeof(Record))),
        (int)info,file?file:"-");
    ESP_LOGE("game_journal","%s",message);
    terminal_log_add("data_loss_risk","ERROR",message);
}
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
static int fallback_locked(Record *record);
void game_journal_storage_lock() {
    // First called during single-threaded storage initialization, before workers.
    if(!s_lock) s_lock=xSemaphoreCreateMutex();
    configASSERT(s_lock);
    xSemaphoreTake(s_lock,portMAX_DELAY);
}
void game_journal_storage_unlock() { xSemaphoreGive(s_lock); }
size_t game_journal_record_size() { return sizeof(Record); }
bool game_journal_inspect_locked(GameJournalInspection *out) {
    *out={};
    Record *record=nullptr;
    DIR *dir=opendir("/fatfs");
    if(!dir) {heap_caps_free(record);out->uncertain=1;return false;}
    for(;;) {
        errno=0;struct dirent *entry=readdir(dir);
        if(!entry) {if(errno) ++out->uncertain;break;}
        if(strncasecmp(entry->d_name,"g_",2)) continue;
        if(!record)record=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!record){++out->uncertain;continue;}
        char file[320];int n=snprintf(file,sizeof(file),"/fatfs/%s",entry->d_name);
        if(n<0 || (size_t)n>=sizeof(file) || !read_record(file,record)) {++out->uncertain;continue;}
        out->bytes+=sizeof(Record);
        if(record->accepted) ++out->accepted;else ++out->pending;
    }
    if(closedir(dir)!=0) ++out->uncertain;
    // The emergency NVS slot is durable pending work too. Do not let a boot
    // repair run before game_journal_init has loaded it into the RAM queue.
    int fallback=fallback_locked(nullptr);
    if(fallback==1) {
        if(!record)record=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(record && fallback_locked(record)==1)++out->pending;
        else ++out->uncertain;
    } else if(fallback<0)++out->uncertain;
    heap_caps_free(record);
    return out->uncertain==0;
}
static bool replace_record(const char *temp,const char *file) {
    char backup[326];snprintf(backup,sizeof(backup),"%.319s.bak",file);
    bool moved=false;
    if(access(file,F_OK)==0) {
        // FatFs does not replace an existing rename destination.
        if(rename(file,backup)!=0) {journal_failure("rename/backup",file,errno);return false;}
        moved=true;
    }
    if(rename(temp,file)!=0) {
        journal_failure("rename/commit",file,errno);
        if(moved) (void)rename(backup,file);
        return false; // Keep the complete temporary record for reboot recovery.
    }
    if(moved) (void)unlink(backup);
    return true;
}
static bool write_record(const char *file,const Record *record) {
    char temp[326]; snprintf(temp,sizeof(temp),"%.319s.part",file);
    FILE *fp=fopen(temp,"wb");
    if(!fp) {journal_failure("fopen(.part)",file,errno);return false;}
    bool complete=false;
    StorageWriteFailure failure{};
    bool ok=storage_write_committed(fp,record,sizeof(*record),&complete,&failure);
    if(fclose(fp)!=0 && ok) {ok=false;failure={"fclose",errno?errno:EIO};}
    if(!ok) {
        journal_failure(failure.step?failure.step:"fclose",file,failure.error);
        // Remove only our incomplete failed write. A complete checksummed
        // unsynced record must survive a failed fsync/close for recovery.
        if(!complete) (void)unlink(temp);
        return false;
    }
    return replace_record(temp,file);
}
static bool valid_record(const Record *record) {
    return record->magic==MAGIC && record->version==1 &&
        record->bytes==sizeof(FinishedGame) && record->game.base.externalId[39]==0 &&
        record->game.spieler_count>=0 && record->game.spieler_count<=MAX_SPIELER &&
        record->game.base.teilnahmen_count>=0 && record->game.base.teilnahmen_count<=MAX_SPIELER &&
        record->game.base.ergebnisse_count>=0 && record->game.base.ergebnisse_count<=MAX_ERGEBNISSE &&
        record->checksum==checksum(record->game,record->accepted);
}
static bool read_record(const char *file,Record *record) {
    errno=0;
    FILE *fp=fopen(file,"rb"); if(!fp) return false;
    bool ok=fread(record,1,sizeof(*record),fp)==sizeof(*record) && fgetc(fp)==EOF && !ferror(fp);
    int error=errno;
    if(fclose(fp)!=0) {ok=false;error=errno;}
    if(!ok) {errno=error?error:EILSEQ;return false;}
    if(!valid_record(record)) {errno=EILSEQ;return false;}
    return true;
}
// One emergency finished game, never a second unbounded NVS game journal.
// A failed finish blocks new starts until FAT recovers or this ID is portal-ACKed.
static int fallback_locked(Record *record) {
    nvs_handle_t n;
    if(nvs_open("tm_cache",NVS_READWRITE,&n)!=ESP_OK)return -1;
    size_t bytes=record?sizeof(*record):0;
    esp_err_t err=nvs_get_blob(n,"finish_pending",record,&bytes);
    nvs_close(n);
    if(err==ESP_ERR_NVS_NOT_FOUND)return 0;
    return err==ESP_OK && bytes==sizeof(Record) && (!record || valid_record(record))?1:-1;
}
static bool clear_fallback_locked(const char *id,Record *record) {
    int state=fallback_locked(record);
    if(state<0)return false;
    if(!state || strcmp(record->game.base.externalId,id))return true;
    nvs_handle_t n;
    if(nvs_open("tm_cache",NVS_READWRITE,&n)!=ESP_OK)return false;
    bool ok=nvs_erase_key(n,"finish_pending")==ESP_OK && nvs_commit(n)==ESP_OK;
    nvs_close(n);return ok;
}
bool game_journal_ready() {
    uint64_t total=0,free_bytes=0;
    return s_ready && !s_has_unloaded && offline_cache_storage_writable() && !offline_cache_cleanup_busy() &&
        esp_vfs_fat_info("/fatfs",&total,&free_bytes)==ESP_OK &&
        free_bytes>=total*STORAGE_FREE_PERCENT/100+storage_allocated(sizeof(Record))*2;
}
int game_journal_contains(const char *id) {
    char file[96];if(!filename(id,file,sizeof(file)))return -1;
    struct stat st{};
    if(stat(file,&st)!=0)return errno==ENOENT?0:-1;
    auto *record=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record)return -1;
    game_journal_storage_lock();
    bool found=read_record(file,record);
    game_journal_storage_unlock();heap_caps_free(record);return found?1:-1;
}
// Bound the accepted archive even when the clock is unset or a busy day has
// thousands of games. Never evict pending, corrupt or temporary journal data.
static bool trim_archive_locked(unsigned reserve) {
    struct Kept {char path[96],stamp[32];} kept[STORAGE_ARCHIVE_SLOTS]{};
    unsigned count=0,total=0;
    Record *record=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    DIR *dir=record?opendir("/fatfs"):nullptr;
    if(!dir) {journal_failure(record?"trim/opendir":"allocation",nullptr,record?errno:ENOMEM);heap_caps_free(record);return false;}
    bool ok=true;
    for(;;) {
        errno=0;struct dirent *entry=readdir(dir);
        if(!entry) {if(errno){journal_failure("trim/readdir",nullptr,errno);ok=false;}break;}
        if(strncmp(entry->d_name,"g_",2) || strchr(entry->d_name,'.')) continue;
        char path[96];int n=snprintf(path,sizeof(path),"/fatfs/%s",entry->d_name);
        ++total;
        if(n<0 || (size_t)n>=sizeof(path) || !read_record(path,record)) {
            // Preserve/count uncertain evidence, but do not prevent a different
            // finished game being durably saved. Readiness proof still fails
            // closed on this entry; it can never be evicted as accepted history.
            journal_failure("trim/read_record",path,errno?errno:EILSEQ);
            s_ready=false;continue;
        }
        if(!record->accepted) continue;
        unsigned oldest=0;
        for(unsigned i=1;i<count;i++) if(strcmp(kept[i].stamp,kept[oldest].stamp)<0) oldest=i;
        if(count==STORAGE_ARCHIVE_SLOTS) {
            if(strcmp(record->game.finishedAt,kept[oldest].stamp)<=0) {
                if(unlink(path)==0) --total;else {journal_failure("trim/unlink",path,errno);ok=false;}
                continue;
            }
            if(unlink(kept[oldest].path)!=0) {journal_failure("trim/unlink",kept[oldest].path,errno);ok=false;continue;}
            --total;
        } else oldest=count++;
        snprintf(kept[oldest].path,sizeof(kept[oldest].path),"%s",path);
        snprintf(kept[oldest].stamp,sizeof(kept[oldest].stamp),"%.31s",record->game.finishedAt);
    }
    closedir(dir);heap_caps_free(record);
    while(total+reserve>STORAGE_JOURNAL_SLOTS && count) {
        unsigned oldest=0;
        for(unsigned i=1;i<count;i++) if(strcmp(kept[i].stamp,kept[oldest].stamp)<0) oldest=i;
        if(unlink(kept[oldest].path)!=0) {journal_failure("trim/unlink",kept[oldest].path,errno);ok=false;break;}
        --total;kept[oldest]=kept[--count];
    }
    if(total+reserve>STORAGE_JOURNAL_SLOTS)journal_failure("trim/capacity",nullptr,ENOSPC);
    return ok && total+reserve<=STORAGE_JOURNAL_SLOTS;
}
static bool save_record_locked(const char *file,const Record *record) {
    uint64_t total=0,free_bytes=0;
    if(!trim_archive_locked(access(file,F_OK)==0?0:1))return false;
    if(esp_vfs_fat_info("/fatfs",&total,&free_bytes)!=ESP_OK) {
        journal_failure("esp_vfs_fat_info",file,errno);return false;
    }
    if(free_bytes<total*STORAGE_FREE_PERCENT/100+storage_allocated(sizeof(Record))) {
        journal_failure("free-space threshold",file,ENOSPC);return false;
    }
    return write_record(file,record);
}
bool game_journal_save(const FinishedGame *game) {
    if(!s_lock || !game) return false;
    char file[96]; if(!filename(game->base.externalId,file,sizeof(file))) return false;
    Record *record=(Record *)heap_caps_calloc(1,sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record) {
        game_journal_storage_lock();journal_failure("allocation",file,ENOMEM);game_journal_storage_unlock();
        return false;
    }
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
    bool ok=save_record_locked(file,record);
    xSemaphoreGive(s_lock);heap_caps_free(record);
    if(!ok) {
        s_ready=false;
    }
    if(ok && !s_ready)(void)game_journal_recheck_ready();
    return ok;
}
bool game_journal_hold_pending(const FinishedGame *game) {
    if(!game || !s_lock)return false;
    auto *record=(Record*)heap_caps_calloc(1,sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record)return false;
    game_journal_storage_lock();
    int previous=fallback_locked(record);
    bool ok=previous>=0 && (!previous || !strcmp(record->game.base.externalId,game->base.externalId));
    if(ok) {
        *record={};record->magic=MAGIC;record->version=1;record->bytes=sizeof(FinishedGame);
        record->game=*game;record->checksum=checksum(record->game);
        nvs_handle_t n;
        ok=nvs_open("tm_cache",NVS_READWRITE,&n)==ESP_OK;
        if(ok) {
            ok=nvs_set_blob(n,"finish_pending",record,sizeof(*record))==ESP_OK && nvs_commit(n)==ESP_OK;
            nvs_close(n);
        }
    }
    if(!ok)journal_failure("NVS fallback",game->base.externalId,EIO);
    s_ready=false;s_retry_attempt=0;s_retry_at=esp_timer_get_time()+1000000;
    game_journal_storage_unlock();heap_caps_free(record);return ok;
}
bool game_journal_ack(const char *id) {
    if(!s_lock) return false;
    char file[96];if(!filename(id,file,sizeof(file))) return false;
    Record *record=(Record *)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record) return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    struct stat st{};
    bool missing=stat(file,&st)!=0 && errno==ENOENT;
    bool ok=missing || read_record(file,record);
    if(ok && !missing) {
        record->accepted=true;record->checksum=checksum(record->game,true);
        ok=write_record(file,record);
        if(!ok) {
            uint64_t total=0,free_bytes=0;
            // The portal has explicitly acknowledged THIS external ID. If
            // ENOSPC prevents an archive rewrite, removing only that accepted
            // record is safe and lets a previously full journal drain.
            if(esp_vfs_fat_info("/fatfs",&total,&free_bytes)==ESP_OK &&
               free_bytes<storage_allocated(sizeof(Record))*2)
                ok=unlink(file)==0;
            if(ok) terminal_log_add("data_loss_risk","WARN","Portal-bestätigtes Spiel bei Platzmangel nicht im lokalen Archiv behalten");
        }
        if(ok) (void)trim_archive_locked(0);
    }
    if(ok) {
        // Portal acceptance is authoritative for THIS ID. A missing canonical
        // record is not an ACK failure. Remove only its acknowledged recovery
        // siblings so a failed fsync/rename cannot resurrect it on the next scan.
        char sibling[104];
        for(const char *suffix:{".part",".bak"}) {
            snprintf(sibling,sizeof(sibling),"%s%s",file,suffix);
            if(unlink(sibling)!=0 && errno!=ENOENT)ok=false;
        }
        if(ok)ok=clear_fallback_locked(id,record);
    }
    xSemaphoreGive(s_lock);heap_caps_free(record);
    return ok;
}
bool game_journal_recheck_ready() {
    if(!s_lock || !s_protected || !offline_cache_storage_writable() ||
       offline_cache_cleanup_busy())return false;
    auto *record=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record)return false;
    game_journal_storage_lock();
    GameJournalInspection proof{};
    uint64_t total=0,available=0;
    bool ok=fallback_locked(record)==0 && game_journal_inspect_locked(&proof) &&
        proof.pending+proof.accepted<=(int)STORAGE_JOURNAL_SLOTS &&
        esp_vfs_fat_info("/fatfs",&total,&available)==ESP_OK &&
        available>=total*STORAGE_FREE_PERCENT/100+storage_allocated(sizeof(Record))*2;
    // A failed write is a transient latch, not a permanent storage diagnosis.
    s_ready=ok;
    if(ok)s_has_unloaded=proof.pending>MAX_PENDING_GAMES;
    game_journal_storage_unlock();heap_caps_free(record);return ok;
}
void game_journal_maintenance() {
    if(!s_lock || !s_protected || offline_cache_cleanup_busy())return;
    int64_t now=esp_timer_get_time();
    auto *record=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record)return;
    game_journal_storage_lock();
    if(now<s_retry_at) {game_journal_storage_unlock();heap_caps_free(record);return;}
    int state=fallback_locked(record);
    bool restored=false;
    if(state==1 && offline_cache_storage_writable()) {
        char file[96];
        restored=filename(record->game.base.externalId,file,sizeof(file)) &&
            save_record_locked(file,record);
        if(restored) {
            // Clear only after the complete journal replacement is committed.
            char id[40];snprintf(id,sizeof(id),"%s",record->game.base.externalId);
            restored=clear_fallback_locked(id,record);
        }
    }
    static constexpr int64_t delays[]={1000000,5000000,30000000};
    if(state==1 && !restored) {
        if(s_retry_attempt<2)++s_retry_attempt;
        s_retry_at=now+delays[s_retry_attempt];
    } else {s_retry_attempt=0;s_retry_at=now+30000000;}
    game_journal_storage_unlock();heap_caps_free(record);
    // Also retry a finish's separately failed active tombstone. Restrict this
    // to a completed game; never clear a live game's checkpoint error here.
    active_game_retry_finished_checkpoint();
    if(restored) {
        terminal_log_add("data_loss_risk","INFO","Fertiges Spiel nach Speicherfehler dauerhaft im Spielarchiv gesichert");
    }
    if(!s_ready)(void)game_journal_recheck_ready();
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
        if(strptime(stamp,"%Y-%m-%dT%H:%M:%S",&utc)) {
            // Newlib/ESP-IDF has no timegm. Convert civil UTC directly; never
            // change process TZ while the UI/inventory use local club dates.
            int year=utc.tm_year+1900,month=utc.tm_mon+1;
            year-=month<=2;
            const int era=(year>=0?year:year-399)/400;
            const unsigned yoe=(unsigned)(year-era*400);
            const unsigned doy=(153*(unsigned)(month+(month>2?-3:9))+2)/5+
                (unsigned)utc.tm_mday-1;
            const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
            const int64_t days=(int64_t)era*146097+(int64_t)doe-719468;
            finished=(time_t)(days*86400+utc.tm_hour*3600+utc.tm_min*60+utc.tm_sec);
        }
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
            if(changed) {
                record->checksum=checksum(record->game,record->accepted);
                if(!write_record(file,record)) {corrupt=true;break;}
            }
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
            if(!write_record(file,record)) {corrupt=true;break;}
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
    if(!s_lock) s_lock=xSemaphoreCreateMutex();
    if(!s_lock || !offline_cache_storage_available()) return false;
    nvs_handle_t n;
    if(nvs_open("tm_cache",NVS_READWRITE,&n)!=ESP_OK) return false;
    bool protected_volume=nvs_set_u8(n,"game_journal",1)==ESP_OK && nvs_commit(n)==ESP_OK;
    nvs_close(n);
    if(!protected_volume) return false;
    s_protected=true;
    s_ready=true;
    bool scanned=scan(0,false)>=0;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool bounded=trim_archive_locked(0);
    xSemaphoreGive(s_lock);
    if(!bounded) {
        // Keep old over-capacity pending records uploadable; only new games
        // are blocked. Never strand the excess records behind s_ready=false.
        s_has_unloaded=true;
        terminal_log_add("data_loss_risk","WARN","Spielarchiv über Budget; zuerst offene Spiele übertragen");
    }
    auto *fallback=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!fallback){s_ready=false;return false;}
    game_journal_storage_lock();
    int state=fallback_locked(fallback);
    if(state==1) {
        bool present=false;
        for(int i=0;i<g_store.pendingGamesCount;i++)
            if(!strcmp(g_store.pendingGames[i].externalId,fallback->game.base.externalId))present=true;
        if(!present && g_store.pendingGamesCount<MAX_PENDING_GAMES)
            g_store.pendingGames[g_store.pendingGamesCount++]=fallback->game.base;
        if(!present && g_store.pendingGamesCount>=MAX_PENDING_GAMES)s_has_unloaded=true;
        s_ready=false;s_retry_attempt=0;s_retry_at=esp_timer_get_time()+1000000;
    } else if(state<0)s_ready=false;
    game_journal_storage_unlock();heap_caps_free(fallback);
    return scanned && state>=0;
}
bool game_journal_resume_after_cleanup() {
    game_journal_storage_lock();
    GameJournalInspection inspection{};
    bool ok=offline_cache_storage_available() &&
        game_journal_inspect_locked(&inspection) && !inspection.pending &&
        !g_store.pendingGamesCount;
    if(ok) {
        nvs_handle_t n;
        ok=nvs_open("tm_cache",NVS_READWRITE,&n)==ESP_OK;
        if(ok) {
            ok=nvs_set_u8(n,"game_journal",1)==ESP_OK && nvs_commit(n)==ESP_OK;
            nvs_close(n);
        }
    }
    if(ok) {
        s_ready=true;
        s_has_unloaded=inspection.accepted>(int)STORAGE_JOURNAL_SLOTS;
    }
    game_journal_storage_unlock();return ok;
}
int game_journal_recover(unsigned days) {
    if(!s_ready) return -1;
    return scan(days,true);
}
bool game_journal_remap(int old_id,int new_id) {
    // Local players in an emergency finished game must remain synchronizable
    // even while new-game readiness is latched off.
    if(!s_lock || !old_id || !new_id || old_id==new_id) return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool recorded=false;
    for(int i=0;i<s_remap_count;i++) if(s_remaps[i].old_id==old_id) {
        if(s_remaps[i].new_id!=new_id) {xSemaphoreGive(s_lock);return false;}
        recorded=true;break;
    }
    if(!recorded && s_remap_count<MAX_PORTAL_SPIELER) {
        s_remaps[s_remap_count++]={old_id,new_id};recorded=true;
    }
    int fallback=fallback_locked(nullptr);
    if(fallback<0)recorded=false;
    if(recorded && fallback==1) {
        auto *r=(Record*)heap_caps_malloc(sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        recorded=r && fallback_locked(r)==1;
        if(recorded) {
            for(int i=0;i<r->game.base.teilnahmen_count;i++)
                if(r->game.base.teilnahmen[i].spielerId==old_id)r->game.base.teilnahmen[i].spielerId=new_id;
            for(int i=0;i<r->game.base.ergebnisse_count;i++)
                if(r->game.base.ergebnisse[i].spielerId==old_id)r->game.base.ergebnisse[i].spielerId=new_id;
            for(int i=0;i<r->game.spieler_count;i++)
                if(r->game.spielerIds[i]==old_id)r->game.spielerIds[i]=new_id;
            r->checksum=checksum(r->game,r->accepted);
            nvs_handle_t n;
            recorded=nvs_open("tm_cache",NVS_READWRITE,&n)==ESP_OK;
            if(recorded) {
                recorded=nvs_set_blob(n,"finish_pending",r,sizeof(*r))==ESP_OK && nvs_commit(n)==ESP_OK;
                nvs_close(n);
            }
        }
        heap_caps_free(r);
    }
    xSemaphoreGive(s_lock);
    return recorded && scan(0,false,old_id,new_id)>=0 && active_game_remap();
}
int game_journal_resolve_player_locked(int id) {
    for(int i=0;i<s_remap_count;i++)if(id==s_remaps[i].old_id)id=s_remaps[i].new_id;
    return id;
}
int game_journal_reload_pending() {
    if(!s_ready) return -1;
    int queued=scan(0,false);
    if(queued<0) return queued;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(!trim_archive_locked(0)) s_has_unloaded=true;
    xSemaphoreGive(s_lock);
    return queued;
}
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
