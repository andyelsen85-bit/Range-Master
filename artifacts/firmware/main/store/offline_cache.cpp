#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "esp_partition.h"
#include "sdkconfig.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "game_store.h"
#include "offline_cache.h"
#include "game_journal.h"
#include "active_game.h"
#include "terminal_logs.h"
#include "storage_budget.h"
#include "portal_runtime.h"
#include "ui_manager.h"
#include "esp_timer.h"
#include <strings.h>
#include <initializer_list>

static const char *TAG = "offline_cache";
static const char *ROOT = "/fatfs/tmcache";
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static SemaphoreHandle_t s_mutex;
static bool s_mounted;
static bool s_mount_attempted;
static uint32_t s_failed_sections;
static bool s_metadata_failed;
static bool s_census_done;
static uint32_t s_saved_hash[OFFLINE_CACHE_SECTION_COUNT];
static int64_t s_saved_at[OFFLINE_CACHE_SECTION_COUNT];
static bool s_saved_known[OFFLINE_CACHE_SECTION_COUNT];
static bool s_attempted[OFFLINE_CACHE_SECTION_COUNT];
static uint32_t s_dirty_sections;
static std::atomic<bool> s_repair_pending{false};
static std::atomic<bool> s_cleaning{false},s_storage_blocked{false};
static std::atomic<int> s_disk_pending{0};
static constexpr size_t CACHE_ALLOCATION_UNIT = 4096;
static_assert(CACHE_ALLOCATION_UNIT >= CONFIG_WL_SECTOR_SIZE &&
              CACHE_ALLOCATION_UNIT % CONFIG_WL_SECTOR_SIZE == 0,
              "FAT allocation unit must be a multiple of the WL sector size");

// Called under the cache mutex. An unrelated successful section must never
// hide an earlier failed snapshot or metadata write.
static void update_health(void) {
    g_store.offlineCacheHealthy =
        s_mounted && s_failed_sections == 0 && !s_metadata_failed;
}
#define CACHE_MAGIC 0x544D4348u
#define CACHE_SCHEMA 1u
// Layout 3 is a one-time recovery migration for released devices that can have
// a v2 marker alongside an unformatted reserved partition. Healthy v2 volumes
// mount without formatting; only an unmountable v2 cache is rebuilt. NVS
// outboxes are outside this partition and remain untouched.
#define CACHE_FAT_LAYOUT 3u
typedef struct { uint32_t magic; uint16_t schema, section; uint32_t payload_len, crc32; } CacheEnvelope;
static_assert(sizeof(CacheEnvelope) == 16, "stable cache envelope");
typedef struct { int count; PortalSpieler value[MAX_PORTAL_SPIELER]; } RosterPayload;
typedef struct { int count; Produkt value[MAX_PRODUKTE]; } ProductsPayload;
typedef struct { int count; FinishedGame value[MAX_HISTORY]; } HistoryPayload;
typedef struct { char date[11]; int ids[MAX_PORTAL_SPIELER]; KreditStand values[MAX_PORTAL_SPIELER]; } CreditsPayload;
typedef struct { char date[11]; MunitionStand values[MAX_PORTAL_SPIELER]; int32_t cal12, cal20; } SalesPayload;
typedef struct { int64_t lastSync; bool health, loaded; char dailyDate[11]; char tokens[OFFLINE_CACHE_SECTION_COUNT][CACHE_MANIFEST_TOKEN_LEN]; } MetaPayload;
static const char *names[] = {"roster","products","history","credits","sales","bills"};

