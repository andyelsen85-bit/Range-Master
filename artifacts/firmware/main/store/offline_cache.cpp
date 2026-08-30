#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "game_store.h"
#include "offline_cache.h"

static const char *TAG = "offline_cache";
static const char *ROOT = "/fatfs/tmcache";
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static SemaphoreHandle_t s_mutex;
static bool s_mounted;
static bool s_mount_attempted;
#define CACHE_MAGIC 0x544D4348u
#define CACHE_SCHEMA 1u
// Increment only when the FAT partition moves. A layout migration may format
// the new partition once; an established partition must never be auto-formatted.
#define CACHE_FAT_LAYOUT 2u
typedef struct { uint32_t magic; uint16_t schema, section; uint32_t payload_len, crc32; } CacheEnvelope;
static_assert(sizeof(CacheEnvelope) == 16, "stable cache envelope");
typedef struct { int count; PortalSpieler value[MAX_PORTAL_SPIELER]; } RosterPayload;
typedef struct { int count; Produkt value[MAX_PRODUKTE]; } ProductsPayload;
typedef struct { int count; FinishedGame value[MAX_HISTORY]; } HistoryPayload;
typedef struct { char date[11]; int ids[MAX_PORTAL_SPIELER]; KreditStand values[MAX_PORTAL_SPIELER]; } CreditsPayload;
typedef struct { char date[11]; MunitionStand values[MAX_PORTAL_SPIELER]; int32_t cal12, cal20; } SalesPayload;
typedef struct { int64_t lastSync; bool health, loaded; char dailyDate[11]; char tokens[OFFLINE_CACHE_SECTION_COUNT][65]; } MetaPayload;
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
    case OFFLINE_CACHE_BILLS: memcpy(p,&g_store.billDay,sizeof(g_store.billDay)); break; default: break;
    }
}
static bool write_all(int fd,const void *p,size_t n) { const uint8_t *b=(const uint8_t *)p; while(n){ ssize_t r=write(fd,b,n); if(r<0&&errno==EINTR)continue; if(r<=0)return false; b+=r;n-=(size_t)r;} return true; }
static bool read_all(int fd,void *p,size_t n) { uint8_t *b=(uint8_t *)p; while(n){ ssize_t r=read(fd,b,n); if(r<0&&errno==EINTR)continue; if(r<=0)return false; b+=r;n-=(size_t)r;} return true; }
static bool write_file(const char *name,uint16_t section,const void *p,size_t n) {
    char path[64],tmp[64],backup[64];
    snprintf(path,sizeof(path),"%s/%s.bin",ROOT,name);
    snprintf(tmp,sizeof(tmp),"%s/%s.tmp",ROOT,name);
    snprintf(backup,sizeof(backup),"%s/%s.bak",ROOT,name);
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd<0){ESP_LOGE(TAG,"Could not open cache temp file %s: errno=%d",name,errno);return false;}
    CacheEnvelope h={CACHE_MAGIC,CACHE_SCHEMA,section,(uint32_t)n,esp_rom_crc32_le(0,(const uint8_t *)p,n)};
    bool ok=write_all(fd,&h,sizeof(h))&&write_all(fd,p,n)&&fsync(fd)==0; if(close(fd)<0)ok=false;
    if(!ok){ESP_LOGE(TAG,"Could not write cache temp file %s: errno=%d",name,errno);unlink(tmp);return false;}

    // FatFs rename does not replace an existing destination. Preserve the old
    // envelope as a backup while swapping in the fully-fsynced temp file.
    unlink(backup);
    bool had_old=access(path,F_OK)==0;
    if(had_old&&rename(path,backup)!=0){
        ESP_LOGE(TAG,"Could not stage old cache file %s: errno=%d",name,errno);
        unlink(tmp);
        return false;
    }
    if(rename(tmp,path)!=0){
        ESP_LOGE(TAG,"Could not install cache file %s: errno=%d",name,errno);
        if(had_old)rename(backup,path);
        unlink(tmp);
        return false;
    }
    int dir=open(ROOT,O_RDONLY);
    if(dir>=0){
        if(fsync(dir)!=0&&errno!=EINVAL&&errno!=ENOTSUP)
            ESP_LOGW(TAG,"cache directory fsync failed");
        close(dir);
    }
    if(had_old){
        unlink(backup);
        dir=open(ROOT,O_RDONLY);
        if(dir>=0){
            if(fsync(dir)!=0&&errno!=EINVAL&&errno!=ENOTSUP)
                ESP_LOGW(TAG,"cache directory fsync failed");
            close(dir);
        }
    }
    return true;
}
static bool read_envelope(const char *path,uint16_t section,void *p,size_t n) {
    int fd=open(path,O_RDONLY); if(fd<0)return false;
    CacheEnvelope h;
    bool ok=read_all(fd,&h,sizeof(h))&&h.magic==CACHE_MAGIC&&h.schema==CACHE_SCHEMA&&h.section==section&&h.payload_len==n&&read_all(fd,p,n)&&esp_rom_crc32_le(0,(const uint8_t *)p,n)==h.crc32;
    close(fd);
    return ok;
}
static bool read_file(const char *name,uint16_t section,void *p,size_t n) {
    char path[64],backup[64];
    snprintf(path,sizeof(path),"%s/%s.bin",ROOT,name);
    snprintf(backup,sizeof(backup),"%s/%s.bak",ROOT,name);
    if(read_envelope(path,section,p,n))return true;
    if(!read_envelope(backup,section,p,n))return false;
    // Recover a backup left by power loss between the two rename operations.
    unlink(path);
    if(rename(backup,path)!=0)
        ESP_LOGW(TAG,"Loaded backup for %s but could not restore it: errno=%d",name,errno);
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
    const bool may_initialize = nvs_err == ESP_OK &&
        (marker_err == ESP_ERR_NVS_NOT_FOUND ||
         (marker_err == ESP_OK && initialized_layout != CACHE_FAT_LAYOUT));

    esp_vfs_fat_mount_config_t cfg = {};
    cfg.format_if_mount_failed = may_initialize;
    cfg.max_files = 12;
    cfg.allocation_unit_size = 4096;
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        "/fatfs", "storage", &cfg, &s_wl);

    // "storage" is reserved for this cache. On first initialization or an
    // intentional partition-layout migration, raw flash may contain non-FF
    // residue and still report FR_NO_FILESYSTEM. Once the current layout marker
    // exists, format_if_mount_failed remains false so a damaged established
    // cache is never silently erased.
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FAT unavailable (%s); cache disabled until reboot",
                 esp_err_to_name(err));
        g_store.offlineCacheHealthy = false;
        if (nvs_err == ESP_OK) nvs_close(n);
        unlock_cache();
        return false;
    }

    s_mounted = true;
    if (mkdir(ROOT, 0755) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "Could not create cache directory: errno=%d", errno);
    }
    if (nvs_err == ESP_OK) {
        nvs_set_u8(n, "fat_layout", CACHE_FAT_LAYOUT);
        nvs_commit(n);
        nvs_close(n);
    }
    unlock_cache();
    return true;
}
bool offline_cache_save(OfflineCacheSection s) {
    size_t n=payload_size(s); if(!s_mounted&&!offline_cache_mount())return false; if(!n)return false;
    void *p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); if(!p){ESP_LOGE(TAG,"OOM saving cache section %d",(int)s);return false;}
    lock_cache(); snapshot(s,p); bool ok=valid(s,p)&&write_file(names[s],s,p,n); if(ok){g_store.offlineCacheHealthy=true;g_store.offlineCacheLoaded=true;} unlock_cache(); heap_caps_free(p); return ok;
}
bool offline_cache_save_metadata(void) {
    if (!s_mounted && !offline_cache_mount()) {
        return false;
    }
    MetaPayload *m = (MetaPayload *)heap_caps_malloc(
        sizeof(*m), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m) {
        return false;
    }
    lock_cache(); memset(m,0,sizeof(*m));m->lastSync=g_store.lastSuccessfulSyncAt;m->health=g_store.offlineCacheHealthy;m->loaded=g_store.offlineCacheLoaded;memcpy(m->dailyDate,g_store.cacheManifestDailyDate,sizeof(m->dailyDate));memcpy(m->tokens,g_store.cacheManifestTokens,sizeof(m->tokens));bool ok=write_file("meta",0xffff,m,sizeof(*m));unlock_cache();heap_caps_free(m);return ok;
}
bool offline_cache_set_manifest_token(OfflineCacheSection s,const char *token) {
    if(s>=OFFLINE_CACHE_SECTION_COUNT||!token||strlen(token)>=CACHE_MANIFEST_TOKEN_LEN)return false;
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
    for(int i=0;i<OFFLINE_CACHE_SECTION_COUNT;i++){size_t n=payload_size((OfflineCacheSection)i);void *p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!p){ESP_LOGW(TAG,"OOM loading cache section %d",i);continue;}if(!read_file(names[i],i,p,n)||!valid((OfflineCacheSection)i,p)){heap_caps_free(p);continue;} if(i==0){RosterPayload*x=(RosterPayload*)p;memcpy(g_store.portalSpieler,x->value,sizeof(x->value));g_store.portalSpielerCount=x->count;roster_restored=true;}else if(i==1){ProductsPayload*x=(ProductsPayload*)p;memcpy(g_store.produkte,x->value,sizeof(x->value));g_store.produkteCount=x->count;}else if(i==2){HistoryPayload*x=(HistoryPayload*)p;memcpy(g_store.history,x->value,sizeof(x->value));g_store.historyCount=x->count;}else if(i==3&&!g_store.pendingKreditEventCount&&today(((CreditsPayload*)p)->date)){CreditsPayload*x=(CreditsPayload*)p;memcpy(g_store.kreditDatum,x->date,sizeof(x->date));memcpy(g_store.kreditPlayerIds,x->ids,sizeof(x->ids));memcpy(g_store.kredite,x->values,sizeof(x->values));}else if(i==4&&!g_store.pendingVerkaufEventCount&&today(((SalesPayload*)p)->date)){SalesPayload*x=(SalesPayload*)p;memcpy(g_store.verkaufDatum,x->date,sizeof(x->date));memcpy(g_store.munition,x->values,sizeof(x->values));g_store.verkaufCal12Total=x->cal12;g_store.verkaufCal20Total=x->cal20;}else if(i==5&&today(((BillDaySummary*)p)->datum))memcpy(&g_store.billDay,p,n); any=true;heap_caps_free(p);}
    // Restoration above is deliberately side-effect-free: no section writer
    // runs until every independent envelope has been considered.
    if(roster_restored)store_reconcile_lineup_after_cache_load();
    g_store.offlineCacheLoaded=any;g_store.offlineCacheHealthy=true;
}