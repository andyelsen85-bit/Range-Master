#include "game_store.h"
#include "active_game.h"
#include "game_journal.h"
#include "terminal_logs.h"
#include "ui_manager.h"
#include "storage_write.h"
#include "fat_name.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <atomic>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include "runtime_safety.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
static SemaphoreHandle_t writer_mutex;
static TaskHandle_t writer_task;
static std::atomic<unsigned> dirty_revision{0},saved_revision{0};
static std::atomic<int64_t> dirty_at{0};
static std::atomic<bool> writer_busy{false};
static std::atomic<int64_t> writer_activity{0};
static std::atomic<bool> force_checkpoint{false};
static std::atomic<unsigned> attempted_revision{0};
static void writer_lock(){
    if(!writer_mutex)writer_mutex=xSemaphoreCreateMutex();
    configASSERT(writer_mutex);xSemaphoreTake(writer_mutex,portMAX_DELAY);
}
static void writer_unlock(){xSemaphoreGive(writer_mutex);}
#else
static void writer_lock(){}
static void writer_unlock(){}
#endif

#ifndef ACTIVE_GAME_ROOT
#define ACTIVE_GAME_ROOT "/fatfs"
#endif
struct ActiveData {
    Modus modus;
    int lauf,taubeIndex,spielerIndex,sequenzLen,spielerCount,ergebnisseCount;
    int activeAcknowledgedClays,activeGameCreditCount;
    int activeGameCreditPlayerIds[MAX_SPIELER];
    char activeGameCreditUseIds[MAX_SPIELER][40];
    char spielId[40];
    Spieler spieler[MAX_SPIELER];
    SequenzEintrag sequenz[MAX_SEQUENZ];
    uint8_t harakiriOrder[MAX_SPIELER][HARAKIRI_MAX_CLAYS];
    Ergebnis ergebnisse[MAX_ERGEBNISSE];
    bool currentFireSent;
    bool repeatCurrentLaunch; // occupies the old zeroed padding: binary size unchanged
    int customRuns;
};
static_assert(offsetof(ActiveData,customRuns)==offsetof(ActiveData,currentFireSent)+4,"Preserve active record ABI");
struct ActiveRecord {
    uint32_t magic,version,bytes,checksum,generation;
    bool active;
    ActiveData data;
};
static_assert(sizeof(ActiveRecord)<=8192,"Two active-game slots plus scratch must fit in 24 KiB");
static bool initialized=false,blocked=false;
static std::atomic<bool> had_active{false};
static std::atomic<uint32_t> next_generation{0};
static uint32_t committed_generation=0;
static int committed_slot=-1;
static ActiveRecord *last_written=nullptr; // PSRAM; compared only under writer mutex
static std::atomic<bool> pending_clear{false};
static std::atomic<int64_t> retry_at{0};
static std::atomic<unsigned> retry_step{0};
bool active_game_is_open(){return g_store.activeGameOpen||had_active.load();}
void active_game_mark_ended() {
    // RAM-only. Callers have already durably recorded the finished journal or
    // cancellation intent. The small ended-ID guard is flushed off LVGL.
    ++next_generation;
    g_store.activeGameOpen=false;g_store.activeGameStorageError=false;
    had_active=false;pending_clear=true;retry_at=0;retry_step=0;
}
static uint32_t hash(const ActiveRecord &r) {
    uint32_t h=2166136261u;const auto *b=(const uint8_t*)&r;
    for(size_t i=0;i<sizeof(r);i++)
        if(i<offsetof(ActiveRecord,checksum)||i>=offsetof(ActiveRecord,checksum)+sizeof(r.checksum))
            h=(h^b[i])*16777619u;
    return h;
}
static void path(int slot,char *out,size_t n) {
    char base[9];snprintf(base,sizeof(base),"ACTIVE%u",(unsigned)(slot&1));
    (void)fat_name(out,n,base,"BIN",ACTIVE_GAME_ROOT);
}
#define ACTIVE_FIELDS(X) \
    X(modus) X(lauf) X(taubeIndex) X(spielerIndex) X(sequenzLen) X(spielerCount) \
    X(ergebnisseCount) X(activeAcknowledgedClays) X(activeGameCreditCount) \
    X(activeGameCreditPlayerIds) X(activeGameCreditUseIds) X(spielId) X(spieler) \
    X(sequenz) X(harakiriOrder) X(ergebnisse) X(currentFireSent) X(repeatCurrentLaunch)