static void lock_cache(void) { if (!s_mutex) s_mutex = xSemaphoreCreateMutex(); configASSERT(s_mutex); xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock_cache(void) { xSemaphoreGive(s_mutex); }
static size_t payload_size(OfflineCacheSection s) {
    switch (s) {
    case OFFLINE_CACHE_ROSTER: return sizeof(RosterPayload); case OFFLINE_CACHE_PRODUCTS: return sizeof(ProductsPayload);
    case OFFLINE_CACHE_HISTORY: return sizeof(HistoryPayload); case OFFLINE_CACHE_CREDITS: return sizeof(CreditsPayload);
    case OFFLINE_CACHE_SALES: return sizeof(SalesPayload); case OFFLINE_CACHE_BILLS: return sizeof(BillDaySummary);
    default: return 0;
    }
}
static size_t cache_budget_bytes() {
    size_t sum=storage_allocated(sizeof(CacheEnvelope)+sizeof(MetaPayload));
    for(unsigned i=0;i<OFFLINE_CACHE_SECTION_COUNT;i++)
        sum+=storage_allocated(sizeof(CacheEnvelope)+payload_size((OfflineCacheSection)i));
    return sum;
}
struct Census {uint64_t bytes=0,allocated=0;unsigned files=0,dirs=0;bool complete=true;};
static void census_directory(const char *root,Census *out,bool log_files,unsigned depth=0) {
    if(depth>8) {out->complete=false;return;}
    DIR *dir=opendir(root);
    if(!dir) {if(errno!=ENOENT) out->complete=false;return;}
    ++out->dirs;
    for(;;) {
        errno=0;struct dirent *entry=readdir(dir);
        if(!entry) {if(errno) out->complete=false;break;}
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        char path[384];int n=snprintf(path,sizeof(path),"%s/%s",root,entry->d_name);
        struct stat st{};
        if(n<0 || (size_t)n>=sizeof(path) || stat(path,&st)!=0) {out->complete=false;continue;}
        if(S_ISDIR(st.st_mode)) {census_directory(path,out,log_files,depth+1);continue;}
        if(!S_ISREG(st.st_mode)) {out->complete=false;continue;}
        ++out->files;out->bytes+=(uint64_t)st.st_size;
        out->allocated+=storage_allocated((size_t)st.st_size);
        if(log_files) ESP_LOGI(TAG,"FAT census file=%s size=%llu allocated=%llu",path,
            (unsigned long long)st.st_size,(unsigned long long)storage_allocated((size_t)st.st_size));
    }
    if(closedir(dir)!=0) out->complete=false;
}
struct VolumeUsage {uint64_t total=0,free=0,used=0,reachable=0,lost=0;bool proven=false;};
static VolumeUsage volume_usage(const char *step,bool log=true) {
    VolumeUsage u;Census c;census_directory("/fatfs",&c,false);
    u.proven=c.complete&&esp_vfs_fat_info("/fatfs",&u.total,&u.free)==ESP_OK&&u.free<=u.total;
    if(u.proven) {
        u.used=(u.total-u.free)/CACHE_ALLOCATION_UNIT;
        u.reachable=c.allocated/CACHE_ALLOCATION_UNIT+c.dirs;
        u.lost=u.used>u.reachable?u.used-u.reachable:0;
    }
    if(log)ESP_LOGW(TAG,"FAT lost clusters=%llu (%llu bytes) used_clusters=%llu reachable_clusters=%llu total=%llu free=%llu step=%s proven=%d (directory allocation estimated)",
        (unsigned long long)u.lost,(unsigned long long)(u.lost*CACHE_ALLOCATION_UNIT),
        (unsigned long long)u.used,(unsigned long long)u.reachable,(unsigned long long)u.total,
        (unsigned long long)u.free,step,(int)u.proven);
    return u;
}
static void log_census_once() {
    if(s_census_done) return;
    s_census_done=true;
    Census census;census_directory("/fatfs",&census,true);
    uint64_t total=0,free_bytes=0;
    (void)esp_vfs_fat_info("/fatfs",&total,&free_bytes);
    ESP_LOGI(TAG,"FAT census files=%u dirs=%u sum=%llu allocated=%llu total=%llu free=%llu complete=%d",
        census.files,census.dirs,(unsigned long long)census.bytes,(unsigned long long)census.allocated,
        (unsigned long long)total,(unsigned long long)free_bytes,(int)census.complete);
    ESP_LOGI(TAG,"FAT budget FinishedGame=%u Record=%u x %u slots (+2 scratch)=%llu; "
        "journal<=50%% cache<=25%% free>=20%%; raw storage=0x980000 (WL/FAT excluded from usable total)",
        (unsigned)sizeof(FinishedGame),(unsigned)game_journal_record_size(),STORAGE_JOURNAL_SLOTS,
        (unsigned long long)(storage_allocated(game_journal_record_size())*(STORAGE_JOURNAL_SLOTS+2)));
    for(unsigned i=0;i<OFFLINE_CACHE_SECTION_COUNT;i++)
        ESP_LOGI(TAG,"FAT budget cache %s payload=%u allocated=%llu",names[i],
            (unsigned)payload_size((OfflineCacheSection)i),
            (unsigned long long)storage_allocated(sizeof(CacheEnvelope)+payload_size((OfflineCacheSection)i)));
    ESP_LOGI(TAG,"FAT budget cache total=%llu history cap=%u accepted archive cap=%u",
        (unsigned long long)cache_budget_bytes(),(unsigned)MAX_HISTORY,STORAGE_ARCHIVE_SLOTS);
    ESP_LOGI(TAG,"FAT budget active Harakiri game: record=%u x2 + scratch allocated=%llu (cap=24576); retained during cleanup",
        (unsigned)active_game_record_size(),
        (unsigned long long)(3*storage_allocated(active_game_record_size())));
    ESP_LOGI(TAG,"FAT budget meta payload=%u; raw-minus-usable WL/FAT overhead=%llu bytes",
        (unsigned)sizeof(MetaPayload),(unsigned long long)(total<=0x980000?0x980000-total:0));
    volume_usage("mount census");
}
static bool removable_root_only_locked() {
    DIR *d=opendir("/fatfs");if(!d)return false;
    bool ok=true;
    for(;;) {
        errno=0;auto *e=readdir(d);if(!e){if(errno)ok=false;break;}
        const char *n=e->d_name;
        if(!strcmp(n,".")||!strcmp(n,".."))continue;
        char full[384];snprintf(full,sizeof(full),"/fatfs/%s",n);struct stat st{};
        if(stat(full,&st)!=0){ok=false;continue;}
        if(!strcasecmp(n,"tmcache")){if(!S_ISDIR(st.st_mode))ok=false;continue;}
        if(!S_ISREG(st.st_mode)){ok=false;continue;}
        if(!strcasecmp(n,"tmcache")||!strncasecmp(n,"g_",2)||!strcasecmp(n,"probe.tmp")||
           !strcasecmp(n,"active0.bin")||!strcasecmp(n,"active1.bin")||
           !strcasecmp(n,"active0.bin.tmp")||!strcasecmp(n,"active1.bin.tmp")||
           !strcasecmp(n,"queue0.bin")||!strcasecmp(n,"queue1.bin"))continue;
        ok=false;
    }
    if(closedir(d)!=0)ok=false;
    d=opendir(ROOT);if(!d)return ok&&errno==ENOENT;
    for(;;) {
        errno=0;auto *e=readdir(d);if(!e){if(errno)ok=false;break;}
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        char full[384];snprintf(full,sizeof(full),"%s/%s",ROOT,e->d_name);struct stat st{};
        if(stat(full,&st)!=0||!S_ISREG(st.st_mode)){ok=false;continue;}
        bool known=false;
        for(unsigned i=0;i<=OFFLINE_CACHE_SECTION_COUNT;i++)for(const char *ext:{"bin","bak","tmp"}) {
            char name[40];snprintf(name,sizeof(name),"%s.%s",i<OFFLINE_CACHE_SECTION_COUNT?names[i]:"meta",ext);
            if(!strcasecmp(name,e->d_name))known=true;
        }
        if(!known)ok=false;
    }
    if(closedir(d)!=0)ok=false;
    return ok;
}
static bool repair_marker(uint8_t intent) {
    nvs_handle_t h=0;
    if(nvs_open("tm_cache",NVS_READWRITE,&h)!=ESP_OK)return false;
    bool ok=nvs_set_u8(h,"fat_repair",intent)==ESP_OK;
    if(ok&&!intent)ok=nvs_set_u8(h,"fat_layout",CACHE_FAT_LAYOUT)==ESP_OK;
    if(ok)ok=nvs_commit(h)==ESP_OK;
    nvs_close(h);return ok;
}
static bool format_empty_locked(const esp_vfs_fat_mount_config_t &cfg) {
    // Persist authorization BEFORE destruction; resume only this proven-empty
    // operation after reset. New games remain blocked until the intent clears.
    s_repair_pending=true;
    s_mounted=false;s_storage_blocked=true;
    if(!repair_marker(1))return false;
    volume_usage("before empty-volume format");
    if(s_wl!=WL_INVALID_HANDLE) {
        if(esp_vfs_fat_spiflash_unmount_rw_wl("/fatfs",s_wl)!=ESP_OK)return false;
        s_wl=WL_INVALID_HANDLE;
    }
    auto format=cfg;format.format_if_mount_failed=true;
    if(esp_vfs_fat_spiflash_format_cfg_rw_wl("/fatfs","storage",&format)!=ESP_OK)return false;
    auto safe=cfg;safe.format_if_mount_failed=false;
    if(esp_vfs_fat_spiflash_mount_rw_wl("/fatfs","storage",&safe,&s_wl)!=ESP_OK)return false;
    if(mkdir(ROOT,0755)!=0&&errno!=EEXIST)return false;
    auto u=volume_usage("after empty-volume format");
    if(!u.proven||!u.total||u.free<=u.total*80/100||!repair_marker(0))return false;
    s_repair_pending=false;
    memset(s_saved_known,0,sizeof(s_saved_known));
    memset(s_attempted,0,sizeof(s_attempted));
    ESP_LOGW(TAG,"FAT neu formatiert wegen verwaister Cluster, keine offenen Spiele");
    terminal_log_add("sync","INFO","FAT neu formatiert wegen verwaister Cluster, keine offenen Spiele");
    return true;
}
static bool remove_cache_files_locked() {
    // Strict allow-list. Unknown files and ALL g_* records/parts/backups survive.
    bool ok=true;
    const char *extensions[]={"bin","bak","tmp"};
    for(unsigned i=0;i<=OFFLINE_CACHE_SECTION_COUNT;i++) {
        const char *name=i<OFFLINE_CACHE_SECTION_COUNT?names[i]:"meta";
        for(const char *extension:extensions) {
            char path[64];snprintf(path,sizeof(path),"%s/%s.%s",ROOT,name,extension);
            bool present=access(path,F_OK)==0;
            if(present)volume_usage(path);
            if(unlink(path)!=0 && errno!=ENOENT) ok=false;
            if(present)volume_usage("after cache unlink");
        }
    }
    if(unlink("/fatfs/probe.tmp")!=0 && errno!=ENOENT) ok=false;
    return ok;
}
static void repair_proof_locked(StorageRepairStatus *out,bool log) {
    *out={};GameJournalInspection journal{};ActiveGameRepairInspection active{};QueueRepairInspection queue{};
    // Independent checks: short-circuiting hides the actual failing guard.
    bool journal_ok=game_journal_inspect_locked(&journal);
    bool active_ok=active_game_inspect_repair_locked(&active);
    bool queue_ok=portal_queue_inspect_repair_locked(&queue);
    s_disk_pending=journal.pending;
    out->pending=journal.pending>g_store.pendingGamesCount?journal.pending:g_store.pendingGamesCount;
    snprintf(out->checks,sizeof(out->checks),
        "Journal: geprüft=%d, offen=%d, unklar=%d\n"
        "Aktives Spiel: Dateien=%d, unlesbar=%d, had_active=%d%s, aktiv=%d, RAM=%d\n"
        "Warteschlange: Dateien=%d, unlesbar=%d, nicht leer=%d, beschäftigt=%d",
        journal_ok,journal.pending,journal.uncertain,active.present,active.unreadable,
        active.had_active,active.stale_latch?" (veraltet, gelöscht)":"",active.active,active.ram_active,
        queue.present,queue.unreadable,queue.non_empty,queue.busy);
    const char *reason="Alle Sicherheitsprüfungen bestanden.";
    if(s_wl==WL_INVALID_HANDLE)reason="FAT ist nicht eingehängt.";
    else if(!journal_ok)reason="Spieljournal nicht prüfbar (uncertain); Reparatur gesperrt.";
    else if(out->pending)reason="Nicht übertragene Spiele im Journal oder RAM.";
    else if(active.unreadable)reason="Aktives Spiel: Datei nicht lesbar oder ungültig.";
    else if(active.active)reason="Aktives Spiel: gespeicherter Spielstand aktiv.";
    else if(active.ram_active)reason=active.had_active?"Aktives Spiel gemerkt (had_active); RAM-Spiel aktiv.":"Aktives Spiel im RAM.";
    else if(!active_ok)reason="Aktives Spiel gemerkt (had_active); nicht freigegeben.";
    else if(queue.unreadable)reason="Warteschlange: Datei nicht lesbar oder ungültig.";
    else if(queue.non_empty)reason="Warteschlange: Reservierung oder Ereignisse offen.";
    else if(queue.busy)reason="Warteschlange: Übernahme läuft noch.";
    else if(!queue_ok)reason="Warteschlange nicht freigegeben.";
    out->allowed=s_wl!=WL_INVALID_HANDLE&&journal_ok&&active_ok&&queue_ok&&!out->pending;
    snprintf(out->reason,sizeof(out->reason),"%s",reason);
    if(log) {
        char line[192];
        snprintf(line,sizeof(line),"FAT proof journal: proven=%d pending=%d uncertain=%d accepted=%d RAM_pending=%d",
            journal_ok,journal.pending,journal.uncertain,journal.accepted,g_store.pendingGamesCount);
        ESP_LOGI(TAG,"%s",line);terminal_log_add("sync","INFO",line);
        snprintf(line,sizeof(line),"FAT proof active: present=%d unreadable=%d had_active=%d active=%d ram_active=%d stale_latch=%d errno=%d",
            active.present,active.unreadable,active.had_active,active.active,active.ram_active,active.stale_latch,active.error);
        ESP_LOGI(TAG,"%s",line);terminal_log_add("sync","INFO",line);
        snprintf(line,sizeof(line),"FAT proof queue: present=%d unreadable=%d non_empty=%d busy=%d errno=%d",
            queue.present,queue.unreadable,queue.non_empty,queue.busy,queue.error);
        ESP_LOGI(TAG,"%s",line);terminal_log_add("sync","INFO",line);
        if(!out->allowed){ESP_LOGW(TAG,"%s",out->reason);terminal_log_add("sync","WARN",out->reason);}
    }
}
static bool recover_cache_locked(const esp_vfs_fat_mount_config_t &cfg,bool *refused=nullptr,bool allow_format=false,bool boot=false) {
    game_journal_storage_lock();
    volume_usage("recovery before proof");
    StorageRepairStatus proof{};repair_proof_locked(&proof,true);
    if(!proof.allowed) {
        if(refused) *refused=true;
        ESP_LOGW(TAG,"FAT cleanup refused: %s; nothing deleted",proof.reason);
        terminal_log_add("sync","WARN",proof.reason);
        game_journal_storage_unlock();return false;
    }
    ESP_LOGW(TAG,"FAT cache-only cleanup: no pending games; journal/NVS untouched (layout marker ignored)");
    terminal_log_add("sync","WARN","Speicherbereinigung: nur Portal-Cache; keine offenen Spiele, Spielarchiv und NVS bleiben erhalten");
    bool ok=remove_cache_files_locked();
    auto usage=volume_usage("after cache cleanup");
    bool needs_format=usage.proven&&usage.total&&
        (usage.lost*CACHE_ALLOCATION_UNIT>usage.total/20 ||
         usage.free<usage.total*STORAGE_FREE_PERCENT/100+2*storage_allocated(game_journal_record_size()));
    bool formatted=false;
    if(ok&&needs_format&&allow_format) {
        if(g_store.pendingKreditEventCount||g_store.pendingVerkaufEventCount||
           g_store.pendingPaymentEventCount||!removable_root_only_locked()) {
            if(refused)*refused=true;
            terminal_log_add("sync","WARN","Speicherreparatur gesperrt: offene Ereignisse oder unbekannte Dateien; nichts formatiert");
            game_journal_storage_unlock();return false;
        }
        Census census{};census_directory("/fatfs",&census,false);
        if(!census.complete) {
            if(refused)*refused=true;
            terminal_log_add("sync","WARN","FAT-Dateiliste unvollständig; nichts formatiert");
            game_journal_storage_unlock();return false;
        }
        if(boot&&!census.files&&usage.lost*CACHE_ALLOCATION_UNIT>usage.total*95/100) {
            ESP_LOGW(TAG,"FAT boot auto-repair: files=0 lost_clusters=%llu (>95%%), all guards proven",
                (unsigned long long)usage.lost);
            terminal_log_add("sync","WARN","Leeres FAT mit >95% verwaisten Clustern: automatische Reparatur nach Einzelprüfungen");
        }
        ok=format_empty_locked(cfg);formatted=ok;
    }
    if(ok&&!formatted) {
        esp_err_t err=esp_vfs_fat_spiflash_unmount_rw_wl("/fatfs",s_wl);
        if(err==ESP_OK) {
            s_wl=WL_INVALID_HANDLE;
            esp_vfs_fat_mount_config_t safe=cfg;safe.format_if_mount_failed=false;
            err=esp_vfs_fat_spiflash_mount_rw_wl("/fatfs","storage",&safe,&s_wl);
        }
        ok=err==ESP_OK;
    }
    if(ok) {
        memset(s_saved_known,0,sizeof(s_saved_known));
        memset(s_attempted,0,sizeof(s_attempted));
        s_failed_sections=0;s_dirty_sections=0;s_metadata_failed=false;g_store.offlineCacheLoaded=false;
        memset(g_store.cacheManifestTokens,0,sizeof(g_store.cacheManifestTokens));
        g_store.cacheManifestDailyDate[0]=0;
    }
    volume_usage("after recovery");
    game_journal_storage_unlock();
    return ok;
}
static bool count_valid(OfflineCacheSection s,int count) {
    if(s==OFFLINE_CACHE_ROSTER)return count>=0&&count<=MAX_PORTAL_SPIELER;
    if(s==OFFLINE_CACHE_PRODUCTS)return count>=0&&count<=MAX_PRODUKTE;
    if(s==OFFLINE_CACHE_HISTORY)return count>=0&&count<=MAX_HISTORY;
    return false;
}
#ifdef TM_OFFLINE_CACHE_TEST_HOOKS
bool offline_cache_test_count_valid(OfflineCacheSection s,int count){return count_valid(s,count);}
#endif
static bool valid(OfflineCacheSection s, const void *p) {
    if (!p) return false;
    if (s == OFFLINE_CACHE_ROSTER) return count_valid(s,((const RosterPayload *)p)->count);
    if (s == OFFLINE_CACHE_PRODUCTS) return count_valid(s,((const ProductsPayload *)p)->count);
    if (s == OFFLINE_CACHE_HISTORY) return count_valid(s,((const HistoryPayload *)p)->count);
    if (s == OFFLINE_CACHE_BILLS) { const BillDaySummary *b=(const BillDaySummary *)p; return b->playerCount>=0 && b->playerCount<=MAX_DAY_BILLS && b->categoryCount>=0 && b->categoryCount<=MAX_BILL_CATEGORIES && b->productCount>=0 && b->productCount<=MAX_DAY_PRODUCTS; }
    return true;
}
static void snapshot(OfflineCacheSection s, void *p) {
    switch (s) {
    case OFFLINE_CACHE_ROSTER: { RosterPayload *x=(RosterPayload *)p; x->count=g_store.portalSpielerCount; memcpy(x->value,g_store.portalSpieler,sizeof(x->value)); break; }
    case OFFLINE_CACHE_PRODUCTS: { ProductsPayload *x=(ProductsPayload *)p; x->count=g_store.produkteCount; memcpy(x->value,g_store.produkte,sizeof(x->value)); break; }
    case OFFLINE_CACHE_HISTORY: { HistoryPayload *x=(HistoryPayload *)p; x->count=g_store.historyCount; memcpy(x->value,g_store.history,sizeof(x->value)); break; }
    case OFFLINE_CACHE_CREDITS: { CreditsPayload *x=(CreditsPayload *)p; memcpy(x->date,g_store.kreditDatum,sizeof(x->date)); memcpy(x->ids,g_store.kreditPlayerIds,sizeof(x->ids)); memcpy(x->values,g_store.kredite,sizeof(x->values)); break; }
    case OFFLINE_CACHE_SALES: { SalesPayload *x=(SalesPayload *)p; memcpy(x->date,g_store.verkaufDatum,sizeof(x->date)); memcpy(x->values,g_store.munition,sizeof(x->values)); x->cal12=g_store.verkaufCal12Total; x->cal20=g_store.verkaufCal20Total; break; }
    case OFFLINE_CACHE_BILLS: memcpy(p,&g_store.billDayBaseline,sizeof(g_store.billDayBaseline)); break; default: break;
    }
}
static bool write_all(int fd, const void *p, size_t n, const char *name,
                      const char *part) {
    const uint8_t *b = (const uint8_t *)p;
    while (n) {
        errno = 0;
        ssize_t r = write(fd, b, n>16384?16384:n);
        const int write_errno = errno;
        if (r < 0 && write_errno == EINTR) continue;
        if (r <= 0) {
            ESP_LOGE(TAG, "Cache %s write(%s) failed: ret=%ld remaining=%u errno=%d",
                     name, part, (long)r, (unsigned)n, write_errno);
            return false;
        }
        b += r;
        n -= (size_t)r;
        if(fsync(fd)!=0) {
            ESP_LOGE(TAG,"Cache %s fsync failed: ret=-1 errno=%d",name,errno);
            return false;
        }
    }
    return true;
}
static bool read_all(int fd,void *p,size_t n) { uint8_t *b=(uint8_t *)p; while(n){ ssize_t r=read(fd,b,n); if(r<0&&errno==EINTR)continue; if(r<=0)return false; b+=r;n-=(size_t)r;} return true; }
static void sync_cache_directory(void) {
    errno = 0;
    const int fd = open(ROOT, O_RDONLY);
    const int open_errno = errno;
    if (fd < 0) {
        // FatFs may not support directory descriptors; file fsync + f_rename
        // still flushes its filesystem metadata.
        ESP_LOGD(TAG, "Cache directory open unavailable: errno=%d", open_errno);
        return;
    }
    errno = 0;
    const int sync_result = fsync(fd);
    const int sync_errno = errno;
    if (sync_result != 0 && sync_errno != EINVAL && sync_errno != ENOTSUP)
        ESP_LOGW(TAG, "Cache directory fsync failed: ret=%d errno=%d",
                 sync_result, sync_errno);
    errno = 0;
    const int close_result = close(fd);
    const int close_errno = errno;
    if (close_result != 0)
        ESP_LOGW(TAG, "Cache directory close failed: ret=%d errno=%d",
                 close_result, close_errno);
}

static bool write_file(const char *name,uint16_t section,const void *p,size_t n) {
    char path[64],tmp[64],backup[64];
    snprintf(path,sizeof(path),"%s/%s.bin",ROOT,name);
    snprintf(tmp,sizeof(tmp),"%s/%s.tmp",ROOT,name);
    snprintf(backup,sizeof(backup),"%s/%s.bak",ROOT,name);
    // Snapshots are rebuildable. Drop the previous section, never hold old +
    // new full copies. A failed write removes its partial temp immediately.
    const char *obsolete[]={tmp,backup,path};
    for(const char *file:obsolete)
        if(unlink(file)!=0 && errno!=ENOENT) return false;
    uint64_t total=0,free_bytes=0;
    Census used;census_directory(ROOT,&used,false);
    const size_t allocation=storage_allocated(sizeof(CacheEnvelope)+n);
    if(esp_vfs_fat_info("/fatfs",&total,&free_bytes)!=ESP_OK || !used.complete ||
       cache_budget_bytes()>total*STORAGE_CACHE_PERCENT/100 ||
       used.allocated+allocation>total*STORAGE_CACHE_PERCENT/100 ||
       free_bytes<total*STORAGE_FREE_PERCENT/100+
           storage_allocated(game_journal_record_size())*2+allocation) {
        ESP_LOGW(TAG,"Cache %s skipped: 25%% cache / 20%% free budget",name);return false;
    }
    errno = 0;
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0644);
    const int open_errno = errno;
    if(fd<0){ESP_LOGE(TAG,"Cache %s open failed: errno=%d",name,open_errno);return false;}
    CacheEnvelope h={CACHE_MAGIC,(uint16_t)(section==OFFLINE_CACHE_ROSTER?2:CACHE_SCHEMA),section,(uint32_t)n,esp_rom_crc32_le(0,(const uint8_t *)p,n)};
    bool ok = write_all(fd, &h, sizeof(h), name, "header") &&
              write_all(fd, p, n, name, "payload");
    if (ok) {
        errno = 0;
        const int result = fsync(fd);
        const int sync_errno = errno;
        if (result != 0) {
            ESP_LOGE(TAG, "Cache %s fsync failed: ret=%d errno=%d",
                     name, result, sync_errno);
            ok = false;
        }
    }
    errno = 0;
    const int close_result = close(fd);
    const int close_errno = errno;
    if (close_result != 0) {
        ESP_LOGE(TAG, "Cache %s close failed: ret=%d errno=%d",
                 name, close_result, close_errno);
        ok = false;
    }
    if(!ok){unlink(tmp);return false;}

    errno = 0;
    const int install_result = rename(tmp, path);
    const int install_errno = errno;
    if(install_result!=0){
        ESP_LOGE(TAG,"Cache %s rename-install failed: errno=%d",name,install_errno);
        unlink(tmp);
        return false;
    }
    sync_cache_directory();
    return true;
}
static bool read_envelope(const char *path,uint16_t section,void *p,size_t n) {
    int fd=open(path,O_RDONLY); if(fd<0)return false;
    CacheEnvelope h;
    bool ok=read_all(fd,&h,sizeof(h))&&h.magic==CACHE_MAGIC&&
        (h.schema==CACHE_SCHEMA||(section==OFFLINE_CACHE_ROSTER&&h.schema==2))&&h.section==section;
    bool legacy_bill=ok&&section==OFFLINE_CACHE_BILLS&&h.schema==1&&h.payload_len==offsetof(BillDaySummary,ammoSold);
    if(ok&&(h.payload_len==n||legacy_bill)){
        memset(p,0,n);ok=read_all(fd,p,h.payload_len)&&esp_rom_crc32_le(0,(const uint8_t*)p,h.payload_len)==h.crc32;
    }else ok=false;
    // Legacy roster fields occupied padding: never interpret those bytes as
    // authorization flags. Upgrade just this payload without discarding others.
    if(ok&&section==OFFLINE_CACHE_ROSTER&&h.schema==1){
        RosterPayload *roster=(RosterPayload*)p;
        for(int i=0;i<MAX_PORTAL_SPIELER;i++){roster->value[i].isMaitreDeTir=false;roster->value[i].aktiv=true;}
    }
    close(fd);
    return ok;
}
static bool read_file(const char *name,uint16_t section,void *p,size_t n) {
    char path[64],backup[64];
    snprintf(path,sizeof(path),"%s/%s.bin",ROOT,name);
    snprintf(backup,sizeof(backup),"%s/%s.bak",ROOT,name);
    if(read_envelope(path,section,p,n))return true;
    if(!read_envelope(backup,section,p,n)) {
        // A missing single-copy snapshot must invalidate its old manifest
        // token after reboot, otherwise a matching server revision skips repair.
        if(section<OFFLINE_CACHE_SECTION_COUNT) s_failed_sections|=1u<<section;
        else s_metadata_failed=true;
        return false;
    }
    // Recover a backup left by power loss between the two rename operations.
    unlink(path);
    if(rename(backup,path)!=0)
        ESP_LOGW(TAG,"Loaded backup for %s but could not restore it: errno=%d",name,errno);
    return true;
}
static bool cache_self_test(void) {
    const char *path = "/fatfs/probe.tmp"; // 8.3 name, also works without LFN
    const uint8_t bytes[16] = {0};
    errno = 0;
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const int open_errno = errno;
    ESP_LOGI(TAG, "FAT self-test open: fd=%d errno=%d", fd, open_errno);
    if (fd < 0) return false;
    errno = 0;
    const ssize_t written = write(fd, bytes, sizeof(bytes));
    const int write_errno = errno;
    ESP_LOGI(TAG, "FAT self-test write(16): ret=%ld errno=%d",
             (long)written, write_errno);
    errno = 0;
    const int synced = fsync(fd);
    const int sync_errno = errno;
    ESP_LOGI(TAG, "FAT self-test fsync: ret=%d errno=%d", synced, sync_errno);
    errno = 0;
    const int closed = close(fd);
    const int close_errno = errno;
    ESP_LOGI(TAG, "FAT self-test close: ret=%d errno=%d", closed, close_errno);
    errno = 0;
    const int removed = unlink(path);
    const int unlink_errno = errno;
    if (removed != 0)
        ESP_LOGW(TAG, "FAT self-test cleanup: ret=%d errno=%d", removed, unlink_errno);
    return written == (ssize_t)sizeof(bytes) && synced == 0 && closed == 0;
}