static bool valid(const ActiveRecord &r) {
    if(r.magic!=0x48414b49 || (r.version!=1&&r.version!=2) || r.bytes!=sizeof(r) ||
       r.checksum!=hash(r))return false;
    if(!r.active)return true;
    const auto &d=r.data;
    if(d.modus<MODUS_NORMAL || d.modus>MODUS_HARAKIRI_CUSTOM ||
       d.spielerCount<1 || d.spielerCount>MAX_SPIELER || d.sequenzLen<1 ||
       d.sequenzLen>MAX_SEQUENZ || d.spielerIndex<0 ||
       d.spielerIndex>=d.spielerCount || d.taubeIndex<0 || d.taubeIndex>d.sequenzLen ||
       d.lauf<1 || d.lauf>2 || d.customRuns<1 || d.customRuns>2 ||
       d.ergebnisseCount<0 || d.ergebnisseCount>MAX_ERGEBNISSE ||
       d.activeGameCreditCount<0 || d.activeGameCreditCount>MAX_SPIELER ||
       !d.spielId[0] || d.spielId[39])return false;
    for(int p=0;p<d.spielerCount;p++) {
        if(d.spieler[p].name[MAX_NAME_LEN-1])return false;
        if(d.modus!=MODUS_HARAKIRI && d.modus!=MODUS_HARAKIRI_CUSTOM)continue;
        bool seen[HARAKIRI_MAX_CLAYS]={};
        for(int c=0;c<d.sequenzLen;c++) {
            int index=d.harakiriOrder[p][c];
            if(index>=d.sequenzLen || seen[index])return false;
            seen[index]=true;
            const auto &entry=d.sequenz[index];
            if(entry.maschine<MASCHINE_A || entry.maschine>=MASCHINE_COUNT)return false;
            if(entry.isPair && !entry.isDoublette &&
               (c+1>=d.sequenzLen || d.harakiriOrder[p][c+1]!=index+1 ||
                !d.sequenz[index+1].isDoublette))return false;
            if(entry.isPair && entry.isDoublette &&
               (c==0 || d.harakiriOrder[p][c-1]!=index-1))return false;
        }
    }
    return true;
}
static bool read_file(const char *name,ActiveRecord *out,bool *present) {
    errno=0;
    FILE *f=fopen(name,"rb");
    if(!f){*present=errno!=ENOENT;return false;}
    *present=true;
    bool ok=fread(out,1,sizeof(*out),f)==sizeof(*out) && fgetc(f)==EOF && !ferror(f);
    if(fclose(f)!=0)ok=false;
    return ok&&valid(*out);
}
static bool read(int slot,ActiveRecord *out,bool *present,bool strict=false) {
    char name[96],temp[104];path(slot,name,sizeof(name));
    (void)fat_name(temp,sizeof(temp),name,"TMP",ACTIVE_GAME_ROOT);
    bool bin_present=false,tmp_present=false;
    bool ok=read_file(name,out,&bin_present);
    struct stat st{};errno=0;
    if(stat(temp,&st)!=0&&errno==ENOENT){*present=bin_present;return ok;}
    auto *candidate=(ActiveRecord*)heap_caps_malloc(sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!candidate){*present=true;return false;}
    bool tmp_ok=read_file(temp,candidate,&tmp_present);
    if(tmp_ok&&(!ok||candidate->generation>=out->generation))*out=*candidate;
    heap_caps_free(candidate);*present=bin_present||tmp_present;
    if(strict&&((bin_present&&!ok)||(tmp_present&&!tmp_ok)))return false;
    return ok||tmp_ok;
}
size_t active_game_record_size(){return sizeof(ActiveRecord);}
bool active_game_repair_safe_locked() {
    ActiveGameRepairInspection result{};
    return active_game_inspect_repair_locked(&result);
}
bool active_game_inspect_repair_locked(ActiveGameRepairInspection *out) {
    *out={};out->had_active=had_active.load();
    out->ram_active=g_store.activeGameOpen||g_store.harakiriOrdersReady||g_store.screen==SCREEN_SPIEL;
    DIR *dir=opendir(ACTIVE_GAME_ROOT);
    if(!dir){out->unreadable=1;out->error=errno;return false;}
    ActiveRecord *r=nullptr;bool found=false;uint32_t generation=0;
    for(;;) {
        errno=0;auto *entry=readdir(dir);
        if(!entry){if(errno){++out->unreadable;out->error=errno;}break;}
        const char *n=entry->d_name;
        if(strcasecmp(n,"active0.bin")&&strcasecmp(n,"active1.bin")&&
            strcasecmp(n,"active0.tmp")&&strcasecmp(n,"active1.tmp")&&
            strcasecmp(n,"active0.bin.tmp")&&strcasecmp(n,"active1.bin.tmp"))continue;
        ++out->present;
        // Empty roots need no PSRAM at all. Allocation failure is uncertainty
        // only when a real record exists, never synthetic "present=true".
        if(!r)r=(ActiveRecord*)heap_caps_malloc(sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!r){++out->unreadable;out->error=ENOMEM;continue;}
        char file[384];
        if(!fat_name(file,sizeof(file),n,nullptr,ACTIVE_GAME_ROOT)){++out->unreadable;out->error=EINVAL;continue;}
        bool present=false;
        if(read_file(file,r,&present)) {
            if(!found||r->generation>generation){found=true;generation=r->generation;
                out->active=r->active&&!store_game_was_ended(r->data.spielId)&&
                    !store_credit_game_cancelled(r->data.spielId);}
        } else {++out->unreadable;out->error=errno?errno:EBADMSG;}
    }
    if(closedir(dir)!=0){++out->unreadable;out->error=errno;}
    heap_caps_free(r);
    out->stale_latch=out->had_active&&!out->active&&!out->unreadable&&!out->ram_active;
    // This is only a volatile cache of the last successful checkpoint. It is
    // not evidence of pending work when BOTH disk and live RAM are empty.
    if(out->stale_latch) {
        had_active=false;
        terminal_log_add("sync","INFO","Aktives Spiel gemerkt (had_active), aber Dateien und RAM leer: veraltete Markierung zurückgesetzt");
    }
    if(!out->active&&!out->unreadable&&!out->ram_active) {
        g_store.activeGameStorageError=false;blocked=false;
    }
    return !out->unreadable&&!out->active&&!out->ram_active&&(!out->had_active||out->stale_latch);
}
static void remap_record(ActiveRecord *r) {
    if(!r->active)return;
    for(int i=0;i<r->data.spielerCount;i++)
        r->data.spieler[i].id=game_journal_resolve_player_locked(r->data.spieler[i].id);
    for(int i=0;i<r->data.ergebnisseCount;i++)
        r->data.ergebnisse[i].spielerId=game_journal_resolve_player_locked(r->data.ergebnisse[i].spielerId);
    for(int i=0;i<r->data.activeGameCreditCount;i++)
        r->data.activeGameCreditPlayerIds[i]=game_journal_resolve_player_locked(r->data.activeGameCreditPlayerIds[i]);
}
static bool write_slot(int slot,ActiveRecord *r) {
    r->checksum=hash(*r);
    char name[96],temp[104];path(slot,name,sizeof(name));
    (void)fat_name(temp,sizeof(temp),name,"TMP",ACTIVE_GAME_ROOT);
    FILE *f=fopen(temp,"wb");bool ok=f!=nullptr,complete=false;
    if(!f) {
        char message[200];snprintf(message,sizeof(message),"Spielstand open %.96s: errno=%d",temp,errno);
        terminal_log_add("data_loss_risk","ERROR",message);return false;
    }
    if(f) {
        // A/B already retains the last committed copy. One fsync per new slot,
        // not one for the header and a second for its payload.
        complete=fwrite(r,1,sizeof(*r),f)==sizeof(*r);
        ok=complete&&fflush(f)==0&&fsync(fileno(f))==0;
        if(fclose(f)!=0)ok=false;
    }
    if(!ok){int error=errno;if(!complete)(void)unlink(temp);
        char message[200];snprintf(message,sizeof(message),"Spielstand write/fsync/close %.96s: errno=%d",temp,error);
        terminal_log_add("data_loss_risk","ERROR",message);return false;}
    if(unlink(name)!=0&&errno!=ENOENT) {
        char message[200];snprintf(message,sizeof(message),"Spielstand unlink %.96s: errno=%d",name,errno);
        terminal_log_add("data_loss_risk","ERROR",message);return false;
    }
    ok=rename(temp,name)==0; // Complete temp remains recoverable if rename fails.
    if(!ok){char message[200];snprintf(message,sizeof(message),"Spielstand rename %.96s: errno=%d",temp,errno);
        terminal_log_add("data_loss_risk","ERROR",message);}
    return ok;
}
bool active_game_remap() {
    if(!initialized)return true;
    if(blocked)return false;
    writer_lock();
    game_journal_storage_lock();
    if(committed_slot<0){game_journal_storage_unlock();writer_unlock();return true;}
    auto *r=(ActiveRecord*)heap_caps_malloc(sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool present=false,ok=r&&read(committed_slot,r,&present);
    if(ok) {
        remap_record(r);
    }
    game_journal_storage_unlock();
    if(ok) {
        // Identity-only rewrite: keep the gameplay revision. A newer score
        // already captured by LVGL must not be superseded by this disk copy.
        int other=1-committed_slot;
        ok=write_slot(other,r);
        if(ok){committed_slot=other;ok=write_slot(1-other,r);}
        if(ok&&last_written)*last_written=*r;
    }
    heap_caps_free(r);writer_unlock();return ok;
}

bool active_game_checkpoint() {
    if(!initialized)return true; // NVS normalization precedes the FAT mount.
    if(blocked)return false; // Never overwrite an unrecognized active record.
    auto *r=(ActiveRecord*)heap_caps_calloc(1,sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool acquired=false;
    if(!ui_manager_snapshot_lock(&acquired)){heap_caps_free(r);return false;}
    if(!r){g_store.activeGameStorageError=g_store.activeGameOpen||g_store.harakiriOrdersReady||
            (g_store.screen==SCREEN_SPIEL&&g_store.spielerCount>0);
        if(acquired)ui_manager_unlock();
        terminal_log_add("data_loss_risk","ERROR","Spielstand allocation: errno=12 (ENOMEM)");
        return false;}
    r->active=g_store.activeGameOpen||g_store.harakiriOrdersReady ||
        (g_store.screen==SCREEN_SPIEL && g_store.spielerCount>0 && g_store.spielId[0]);
    if(!r->active&&!had_active&&!pending_clear){
        g_store.activeGameStorageError=false;
        heap_caps_free(r);if(acquired)ui_manager_unlock();return true;
    }
    r->magic=0x48414b49;r->version=2;r->bytes=sizeof(*r);r->generation=++next_generation;
    if(r->active) {
#define PACK(field) memcpy(&r->data.field,&g_store.field,sizeof(r->data.field));
        ACTIVE_FIELDS(PACK)
#undef PACK
        r->data.customRuns=g_store.modus==MODUS_HARAKIRI_CUSTOM ? g_store.customLaeufe[4] :
            (g_store.modus>=MODUS_CUSTOM_1 && g_store.modus<=MODUS_CUSTOM_4 ?
                g_store.customLaeufe[g_store.modus-MODUS_CUSTOM_1] : 2);
    }
    if(acquired)ui_manager_unlock();
    writer_lock();
    bool ok=true;
    if(r->generation>committed_generation) {
        // Rewrite only the older slot: the last committed file always survives.
        game_journal_storage_lock();
        remap_record(r); // includes snapshots captured before RAM publication
        game_journal_storage_unlock();
        int slot=committed_slot==0?1:0;
        bool identical=last_written&&last_written->active==r->active&&
            !memcmp(&last_written->data,&r->data,sizeof(r->data));
        if(!identical)ok=write_slot(slot,r);
        if(ok) {
            committed_generation=r->generation;
            if(!identical)committed_slot=slot;
            had_active=r->active;
            if(!last_written)last_written=(ActiveRecord*)heap_caps_malloc(sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(last_written)*last_written=*r;
            if(!r->active)pending_clear=false;
        }
    }
    uint32_t generation=r->generation;bool snapshot_active=r->active;
    writer_unlock();heap_caps_free(r);
    // A delayed ACK/settings save must never clear the error from a newer,
    // failed score checkpoint. Recheck its generation under the UI lock.
    if(ui_manager_snapshot_lock(&acquired)) {
        if(generation==next_generation.load())g_store.activeGameStorageError=!ok&&snapshot_active;
        if(acquired)ui_manager_unlock();
    }
    return ok;
}

#ifdef ESP_PLATFORM
bool active_game_checkpoint_pending(){return dirty_revision.load()!=saved_revision.load();}
bool active_game_writer_idle_for(unsigned milliseconds) {
    return !writer_busy.load()&&!active_game_checkpoint_pending()&&
        esp_timer_get_time()-writer_activity.load()>=(int64_t)milliseconds*1000;
}
void active_game_request_flush() {
    force_checkpoint=true;
    active_game_schedule();
}
void active_game_schedule() {
    dirty_at=esp_timer_get_time();++dirty_revision;
    if(writer_task)xTaskNotifyGive(writer_task);
}
bool active_game_flush() {
    if(xTaskGetCurrentTaskHandle()!=writer_task) {
        if(!writer_task)return false;
        active_game_schedule();
        unsigned target=dirty_revision.load();
        force_checkpoint=true;xTaskNotifyGive(writer_task);
        int64_t deadline=esp_timer_get_time()+5000000;
        while(attempted_revision.load()<target&&esp_timer_get_time()<deadline)
            vTaskDelay(pdMS_TO_TICKS(5));
        return saved_revision.load()>=target;
    }
    unsigned revision=dirty_revision.load();
    int64_t start=esp_timer_get_time();
    bool ok=active_game_checkpoint();
    if(ok)saved_revision=revision; // never acknowledge a newer, uncaptured score
    attempted_revision=revision;
    int ms=(esp_timer_get_time()-start)/1000;
    esp_log_level_t level=ms>2000?ESP_LOG_WARN:ESP_LOG_INFO;
    ESP_LOG_LEVEL(level,"checkpoint",
        "checkpoint_ms=%d nvs_ms=0 fat_ms=%d queue_depth=%u ok=%d",
        ms,ms,active_game_checkpoint_pending()?1u:0u,ok);
    return ok;
}
static void checkpoint_worker(void*) {
    runtime_task_watch(RuntimeTask::Checkpoint);
    for(;;) {
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(50));
        // Game end is a hard durability point, performed by this same writer.
        bool work=game_store_finish_pending()||active_game_checkpoint_pending();
        if(work){writer_busy=true;writer_activity=esp_timer_get_time();}
        game_store_flush_finished();
        if(active_game_checkpoint_pending()&&
           (force_checkpoint.exchange(false)||esp_timer_get_time()-dirty_at.load()>=250000)) {
            if(!active_game_flush())vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if(work){writer_activity=esp_timer_get_time();writer_busy=false;}
    }
}
void active_game_writer_init() {
    if(writer_task)return;
    if(!writer_mutex)writer_mutex=xSemaphoreCreateMutex();
    if(!writer_mutex||xTaskCreate(checkpoint_worker,"game_writer",16384,nullptr,1,&writer_task)!=pdPASS)
        terminal_log_add("data_loss_risk","ERROR","Spielstand-Hintergrundtask nicht verfügbar");
}
#endif

void active_game_retry_finished_checkpoint() {
    store_record_game_end(""); // retry a small NVS guard without holding LVGL
    bool acquired=false;
    if(!ui_manager_snapshot_lock(&acquired))return;
    bool retry=initialized && !blocked && (pending_clear.load()||had_active.load()) &&
        !g_store.activeGameOpen&&!g_store.harakiriOrdersReady&&g_store.screen!=SCREEN_SPIEL;
    int64_t now=esp_timer_get_time();
    if(retry&&now<retry_at)retry=false;
    if(acquired)ui_manager_unlock();
    if(retry) {
#ifdef ESP_PLATFORM
        // Maintenance requests work; it is not a second FAT writer. Do not
        // reset the idle deadline on every maintenance tick.
        if(!active_game_checkpoint_pending())active_game_schedule();
#else
        static const int seconds[]={1,5,30};
        if(active_game_checkpoint()){retry_step=0;retry_at=0;}
        else {retry_at=now+(int64_t)seconds[retry_step<2?retry_step++:2]*1000000;}
#endif
    }
}

bool active_game_restore() {
    ActiveGameRepairInspection initial{};
    (void)active_game_inspect_repair_locked(&initial);
    if(!initial.present&&!initial.unreadable&&!initial.ram_active) {
        // No checkpoint files is a normal boot, not a reason to allocate two
        // large records and latch an OOM storage error on an empty filesystem.
        initialized=true;blocked=false;had_active=false;committed_slot=-1;
        committed_generation=next_generation=0;g_store.activeGameStorageError=false;
        return true;
    }
    auto *r=(ActiveRecord*)heap_caps_malloc(sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    auto *best=(ActiveRecord*)heap_caps_calloc(1,sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!r||!best){
        heap_caps_free(r);heap_caps_free(best);blocked=true;initialized=true;
        g_store.activeGameStorageError=true;
        snprintf(g_store.lineupWarning,sizeof(g_store.lineupWarning),"Zu wenig RAM zum Prüfen des offenen Harakiri-Spiels.");
        return false;
    }
    bool any=false,invalid=false;int best_slot=-1;
    for(int slot=0;slot<2;slot++) {
        bool present=false;
        bool ok=read(slot,r,&present);
        if(ok && (best_slot<0 || r->generation>best->generation)){*best=*r;best_slot=slot;}
        if(present&&!ok)invalid=true;
        any|=present;
    }
    blocked=any&&best_slot<0;initialized=true;
    if(!any&&!g_store.harakiriOrdersReady&&g_store.screen!=SCREEN_SPIEL)had_active=false;
    if(blocked) {
        g_store.activeGameStorageError=true;
        snprintf(g_store.lineupWarning,sizeof(g_store.lineupWarning),
                 "Harakiri-Spielstand beschädigt. Nicht überschreiben; Diagnose erforderlich.");
        terminal_log_add("data_loss_risk","ERROR",g_store.lineupWarning);
    }
    if(best_slot>=0) {
        // A complete temporary can be the newest durable checkpoint after a
        // reset between unlink and rename. Canonicalize it before the next save.
        char name[96],temp[104];path(best_slot,name,sizeof(name));
        (void)fat_name(temp,sizeof(temp),name,"TMP",ACTIVE_GAME_ROOT);
        bool present=false;
        if(read_file(temp,r,&present)&&r->generation==best->generation) {
            if((unlink(name)!=0&&errno!=ENOENT)||rename(temp,name)!=0)invalid=true;
        }
        committed_generation=next_generation=best->generation;committed_slot=best_slot;had_active=best->active;
        // A crash between final-journal commit and active tombstone must not
        // resurrect an already finished (possibly already accepted) game.
        bool cancelled=best->active&&(store_credit_game_cancelled(best->data.spielId)||
            store_game_was_ended(best->data.spielId));
        bool pending_finish=false;
        for(int i=0;i<g_store.pendingGamesCount;i++)
            if(!strcmp(g_store.pendingGames[i].externalId,best->data.spielId))pending_finish=true;
        int finished=best->active&&!cancelled?
            (pending_finish?1:game_journal_contains(best->data.spielId)):0;
        if(best->active&&!cancelled && (finished<0 || g_store.operatingMode==TERMINAL_MODE_CATERING)) {
            blocked=true;g_store.activeGameStorageError=true;
            terminal_log_add("data_loss_risk","ERROR","Offenes Harakiri-Spiel gesperrt: Archiv prüfen oder Catering-Modus verlassen");
        } else if(best->active&&!finished&&!cancelled) {
#define UNPACK(field) memcpy(&g_store.field,&best->data.field,sizeof(g_store.field));
            ACTIVE_FIELDS(UNPACK)
#undef UNPACK
            g_store.harakiriOrdersReady=g_store.modus==MODUS_HARAKIRI||g_store.modus==MODUS_HARAKIRI_CUSTOM;
            if(g_store.modus==MODUS_HARAKIRI_CUSTOM)g_store.customLaeufe[4]=best->data.customRuns;
            else if(g_store.modus>=MODUS_CUSTOM_1&&g_store.modus<=MODUS_CUSTOM_4)
                g_store.customLaeufe[g_store.modus-MODUS_CUSTOM_1]=best->data.customRuns;
            g_store.screen=SCREEN_SPIEL;
            g_store.activeGameOpen=true;
            // A physical shot may have happened just before power loss.
            // Never authorize a duplicate automatically after restart.
            // FIRE is admitted from RAM before the checkpoint completes.
            // Even a valid v2 record with false may predate a physical launch.
            // Require an explicit operator repeat for every restored unit.
            g_store.currentFireSent=true;
            g_store.repeatCurrentLaunch=false;
            g_store.activeGameResumed=g_store.currentFireSent;
            terminal_log_add("game","WARN","Spiel fortgesetzt; letzte Einheit vor erneutem Auslösen prüfen");
            if(invalid)terminal_log_add("data_loss_risk","WARN",
                "Unvollständiger Spielstand: letzte gültige Sicherung geladen; letzte Wertung prüfen");
        } else if(best->active) {
            // Finished journal OR durable credit cancellation wins.
            g_store.harakiriOrdersReady=false;
            store_record_game_end(best->data.spielId);
            active_game_mark_ended();
            if(cancelled)terminal_log_add("game","WARN","Storniertes Spiel nach Neustart nicht fortgesetzt; Kredit-Storno bleibt erhalten");
        }
    }
    heap_caps_free(r);heap_caps_free(best);
    if(best_slot>=0 && (had_active||pending_clear) && !g_store.activeGameOpen &&
       !g_store.harakiriOrdersReady)return active_game_checkpoint();
    return !blocked;
}