static bool inspect_volume(bool *bad_capacity) {
    uint64_t total = 0, free_bytes = 0;
    const esp_err_t info_err = esp_vfs_fat_info("/fatfs", &total, &free_bytes);
    ESP_LOGI(TAG, "FAT info: result=%s total=%llu free=%llu bytes",
             esp_err_to_name(info_err), (unsigned long long)total,
             (unsigned long long)free_bytes);
    // Reserve 20% usable capacity and two journal replacement records. Raw
    // storage size is not usable FAT capacity because of WL/FAT overhead.
    *bad_capacity = info_err == ESP_OK &&
        (total < 1024u * 1024u ||
         storage_allocated(game_journal_record_size())*(STORAGE_JOURNAL_SLOTS+2)>total*STORAGE_JOURNAL_PERCENT/100 ||
         cache_budget_bytes()>total*STORAGE_CACHE_PERCENT/100 ||
         free_bytes < total*STORAGE_FREE_PERCENT/100+storage_allocated(game_journal_record_size())*2 ||
         free_bytes > total);
    const size_t sector = wl_sector_size(s_wl);
    ESP_LOGI(TAG, "FAT geometry: allocation_unit=%u WL_sector=%u configured_WL_sector=%u",
             (unsigned)CACHE_ALLOCATION_UNIT, (unsigned)sector,
             (unsigned)CONFIG_WL_SECTOR_SIZE);
    const bool geometry_ok = sector != 0 && CACHE_ALLOCATION_UNIT >= sector &&
                             CACHE_ALLOCATION_UNIT % sector == 0;
    const bool writable = cache_self_test();
    const auto usage=volume_usage("validation",false);
    const bool lost=usage.proven&&usage.total&&usage.lost*CACHE_ALLOCATION_UNIT>usage.total/20;
    return info_err == ESP_OK && !*bad_capacity && geometry_ok && writable&&!lost;
}

static bool cache_volume_is_empty(void) {
    // Older firmware could set the layout marker after mounting a fresh but
    // unwritable volume. A marker alone therefore does not prove durability.
    // Treat it as fresh only when absence is proven, not on arbitrary I/O errors.
    for (unsigned i = 0; i <= OFFLINE_CACHE_SECTION_COUNT; ++i) {
        const char *name = i < OFFLINE_CACHE_SECTION_COUNT ? names[i] : "meta";
        const char *extensions[] = {"bin", "bak"};
        for (unsigned e = 0; e < 2; ++e) {
            char path[64];
            snprintf(path, sizeof(path), "%s/%s.%s", ROOT, name, extensions[e]);
            errno = 0;
            const int result = access(path, F_OK);
            const int access_errno = errno;
            if (result == 0 || access_errno != ENOENT) return false;
        }
    }
    return true;
}

bool offline_cache_mount(void) {
    lock_cache();
    if (s_mounted) {
        unlock_cache();
        return true;
    }
    if (s_mount_attempted) {
        unlock_cache();
        return false;
    }
    s_mount_attempted = true;

    nvs_handle_t n = 0;
    uint8_t initialized_layout = 0;
    esp_err_t nvs_err = nvs_open("tm_cache", NVS_READWRITE, &n);
    esp_err_t marker_err = nvs_err == ESP_OK
        ? nvs_get_u8(n, "fat_layout", &initialized_layout)
        : nvs_err;
    uint8_t journal_protected=0;
    esp_err_t journal_marker_err=nvs_err==ESP_OK?nvs_get_u8(n,"game_journal",&journal_protected):nvs_err;
    uint8_t repair_intent=0;
    bool resume_repair=nvs_err==ESP_OK&&nvs_get_u8(n,"fat_repair",&repair_intent)==ESP_OK&&
        repair_intent==1&&g_store.pendingGamesCount==0&&!g_store.pendingKreditEventCount&&
        !g_store.pendingVerkaufEventCount&&!g_store.pendingPaymentEventCount;
    s_repair_pending=repair_intent==1;
    const bool may_initialize = !journal_protected && g_store.pendingGamesCount==0 &&
        journal_marker_err==ESP_ERR_NVS_NOT_FOUND && nvs_err == ESP_OK &&
        (marker_err == ESP_ERR_NVS_NOT_FOUND ||
         (marker_err == ESP_OK && initialized_layout == 2u));

    esp_vfs_fat_mount_config_t cfg = {};
    cfg.format_if_mount_failed = may_initialize||resume_repair;
    cfg.max_files = 12;
    cfg.allocation_unit_size = CACHE_ALLOCATION_UNIT;
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    if (partition)
        ESP_LOGI(TAG, "FAT storage: offset=0x%lx size=0x%lx layout=%u initialization_allowed=%d",
                 (unsigned long)partition->address, (unsigned long)partition->size,
                 (unsigned)initialized_layout, (int)may_initialize);
    if (may_initialize)
        ESP_LOGW(TAG, "Fresh/migrating cache: storage may be formatted during initial mount");
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        "/fatfs", "storage", &cfg, &s_wl);

    // "storage" is reserved for this cache. On first initialization or an
    // intentional partition-layout migration, raw flash may contain non-FF
    // residue and still report FR_NO_FILESYSTEM. Once the current layout marker
    // exists, format_if_mount_failed remains false so a damaged established
    // cache is never silently erased. A successful but unusable mount is
    // separately diagnosed and rebuilt below, with an explicit warning.
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FAT unavailable (%s); cache disabled until reboot",
                 esp_err_to_name(err));
        g_store.offlineCacheHealthy = false;
        s_storage_blocked=true;
        if (nvs_err == ESP_OK) nvs_close(n);
        unlock_cache();
        return false;
    }

    log_census_once();
    // These names are exclusively rebuildable snapshots, never game/outbox data.
    game_journal_storage_lock();
    GameJournalInspection startup{};
    bool may_clean_temps=game_journal_inspect_locked(&startup)&&!startup.pending&&
        !g_store.pendingGamesCount&&active_game_repair_safe_locked()&&portal_queue_repair_safe_locked();
    if(may_clean_temps)for(unsigned i=0;i<=OFFLINE_CACHE_SECTION_COUNT;i++) {
        char temp[64];snprintf(temp,sizeof(temp),"%s/%s.tmp",ROOT,i<OFFLINE_CACHE_SECTION_COUNT?names[i]:"meta");
        if(access(temp,F_OK)==0) {
            volume_usage("before stale cache temporary");
            if(unlink(temp)!=0)ESP_LOGW(TAG,"Stale cache temporary retained: errno=%d",errno);
            volume_usage("after stale cache temporary");
        }
    }
    game_journal_storage_unlock();
    if(resume_repair) {
        game_journal_storage_lock();
        bool repaired=format_empty_locked(cfg);
        game_journal_storage_unlock();
        if(!repaired) {
            g_store.offlineCacheHealthy=false;s_storage_blocked=true;
            if(nvs_err==ESP_OK)nvs_close(n);
            unlock_cache();return false;
        }
    }
    const bool empty_cache = cache_volume_is_empty();
    const bool fresh_volume = may_initialize || empty_cache;
    ESP_LOGI(TAG, "FAT validation: empty_cache=%d fresh_volume=%d",
             (int)empty_cache, (int)fresh_volume);
    bool bad_capacity = false;
    bool usable = inspect_volume(&bad_capacity);
    if (!usable && recover_cache_locked(cfg,nullptr,true,true)) usable=inspect_volume(&bad_capacity);
    if (!usable) {
        // Keep a readable mount: journal recovery/upload must work even when
        // optional snapshots or the write probe fail. Pending work forbids format.
        ESP_LOGW(TAG,"FAT cache unavailable; readable journal retained, new games blocked until space is safe");
        s_storage_blocked=true;
        g_store.offlineCacheHealthy = false;
        if (nvs_err == ESP_OK) nvs_close(n);
        unlock_cache();
        return false;
    }
    errno = 0;
    const int mkdir_result = mkdir(ROOT, 0755);
    const int mkdir_errno = errno;
    if (mkdir_result != 0 && mkdir_errno != EEXIST) {
        ESP_LOGW(TAG, "Could not create cache directory: errno=%d", mkdir_errno);
        g_store.offlineCacheHealthy = false;
        esp_vfs_fat_spiflash_unmount_rw_wl("/fatfs", s_wl);
        s_wl = WL_INVALID_HANDLE;
        if (nvs_err == ESP_OK) nvs_close(n);
        unlock_cache();
        return false;
    }
    s_mounted = true;
    s_storage_blocked=false;
    update_health();
    if (nvs_err == ESP_OK) {
        nvs_set_u8(n, "fat_layout", CACHE_FAT_LAYOUT);
        nvs_commit(n);
        nvs_close(n);
    }
    unlock_cache();
    return true;
}
bool offline_cache_storage_available(void) {return s_wl!=WL_INVALID_HANDLE;}
bool offline_cache_storage_writable(void){return s_mounted&&!s_storage_blocked&&!s_repair_pending;}
bool offline_cache_cleanup_busy(void) {return s_cleaning.load();}
void offline_cache_repair_status(StorageRepairStatus *out) {
    lock_cache();
    game_journal_storage_lock();
    if(s_wl==WL_INVALID_HANDLE) {
        // A deliberate user retry may remount read-only-to-the-application for
        // proof even after the automatic attempt was latched. Never format.
        esp_vfs_fat_mount_config_t cfg{};
        cfg.max_files=12;cfg.allocation_unit_size=CACHE_ALLOCATION_UNIT;
        (void)esp_vfs_fat_spiflash_mount_rw_wl("/fatfs","storage",&cfg,&s_wl);
    }
    repair_proof_locked(out,true);
    if(out->allowed&&(g_store.pendingKreditEventCount||g_store.pendingVerkaufEventCount||g_store.pendingPaymentEventCount)) {
        out->allowed=false;snprintf(out->reason,sizeof(out->reason),"Offene Kredit-, Verkauf- oder Zahlungsereignisse; erst synchronisieren.");
    }
    if(out->allowed&&!removable_root_only_locked()) {
        out->allowed=false;snprintf(out->reason,sizeof(out->reason),"Unbekannte oder ungeprüfte Dateien auf FAT.");
    }
    if(out->allowed&&g_store.syncStatus==SYNC_RUNNING) {
        out->allowed=false;snprintf(out->reason,sizeof(out->reason),"Synchronisierung läuft; bitte zuerst beenden lassen.");
    }
    if(!out->allowed){ESP_LOGW(TAG,"%s",out->reason);terminal_log_add("sync","WARN",out->reason);}
    game_journal_storage_unlock();unlock_cache();
}
int offline_cache_pending_games(void) {
    StorageRepairStatus proof{};offline_cache_repair_status(&proof);
    return proof.allowed||proof.pending>0?proof.pending:-1;
}
bool offline_cache_storage_warning(char *out,size_t size) {
    if(!s_storage_blocked && !s_cleaning) return false;
    if(s_cleaning) snprintf(out,size,"Speicher wird geprüft und bereinigt. Bitte warten.");
    else if(s_disk_pending>0 || g_store.pendingGamesCount>0)
        snprintf(out,size,"Speicher nicht beschreibbar. Offene Spiele bleiben erhalten. Erst mit dem Portal synchronisieren; Bereinigung gesperrt.");
    else snprintf(out,size,"Spielarchiv nicht verfügbar. Einstellungen > System > Speicher reparieren. Offene oder ungeprüfte Daten werden niemals gelöscht.");
    return true;
}
bool offline_cache_recheck(void) {
    lock_cache();
    bool bad=false;
    bool ok=s_wl!=WL_INVALID_HANDLE && inspect_volume(&bad);
    if(ok) {int result=mkdir(ROOT,0755);ok=result==0 || errno==EEXIST;}
    s_mounted=ok;s_storage_blocked=!ok;update_health();
    unlock_cache();return ok;
}
static bool cleanup_storage(bool repair) {
    if(s_cleaning.exchange(true)) return false;
    lock_cache();
    const bool was_mounted=s_mounted,was_blocked=s_storage_blocked.load();
    if(g_store.pendingGamesCount>0 || g_store.syncStatus==SYNC_RUNNING) {
        unlock_cache();s_cleaning=false;
        terminal_log_add("sync","WARN","Speicherbereinigung gesperrt: offene Spiele oder Synchronisierung");
        return false;
    }
    // Never remount underneath an active sync; the UI worker rechecks here.
    bool ok=g_store.pendingGamesCount==0 && g_store.syncStatus!=SYNC_RUNNING;
    esp_vfs_fat_mount_config_t cfg{};
    cfg.max_files=12;cfg.allocation_unit_size=CACHE_ALLOCATION_UNIT;
    if(ok && s_wl==WL_INVALID_HANDLE)
        ok=esp_vfs_fat_spiflash_mount_rw_wl("/fatfs","storage",&cfg,&s_wl)==ESP_OK;
    bool refused=false;
    if(ok) ok=recover_cache_locked(cfg,&refused,repair);
    if(refused) {
        s_mounted=was_mounted;s_storage_blocked=was_blocked;update_health();
        unlock_cache();s_cleaning=false;return false;
    }
    if(ok) {
        bool bad=false;ok=inspect_volume(&bad);
        if(ok) {int result=mkdir(ROOT,0755);ok=result==0 || errno==EEXIST;}
    }
    s_mounted=ok;s_storage_blocked=!ok;update_health();
    unlock_cache();
    // Read-only journal revalidation: cache cleanup itself must NEVER prune,
    // recover, rename or rewrite even accepted journal records.
    if(ok) ok=game_journal_resume_after_cleanup();
    if(!ok) s_storage_blocked=true;
    s_cleaning=false;
    if(!ok) terminal_log_add("sync","WARN","Speicherbereinigung abgelehnt oder fehlgeschlagen; offene/ungeprüfte Spiele und NVS bleiben erhalten");
    return ok;
}
bool offline_cache_cleanup(void){return cleanup_storage(false);}
bool offline_cache_repair(void){return cleanup_storage(true);}
bool offline_cache_save(OfflineCacheSection s) {
    size_t n = payload_size(s);
    if (!n) return false;
    const bool mounted = offline_cache_mount();
    void *p = mounted ? heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (mounted && !p) ESP_LOGE(TAG, "OOM saving cache section %d", (int)s);
    if(p) {
        bool acquired=false;
        if(!ui_manager_snapshot_lock(&acquired)) {
            // Busy input/UI is a deferral, not a media failure.
            lock_cache();s_dirty_sections|=1u<<s;unlock_cache();
            heap_caps_free(p);return true;
        }
        memset(p,0,n);snapshot(s,p);
        if(acquired)ui_manager_unlock();
    }
    lock_cache();
    bool ok = false,deferred=false;
    if (p) {
        const uint32_t hash=esp_rom_crc32_le(0,(const uint8_t*)p,n);
        const int64_t now=esp_timer_get_time();
        if(valid(s,p)) {
            if(s_saved_known[s]&&s_saved_hash[s]==hash)ok=true;
            else if(!s_attempted[s]||now-s_saved_at[s]>=300000000LL) {
                s_attempted[s]=true;s_saved_at[s]=now;s_saved_known[s]=false;
                ok=write_file(names[s],s,p,n);
                if(ok){s_saved_known[s]=true;s_saved_hash[s]=hash;s_saved_at[s]=now;}
            } else {
                deferred=true;ok=true;s_dirty_sections|=1u<<s;
                ESP_LOGD(TAG,"Cache %s deferred: content changed, five-minute write interval",names[s]);
            }
        }
    }
    if (ok && !deferred) {
        s_dirty_sections &= ~(1u << s);
        s_failed_sections &= ~(1u << s);
        g_store.offlineCacheLoaded = true;
    } else if(!ok) {
        s_dirty_sections |= 1u << s;
        s_failed_sections |= 1u << s;
    }
    update_health();
    unlock_cache();
    if (p) heap_caps_free(p);
    return ok;
}
void offline_cache_flush_pending() {
    // Called after publication by the same sync worker, even for an unchanged
    // manifest. Re-snapshot current RAM; do not retain widget/data pointers.
    for(unsigned i=0;i<OFFLINE_CACHE_SECTION_COUNT;i++) {
        lock_cache();
        bool due=(s_dirty_sections&(1u<<i)) &&
            esp_timer_get_time()-s_saved_at[i]>=300000000LL;
        unlock_cache();
        if(due)(void)offline_cache_save((OfflineCacheSection)i);
    }
}
bool offline_cache_save_metadata(void) {
    if (!s_mounted && !offline_cache_mount()) {
        lock_cache();
        s_metadata_failed = true;
        update_health();
        unlock_cache();
        return false;
    }
    MetaPayload *m = (MetaPayload *)heap_caps_malloc(
        sizeof(*m), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m) {
        lock_cache();
        s_metadata_failed = true;
        update_health();
        unlock_cache();
        return false;
    }
    lock_cache();
    memset(m, 0, sizeof(*m));
    m->lastSync = g_store.lastSuccessfulSyncAt;
    m->health = s_mounted && s_failed_sections == 0;
    m->loaded = g_store.offlineCacheLoaded;
    memcpy(m->dailyDate, g_store.cacheManifestDailyDate, sizeof(m->dailyDate));
    memcpy(m->tokens, g_store.cacheManifestTokens, sizeof(m->tokens));
    // Deferred RAM data and its new token are not yet durable together.
    // An empty persisted token forces a pull after reboot, never a false hit.
    for(unsigned i=0;i<OFFLINE_CACHE_SECTION_COUNT;i++)
        if(s_dirty_sections&(1u<<i))m->tokens[i][0]=0;
    const bool ok = write_file("meta", 0xffff, m, sizeof(*m));
    s_metadata_failed = !ok;
    update_health();
    unlock_cache();heap_caps_free(m);return ok;
}
bool offline_cache_set_manifest_token(OfflineCacheSection s,const char *token) {
    if((unsigned)s>=OFFLINE_CACHE_SECTION_COUNT||!token||strlen(token)>=CACHE_MANIFEST_TOKEN_LEN)return false;
    lock_cache();
    const bool snapshot_available = s_mounted && !(s_failed_sections & (1u << s));
    unlock_cache();
    if (!snapshot_available) return false;
    char old[CACHE_MANIFEST_TOKEN_LEN], old_date[11]; lock_cache(); memcpy(old,g_store.cacheManifestTokens[s],sizeof(old));memcpy(old_date,g_store.cacheManifestDailyDate,sizeof(old_date));snprintf(g_store.cacheManifestTokens[s],CACHE_MANIFEST_TOKEN_LEN,"%s",token); if(s>=OFFLINE_CACHE_CREDITS){time_t t=time(NULL);struct tm x;localtime_r(&t,&x);strftime(g_store.cacheManifestDailyDate,sizeof(g_store.cacheManifestDailyDate),"%Y-%m-%d",&x);} unlock_cache();
    if (offline_cache_save_metadata()) {
        return true;
    }
    lock_cache();
    memcpy(g_store.cacheManifestTokens[s], old, sizeof(old));
    memcpy(g_store.cacheManifestDailyDate, old_date, sizeof(old_date));
    unlock_cache();
    return false;
}
static bool today(const char *d) { time_t t=time(NULL);struct tm x;char now[11];localtime_r(&t,&x);strftime(now,sizeof(now),"%Y-%m-%d",&x);return d&&strcmp(d,now)==0; }
void offline_cache_load(void) {
    if (!offline_cache_mount()) {
        return;
    }
    bool any = false;
    MetaPayload *m=(MetaPayload *)heap_caps_malloc(sizeof(*m),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(m&&read_file("meta",0xffff,m,sizeof(*m))){g_store.lastSuccessfulSyncAt=m->lastSync;memcpy(g_store.cacheManifestDailyDate,m->dailyDate,sizeof(m->dailyDate));memcpy(g_store.cacheManifestTokens,m->tokens,sizeof(m->tokens));} if(m)heap_caps_free(m);
    bool roster_restored=false;
    for(int i=0;i<OFFLINE_CACHE_SECTION_COUNT;i++){size_t n=payload_size((OfflineCacheSection)i);void *p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!p){ESP_LOGW(TAG,"OOM loading cache section %d",i);continue;}if(!read_file(names[i],i,p,n)||!valid((OfflineCacheSection)i,p)){heap_caps_free(p);continue;} if(i==0){RosterPayload*x=(RosterPayload*)p;memcpy(g_store.portalSpieler,x->value,sizeof(x->value));g_store.portalSpielerCount=x->count;roster_restored=true;}else if(i==1){ProductsPayload*x=(ProductsPayload*)p;memcpy(g_store.produkte,x->value,sizeof(x->value));g_store.produkteCount=x->count;}else if(i==2){HistoryPayload*x=(HistoryPayload*)p;memcpy(g_store.history,x->value,sizeof(x->value));g_store.historyCount=x->count;}else if(i==3&&!g_store.pendingKreditEventCount&&today(((CreditsPayload*)p)->date)){CreditsPayload*x=(CreditsPayload*)p;memcpy(g_store.kreditDatum,x->date,sizeof(x->date));memcpy(g_store.kreditPlayerIds,x->ids,sizeof(x->ids));memcpy(g_store.kredite,x->values,sizeof(x->values));}else if(i==4&&!g_store.pendingVerkaufEventCount&&today(((SalesPayload*)p)->date)){SalesPayload*x=(SalesPayload*)p;memcpy(g_store.verkaufDatum,x->date,sizeof(x->date));memcpy(g_store.munition,x->values,sizeof(x->values));g_store.verkaufCal12Total=x->cal12;g_store.verkaufCal20Total=x->cal20;}else if(i==5&&today(((BillDaySummary*)p)->datum)){memcpy(&g_store.billDayBaseline,p,n);store_rebuild_bill_projection();} any=true;heap_caps_free(p);}
    // Restoration above is deliberately side-effect-free: no section writer
    // runs until every independent envelope has been considered.
    if(roster_restored)store_reconcile_lineup_after_cache_load();
    g_store.offlineCacheLoaded=any;
    lock_cache(); update_health(); unlock_cache();
}