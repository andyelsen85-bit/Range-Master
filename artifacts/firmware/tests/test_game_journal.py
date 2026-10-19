"""Compile the actual persistence modules with a POSIX SDK shim.

These are storage/restart tests, not ESP32 timing or physical power-cut proof.
"""
import pathlib
import subprocess
import tempfile
import textwrap
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1] / "main" / "store"

def production_function(path, signature):
    source=path.read_text()
    start=source.index(signature)
    end=source.index("{",start)+1
    depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}")
        end+=1
    return source[start:end]


class DurableGames(unittest.TestCase):
    def test_full_modules_storage_and_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            for folder in ("sdk/freertos", "volume", "nvs"):
                (temp / folder).mkdir(parents=True)
            headers = {
                "portal_runtime.h": '#pragma once\n#include "storage_repair.h"\ninline bool portal_queue_repair_safe_locked(){return true;}\ninline bool portal_queue_inspect_repair_locked(QueueRepairInspection *out){*out={};return true;}\n',
                "ui_manager.h": """
                    #pragma once
                    inline bool ui_manager_snapshot_lock(bool *acquired){*acquired=false;return true;}
                    inline void ui_manager_unlock(){}
                """,
                "esp_vfs_fat.h": """
                    #pragma once
                    #include <cassert>
                    #include "esp_err.h"
                    #include <sys/statvfs.h>
                    #include <cstdint>
                    #include <cstdio>
                    #include <cerrno>
                    #include <unistd.h>
                    #include <filesystem>
                    typedef int wl_handle_t;
                    constexpr int WL_INVALID_HANDLE=-1;
                    struct esp_vfs_fat_mount_config_t {bool format_if_mount_failed=false;int max_files=0;size_t allocation_unit_size=0;};
                    inline int esp_vfs_fat_spiflash_mount_rw_wl(const char*,const char*,const esp_vfs_fat_mount_config_t*,int*h){*h=1;return 0;}
                    inline int esp_vfs_fat_spiflash_unmount_rw_wl(const char*,int){return 0;}
                    inline int esp_vfs_fat_spiflash_format_cfg_rw_wl(const char*,const char*,esp_vfs_fat_mount_config_t*){return -1;}
                    inline int fat_rename(const char*a,const char*b) {
                        if(access(b,F_OK)==0) {errno=EEXIST;return -1;}
                        return std::rename(a,b);
                    }
                    inline bool fat_open_fail=false;
                    inline int fat_open_attempts=0;
                    inline FILE *fat_fopen(const char *path,const char *mode) {
                        if(!strcmp(mode,"wb")) {
                            const char *name=strrchr(path,'/');name=name?name+1:path;
                            const char *dot=strchr(name,'.');
                            bool short_name=dot&&dot>name&&(dot-name)<=8&&strlen(dot+1)<=3&&
                                !strchr(dot+1,'.')&&strspn(name,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-")==strlen(name);
                            if(!short_name){errno=EINVAL;assert(!"FatFs create is not 8.3");return nullptr;}
                        }
                        if(strstr(path,".PRT") && !strcmp(mode,"wb")) {
                            ++fat_open_attempts;
                            if(fat_open_fail){errno=EIO;return nullptr;}
                        }
                        return fopen(path,mode);
                    }
                    inline int esp_vfs_fat_info(const char*p,uint64_t*t,uint64_t*f) {
                        *t=9842688;uint64_t used=0;
                        for(auto &entry:std::filesystem::recursive_directory_iterator(p))
                            if(entry.is_regular_file()) used+=(entry.file_size()+4095)/4096*4096;
                        *f=used>=*t?0:*t-used;return 0;
                    }
                    inline size_t fat_fwrite(const void*p,size_t s,size_t n,FILE*f) {
                        uint64_t total=0,available=0;esp_vfs_fat_info(VOLUME,&total,&available);
                        if(available<s*n) {errno=ENOSPC;return 0;}
                        return fwrite(p,s,n,f);
                    }
                    inline bool fat_sync_fail=false;
                    inline int fat_sync_calls=0;
                    inline int fat_fsync(int fd) {
                        ++fat_sync_calls;
                        if(fat_sync_fail&&fat_sync_calls>=2) {errno=EIO;return -1;}
                        return fsync(fd);
                    }
                """,
                "wear_levelling.h": "#pragma once\ninline size_t wl_sector_size(int){return 4096;}\n",
                "sdkconfig.h": "#pragma once\n#define CONFIG_WL_SECTOR_SIZE 4096\n",
                "esp_partition.h": """
                    #pragma once
                    struct esp_partition_t {unsigned address,size;};
                    #define ESP_PARTITION_TYPE_DATA 1
                    #define ESP_PARTITION_SUBTYPE_DATA_FAT 2
                    inline const esp_partition_t *esp_partition_find_first(int,int,const char*) {
                        static esp_partition_t p{0x680000,0x980000};return &p;
                    }
                """,
                "esp_rom_crc.h": """
                    #pragma once
                    #include <cstdint>
                    inline uint32_t esp_rom_crc32_le(uint32_t n,const uint8_t*p,size_t s) {
                        while(s--) {n=(n^*p++)*16777619u;}
                        return n;
                    }
                """,
                "esp_err.h": """
                    #pragma once
                    typedef int esp_err_t;
                    #define ESP_OK 0
                    #define ESP_FAIL -1
                    #define ESP_ERR_NVS_NOT_FOUND 1
                    #define ESP_ERR_NO_MEM 2
                    #define ESP_ERR_TIMEOUT 3
                    #define ESP_ERR_INVALID_STATE 4
                    #define ESP_ERR_INVALID_RESPONSE 5
                    inline const char *esp_err_to_name(int e){return e?"ESP_FAIL":"ESP_OK";}
                """,
                "esp_attr.h": "#pragma once\n#define EXT_RAM_BSS_ATTR\n",
                "esp_heap_caps.h": """
                    #pragma once
                    #include <cstdlib>
                    #define MALLOC_CAP_SPIRAM 1
                    #define MALLOC_CAP_8BIT 2
                    inline bool host_fail_alloc=false;
                    inline void *heap_caps_malloc(size_t n,int){return host_fail_alloc?nullptr:malloc(n);}
                    inline void *heap_caps_calloc(size_t n,size_t s,int){return host_fail_alloc?nullptr:calloc(n,s);}
                    inline void heap_caps_free(void *p){free(p);}
                """,
                "esp_random.h": """
                    #pragma once
                    #include <cstdint>
                    #include <unistd.h>
                    inline uint32_t esp_random(){static uint32_t s=getpid();return ++s;}
                """,
                "esp_timer.h": """
                    #pragma once
                    #include <cstdint>
                    inline int64_t host_now_us=0;
                    inline int64_t esp_timer_get_time(){return host_now_us;}
                """,
                "esp_log.h": """
                    #pragma once
                    #include <cstdio>
                    #include <cstdarg>
                    #include <string>
                    #include <vector>
                    inline std::vector<std::string> host_logs;
                    inline void host_log(const char*,const char*fmt,...) {
                        char b[512];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);
                        host_logs.emplace_back(b);
                    }
                    #define ESP_LOGW host_log
                    #define ESP_LOGE host_log
                    #define ESP_LOGI host_log
                    #define ESP_LOGD host_log
                """,
                "freertos/FreeRTOS.h": """
                    #pragma once
                    #include <cassert>
                    #define portMAX_DELAY 0xffffffffu
                    #define pdMS_TO_TICKS(n) (n)
                    #define pdPASS 1
                    #define configASSERT(n) assert(n)
                """,
                "freertos/semphr.h": """
                    #pragma once
                    #include <mutex>
                    typedef std::mutex *SemaphoreHandle_t;
                    inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new std::mutex;}
                    inline void xSemaphoreTake(SemaphoreHandle_t m,unsigned){m->lock();}
                    inline void xSemaphoreGive(SemaphoreHandle_t m){m->unlock();}
                """,
                "freertos/task.h": """
                    #pragma once
                    inline int xTaskCreate(void(*)(void*),const char*,int,void*,int,void*){return 1;}
                    inline void vTaskDelay(unsigned){}
                """,
                "cJSON.h": """
                    #pragma once
                    #include <cstdlib>
                    #include <cstring>
                    struct cJSON {const char *text="{}";const char *valuestring=nullptr;cJSON *child=nullptr,*next=nullptr;};
                    inline const char *host_status="created",*host_game_id="";
                    inline cJSON host_json,host_item,host_results,host_id,host_result_status;
                    inline cJSON *cJSON_CreateObject(){return &host_json;}
                    inline cJSON *cJSON_CreateArray(){return &host_json;}
                    inline void cJSON_AddItemToArray(cJSON*,cJSON*){}
                    inline void cJSON_AddItemToObject(cJSON*,const char*,cJSON*){}
                    inline void cJSON_AddStringToObject(cJSON*,const char*,const char*){}
                    inline void cJSON_AddNumberToObject(cJSON*,const char*,double){}
                    inline void cJSON_Delete(cJSON*){}
                    inline cJSON *cJSON_Parse(const char*){return &host_json;}
                    inline bool cJSON_IsString(const cJSON *p){return p&&p->valuestring;}
                    inline cJSON *cJSON_GetObjectItem(cJSON*,const char *key) {
                        if(!strcmp(key,"results")){host_results.child=&host_item;return &host_results;}
                        if(!strcmp(key,"externalId")){host_id.valuestring=host_game_id;return &host_id;}
                        if(!strcmp(key,"status")){host_result_status.valuestring=host_status;return &host_result_status;}
                        return nullptr;
                    }
                    #define cJSON_ArrayForEach(item,array) for(item=(array)?(array)->child:nullptr;item;item=item->next)
                    inline char *cJSON_PrintUnformatted(const cJSON *j){return strdup(j->text);}
                    inline void cJSON_free(void *p){free(p);}
                """,
                "nvs.h": """
                    #pragma once
                    #include "esp_err.h"
                    #include <cstdio>
                    #include <cstdint>
                    #include <string>
                    #include <filesystem>
                    typedef unsigned nvs_handle_t;
                    #define NVS_READWRITE 1
                    inline std::string nvs_path(unsigned h,const char *k){return NVS_DIR+std::to_string(h)+"_"+k;}
                    inline int nvs_open(const char *n,int,unsigned *h){*h=std::string(n)=="diagnostics"?1:2;return 0;}
                    inline void nvs_close(unsigned){}
                    inline int nvs_commit(unsigned){return 0;}
                    inline int nvs_set_blob(unsigned h,const char*k,const void*p,size_t n){
                        FILE*f=fopen(nvs_path(h,k).c_str(),"wb");if(!f)return -1;
                        bool ok=fwrite(p,1,n,f)==n;fclose(f);return ok?0:-1;
                    }
                    inline int nvs_get_blob(unsigned h,const char*k,void*p,size_t*n){
                        FILE*f=fopen(nvs_path(h,k).c_str(),"rb");if(!f)return 1;
                        fseek(f,0,SEEK_END);size_t s=ftell(f);rewind(f);
                        if(!p){*n=s;fclose(f);return 0;}
                        if(s>*n){fclose(f);return -1;}
                        bool ok=fread(p,1,s,f)==s;*n=s;fclose(f);return ok?0:-1;
                    }
                    inline int nvs_set_u8(unsigned h,const char*k,uint8_t v){return nvs_set_blob(h,k,&v,1);}
                    inline int nvs_get_u8(unsigned h,const char*k,uint8_t*v){size_t n=1;return nvs_get_blob(h,k,v,&n);}
                    inline int nvs_erase_key(unsigned h,const char*k){return std::filesystem::remove(nvs_path(h,k))?0:1;}
                """,
            }
            for name, content in headers.items():
                (temp / "sdk" / name).write_text(textwrap.dedent(content))
            for header in ("fat_name.h","fat_patterns.h"):
                (temp/header).write_text((ROOT/header).read_text().replace('"/fatfs','"'+str(temp/"volume")))
            for source in ("game_journal.cpp", "terminal_logs.cpp", "offline_cache.cpp", "active_game.cpp"):
                content = (ROOT / source).read_text().replace('"/fatfs', '"' + str(temp / "volume"))
                if source=="game_journal.cpp":
                    content=content.replace("rename(", "fat_rename(").replace("fwrite(", "fat_fwrite(").replace("fsync(", "fat_fsync(").replace("fopen(", "fat_fopen(")
                if source=="active_game.cpp":content+="\nvoid host_latch_active(){had_active=true;}\n"
                (temp / source).write_text(content)
            # Instrument the real chunk writer too: faults must not bypass its
            # fsync/write calls merely because they moved into a shared header.
            (temp/"storage_write.h").write_text('#include "esp_vfs_fat.h"\n'+
                (ROOT/"storage_write.h").read_text().replace("fwrite(","fat_fwrite(").replace("fsync(","fat_fsync("))
            harness = r'''
                #include "game_journal.h"
                #include "terminal_logs.h"
                #include "offline_cache.h"
                #include "nvs.h"
                #include "esp_log.h"
                #include "esp_vfs_fat.h"
                #include "esp_timer.h"
                #include "esp_heap_caps.h"
                #include "active_game.h"
                #include "ui_manager.h"
                #include <cassert>
                #include <cstdio>
                #include <cstring>
                #include <filesystem>
                #include <sys/time.h>
                #include <set>
                #include <string>
                GameStore g_store;
                bool store_credit_game_cancelled(const char*){return false;}
                static std::set<std::string> ended;
                bool store_game_was_ended(const char *id){return id[0]&&ended.count(id);}
                void store_record_game_end(const char *id){if(id[0])ended.insert(id);}
                bool day_players_repair_safe(){return true;}
                void host_latch_active();
                bool store_sync_commit_begin(){return true;}
                void store_sync_commit_end(){}
                void store_rebuild_bill_projection(){}
                void store_reconcile_lineup_after_cache_load(){}
                static int save_calls=0,pushed_games=0;
                static int s_pending_push_accepted=0,s_pending_push_failed=0,s_last_http_status=200;
                static const char *TAG="test";
                using TickType_t=uint32_t;
                constexpr size_t HTTP_BUF_SIZE=65536;
                void game_store_save(){++save_calls;}
                void game_store_save_settings(){++save_calls;}
                static bool finish_game_commit(FinishedGame&);
                void kredit_events_lock(){}
                void kredit_events_unlock(){}
                bool reconcile_lineup_with_credits_unlocked(){return false;}
                void portal_live_changed(int){}
                void store_reconcile_lineup_with_credits(){}
                void clear_active_game_credit_tracking(GameStore*){}
                bool sync_commit_begin(const char*,TickType_t*){return true;}
                void sync_commit_end(const char*,TickType_t){}
                void note_pushed(const char*,int n){pushed_games+=n;}
                void set_http_error(const char*,const char*,const char*){}
                cJSON *pending_game_to_json(PendingGame *game) {
                    host_game_id=game->externalId;return cJSON_CreateObject();
                }
                esp_err_t http_post_json(const char*,const char*,char *response,size_t size) {
                    snprintf(response,size,"accepted");return ESP_OK;
                }
                static void full_cache() {
                    std::filesystem::create_directory(VOLUME "/TMCACHE");
                    FILE*f=fopen(VOLUME "/TMCACHE/HISTORY.TMP","wb");assert(f);fclose(f);
                    std::filesystem::resize_file(VOLUME "/TMCACHE/HISTORY.TMP",9842688);
                }
                int main(int argc,char**argv) {
                    assert(argc==2);terminal_logs_init();g_store.offlineCacheHealthy=true;
                    bool finish_test=!strncmp(argv[1],"finish-",7);
                    if((finish_test && strcmp(argv[1],"finish-reboot-read")) || !strcmp(argv[1],"empty-proof")||!strcmp(argv[1],"capacity") || !strcmp(argv[1],"write-errors") || !strcmp(argv[1],"rate-limit")) {
                        std::filesystem::remove_all(VOLUME);
                        std::filesystem::create_directory(VOLUME);
                    }
                    bool prepare=!strncmp(argv[1],"prepare-",8);
                    if(prepare) {
                        std::filesystem::remove_all(VOLUME);std::filesystem::create_directory(VOLUME);
                    }
                    bool mounted=offline_cache_mount();
                    if(!strcmp(argv[1],"empty-proof")) {
                        assert(mounted);host_fail_alloc=true;
                        GameJournalInspection journal{};
                        assert(game_journal_inspect_locked(&journal)&&!journal.uncertain&&!journal.pending);
                        host_latch_active();ActiveGameRepairInspection active{};
                        assert(active_game_inspect_repair_locked(&active));
                        assert(active.had_active&&active.stale_latch&&!active.present&&!active.unreadable);
                        assert(active_game_restore()); // Empty boot needs no record allocations.
                        g_store.harakiriOrdersReady=true;host_latch_active();
                        assert(!active_game_inspect_repair_locked(&active)&&active.ram_active&&active.had_active);
                        g_store.harakiriOrdersReady=false;
                        FILE*f=fopen(VOLUME "/g_uncertain","wb");assert(f);fputc(1,f);fclose(f);
                        assert(!game_journal_inspect_locked(&journal)&&journal.uncertain>0);
                        host_fail_alloc=false;
                        assert(unlink(VOLUME "/g_uncertain")==0);
                        printf("PASS real empty journal/active: no allocations, stale had_active cleared; actual RAM game and unreadable journal stay blocked\\n");
                        return 0;
                    }
                    if(prepare) {
                        assert(mounted && game_journal_init());
                        FinishedGame game{};strcpy(game.base.externalId,"protected-game");
                        assert(game_journal_save(&game));
                        if(!strcmp(argv[1],"prepare-clean")) assert(game_journal_ack("protected-game"));
                        if(!strcmp(argv[1],"prepare-unknown")) {
                            FILE*f=fopen(VOLUME "/g_bad.part","wb");assert(f);fputc(1,f);fclose(f);
                        }
                        uint8_t sentinel=42;assert(nvs_set_u8(2,"sentinel",sentinel)==0);
                        full_cache();return 0;
                    }
                    if(!strncmp(argv[1],"full-",5)) {
                        uint8_t sentinel=0;assert(nvs_get_u8(2,"sentinel",&sentinel)==0 && sentinel==42);
                        bool clean=!strcmp(argv[1],"full-clean");
                        if(clean) {
                            assert(mounted && game_journal_init() && game_journal_ready());
                            assert(!std::filesystem::exists(VOLUME "/TMCACHE/HISTORY.TMP"));
                            assert(std::filesystem::exists(VOLUME "/J000.REC"));
                        } else {
                            assert(!mounted && offline_cache_storage_available());
                            assert(!offline_cache_cleanup());
                            assert(std::filesystem::file_size(VOLUME "/TMCACHE/HISTORY.TMP")==9842688);
                            if(!strcmp(argv[1],"full-pending")) {
                                assert(game_journal_init() && g_store.pendingGamesCount==1);
                                assert(!game_journal_ready() && !offline_cache_cleanup());
                                g_store.pendingGamesCount=0; // disk proof must still refuse
                                assert(!offline_cache_cleanup());
                                assert(game_journal_ack("protected-game")); // exact portal ACK, ENOSPC drain
                                assert(offline_cache_cleanup() && game_journal_ready());
                                assert(!std::filesystem::exists(VOLUME "/TMCACHE/HISTORY.TMP"));
                            } else {
                                assert(!game_journal_init());
                                assert(std::filesystem::file_size(VOLUME "/g_bad.part")==1);
                            }
                        }
                        assert(nvs_get_u8(2,"sentinel",&sentinel)==0 && sentinel==42);
                        bool census=false;for(auto &line:host_logs) if(line.find("FAT census file=")!=std::string::npos)census=true;
                        assert(census);
                        printf("PASS real modules: %s; NVS retained\\n",argv[1]);return 0;
                    }
                    assert(mounted);
                    assert(game_journal_init());
                    if(finish_test) {
                        if(!strcmp(argv[1],"finish-reboot-read")) {
                            assert(g_store.pendingGamesCount==1 && !game_journal_ready());
                            host_now_us=1000000;game_journal_maintenance();
                            assert(game_journal_contains("finished-recovery")==1 && game_journal_ready());
                            assert(http_push_pending_games()==ESP_OK && !g_store.pendingGamesCount);
                            GameJournalInspection proof{};assert(game_journal_inspect_locked(&proof));
                            assert(proof.pending==0 && proof.accepted==1);
                            printf("PASS emergency game survives process restart, retries and archives after ACK\\n");
                            return 0;
                        }
                        strcpy(g_store.spielId,"finished-recovery");g_store.lauf=1;
                        g_store.spielerCount=1;g_store.spieler[0].id=9;
                        strcpy(g_store.spieler[0].name,"Test");
                        g_store.screen=SCREEN_SPIEL;
                        bool normal=!strcmp(argv[1],"finish-normal")||!strcmp(argv[1],"finish-ack-rewrite");
                        bool fsync_failure=!strcmp(argv[1],"finish-fsync-ack");
                        fat_open_attempts=0;fat_open_fail=!normal&&!fsync_failure;
                        fat_sync_fail=fsync_failure;fat_sync_calls=0;
                        _store_finish_game();
                        assert(g_store.pendingGamesCount==1 && save_calls==1);
                        assert(g_store.screen==SCREEN_RESULTATE);
                        if(!normal) {
                            assert(!game_journal_ready());
                            size_t bytes=0;
                            assert(nvs_get_blob(2,"finish_pending",nullptr,&bytes)==ESP_OK && bytes==7072);
                            assert(game_journal_contains("finished-recovery")==0);
                            bool diagnostic=false;
                            for(auto &line:host_logs)
                                if(line.find(fsync_failure?"fsync errno=5":"fopen(.part) errno=5")!=std::string::npos &&
                                   line.find("frei=")!=std::string::npos && line.find("Grenze=")!=std::string::npos)diagnostic=true;
                            assert(diagnostic);
                            TerminalLogEntry entries[20];int count=terminal_logs_snapshot(entries,20);
                            bool terminal_diagnostic=false;
                            for(int i=0;i<count;i++)
                                if(strstr(entries[i].msg,"errno=5")&&!strcmp(entries[i].type,"data_loss_risk"))terminal_diagnostic=true;
                            assert(terminal_diagnostic);
                        }
                        if(!strcmp(argv[1],"finish-reboot-write"))return 0;
                        if(!strcmp(argv[1],"finish-remap")) {
                            assert(!game_journal_ready());
                            assert(game_journal_remap(9,19));
                            g_store.pendingGames[0].teilnahmen[0].spielerId=19;
                            host_now_us=1000000;fat_open_fail=false;game_journal_maintenance();
                            assert(game_journal_ready());
                            g_store.historyCount=0;assert(game_journal_reload_pending()>=0);
                            assert(g_store.historyCount==1 && g_store.history[0].base.teilnahmen[0].spielerId==19);
                            printf("PASS emergency NVS player remap works while readiness is latched\\n");
                        }
                        if(!strcmp(argv[1],"finish-retry")) {
                            assert(fat_open_attempts==1);
                            host_now_us=999999;game_journal_maintenance();assert(fat_open_attempts==1);
                            host_now_us=1000000;game_journal_maintenance();assert(fat_open_attempts==2);
                            host_now_us=5999999;game_journal_maintenance();assert(fat_open_attempts==2);
                            host_now_us=6000000;game_journal_maintenance();assert(fat_open_attempts==3);
                            host_now_us=35999999;game_journal_maintenance();assert(fat_open_attempts==3);
                            fat_open_fail=false;host_now_us=36000000;game_journal_maintenance();
                            assert(game_journal_contains("finished-recovery")==1 && game_journal_ready());
                            assert(g_store.pendingGamesCount==1); // durable save alone is not portal acceptance
                            printf("PASS finished game retry: exact 1s/5s/30s backoff, latch recovers\\n");
                        }
                        host_status=!strcmp(argv[1],"finish-skipped")?"skipped":"created";
                        if(!strcmp(argv[1],"finish-ack-rewrite")) {
                            fat_open_fail=true;
                            assert(http_push_pending_games()==ESP_OK && !g_store.pendingGamesCount);
                            assert(pushed_games==1 && save_calls==2 && game_journal_contains("finished-recovery")==1);
                            GameJournalInspection accepted{};
                            assert(game_journal_inspect_locked(&accepted)&&accepted.pending==0&&accepted.accepted==1);
                            host_status="skipped";
                        }
                        fat_open_fail=false;fat_sync_fail=false;
                        assert(http_push_pending_games()==ESP_OK);
                        assert(!g_store.pendingGamesCount && pushed_games==1 && save_calls==2);
                        assert(game_journal_ready());
                        size_t bytes=0;assert(nvs_get_blob(2,"finish_pending",nullptr,&bytes)==ESP_ERR_NVS_NOT_FOUND);
                        assert(!std::filesystem::exists(VOLUME "/J000.PRT"));
                        assert(game_journal_reload_pending()==0 && !g_store.pendingGamesCount);
                        GameJournalInspection proof{};assert(game_journal_inspect_locked(&proof) && !proof.pending);
                        assert(proof.accepted==((normal||!strcmp(argv[1],"finish-retry")||!strcmp(argv[1],"finish-remap"))?1:0));
                        printf("PASS actual finish + HTTP ACK: %s; NVS drained, no resurrection, archive preserved\\n",argv[1]);
                        return 0;
                    }
                    if(!strcmp(argv[1],"rate-limit")) {
                        TerminalLogEntry entries[20];
                        int before=terminal_logs_snapshot(entries,20);
                        terminal_log_add("data_loss_risk","ERROR","Spielarchiv nicht verfügbar");
                        int first=terminal_logs_snapshot(entries,20);assert(first==before+1);
                        terminal_log_add("data_loss_risk","ERROR","Spielarchiv konnte nicht in die Übertragung geladen werden");
                        host_now_us=3599999999LL;
                        terminal_log_add("data_loss_risk","ERROR","Neues Spiel gesperrt: Speicher voll oder Spielarchiv nicht verfügbar");
                        assert(terminal_logs_snapshot(entries,20)==first);
                        host_now_us=3600000000LL;
                        terminal_log_add("data_loss_risk","ERROR","Spielarchiv nicht verfügbar");
                        assert(terminal_logs_snapshot(entries,20)==first+1);
                        terminal_log_add("sync","ERROR","Andere Fehlermeldung");
                        assert(terminal_logs_snapshot(entries,20)==first+2);
                        terminal_log_add("unsupported_glyph","WARN","unsupported glyph: 1");
                        terminal_log_add("unsupported_glyph","WARN","unsupported glyph: 4");
                            int after_hour=terminal_logs_snapshot(entries,20);
                            assert(after_hour==first+3);
                        host_now_us+=3600000000LL;
                        terminal_log_add("unsupported_glyph","WARN","unsupported glyph: 8");
                        assert(terminal_logs_snapshot(entries,20)==first+4);
                        return 0;
                    }
                    if(!strcmp(argv[1],"write-errors")) {
                        FinishedGame game{};strcpy(game.base.externalId,"sync-failed");
                        fat_sync_fail=true;fat_sync_calls=0;
                        assert(!game_journal_save(&game));
                        assert(std::filesystem::exists(VOLUME "/J000.PRT"));
                        g_store.pendingGamesCount=0;
                        assert(!offline_cache_cleanup()); // valid unacknowledged .part is protected
                        fat_sync_fail=false;
                        assert(game_journal_init() && g_store.pendingGamesCount==1);
                        assert(std::filesystem::exists(VOLUME "/J000.REC"));
                        assert(fat_sync_calls>=2); // real 7072-byte writer: 512 + 6560 bytes
                        FILE *broken=fopen(VOLUME "/J199.REC","wb");assert(broken);fputc('x',broken);fclose(broken);
                        strcpy(game.base.externalId,"separate-game");
                        assert(game_journal_save(&game)); // corrupt unrelated entry cannot strand this finish
                        bool named=false;for(auto &line:host_logs)if(line.find("trim/read_record")!=std::string::npos)named=true;
                        assert(named && std::filesystem::exists(VOLUME "/J199.REC"));
                        assert(!game_journal_ready()); // unknown game evidence still fails closed
                        printf("PASS complete unsynced temp survives fsync failure and reload\\n");
                        return 0;
                    }
                    if(!strcmp(argv[1],"capacity")) {
                        std::filesystem::remove_all(VOLUME);
                        std::filesystem::create_directory(VOLUME);
                        for(int i=0;i<250;i++) {
                            FinishedGame game{};
                            snprintf(game.base.externalId,sizeof(game.base.externalId),"archive-%d",i);
                            strcpy(game.finishedAt,"2099-01-01T10:00:00.000Z"); // no age expiry
                            assert(game_journal_save(&game));
                            assert(game_journal_ack(game.base.externalId));
                        }
                        int files=0;
                        for(auto &entry:std::filesystem::directory_iterator(VOLUME)) {
                            (void)entry;++files;
                        }
                        assert(files<=20);
                        for(int i=0;i<200;i++) {
                            FinishedGame game{};
                            snprintf(game.base.externalId,sizeof(game.base.externalId),"pending-%d",i);
                            assert(game_journal_save(&game));
                        }
                        FinishedGame overflow{};strcpy(overflow.base.externalId,"overflow");
                        assert(!game_journal_save(&overflow));
                        GameJournalInspection inspection{};
                        game_journal_storage_lock();
                        assert(game_journal_inspect_locked(&inspection));
                        game_journal_storage_unlock();
                        assert(inspection.pending==200 && inspection.accepted==0);
                        assert(game_journal_record_size()*202 < 9842688/2);
                        printf("PASS journal: 250 ACKs bounded to 20; 200 pending preserved; "
                               "FinishedGame=%zu Record=%zu\\n",sizeof(FinishedGame),game_journal_record_size());
                        return 0;
                    }
                    if(!strcmp(argv[1],"write")) {
                        FinishedGame game{};
                        strcpy(game.base.externalId,"reboot-game");
                        strcpy(game.base.datum,"2026-10-14");
                        strcpy(game.finishedAt,"2026-10-14T10:00:00.000Z");
                        game.base.teilnahmen_count=1;game.base.teilnahmen[0].spielerId=-5;
                        game.base.ergebnisse_count=1;game.base.ergebnisse[0].spielerId=-5;
                        game.spieler_count=1;game.spielerIds[0]=-5;
                        assert(game_journal_save(&game));
                        terminal_log_add("sync","WARN","critical-sync");
                        terminal_log_add("wifi","WARN","volatile-wifi");
                        terminal_logs_flush_critical();
                        return 0;
                    }
                    assert(g_store.pendingGamesCount==1);
                    assert(!strcmp(g_store.pendingGames[0].externalId,"reboot-game"));
                    TerminalLogEntry logs[10];int n=terminal_logs_snapshot(logs,10);
                    assert(n==1 && !strcmp(logs[0].msg,"critical-sync"));
                    const char *unknown[]={"not-an-ack"};terminal_logs_ack(unknown,1);
                    assert(terminal_logs_snapshot(logs,10)==1);
                    const char *acked[]={logs[0].id};terminal_logs_ack(acked,1);
                    assert(terminal_logs_snapshot(logs,10)==0);
                    assert(game_journal_remap(-5,9));
                    // A finishing-game snapshot can still carry the old ID
                    // between disk remap and the roster's RAM publication.
                    assert(game_journal_save(&g_store.history[0]));
                    assert(game_journal_ack("reboot-game"));
                    g_store.pendingGamesCount=0;
                    assert(game_journal_reload_pending()==0);
                    assert(game_journal_recover(3650)==1);
                    assert(g_store.pendingGames[0].teilnahmen[0].spielerId==9);
                    assert(g_store.pendingGames[0].ergebnisse[0].spielerId==9);
                    char ids[4][40];
                    assert(game_journal_manifest("2026-10-14",ids,4)==1);
                    assert(!strcmp(ids[0],"reboot-game"));
                    // Complete fsynced temporary file, power cut before rename.
                    std::filesystem::rename(VOLUME "/J000.REC",VOLUME "/J000.PRT");
                    g_store.pendingGamesCount=0;
                    assert(game_journal_reload_pending()==1);
                    assert(std::filesystem::exists(VOLUME "/J000.REC"));
                    // Corruption is latched and cannot silently enable new games.
                    FILE*f=fopen(VOLUME "/J000.REC","wb");fputs("truncated",f);fclose(f);
                    assert(game_journal_reload_pending()==-1);
                    assert(!game_journal_ready());
                }
            '''
            actual=production_function(ROOT/"game_store.cpp","static void _store_finish_game(void)\n{")
            actual+="\n"+production_function(ROOT/"game_store.cpp","static bool finish_game_commit(FinishedGame &fg) {")
            actual+="\nstatic int s_waiting_local_games=0;\n"
            actual+="\n"+production_function(ROOT.parent/"net/http_sync.cpp","static void log_waiting_local_player(")
            actual+="\n"+production_function(ROOT.parent/"net/http_sync.cpp","esp_err_t http_push_pending_games(void)\n{")
            harness=harness.replace("int main(int argc,char**argv)",actual+"\nint main(int argc,char**argv)")
            (temp / "harness.cpp").write_text(textwrap.dedent(harness))
            exe = temp / "test"
            subprocess.run([
                "g++", "-std=gnu++17", "-Wall", "-Wextra", "-Werror",
                "-I" + str(temp / "sdk"), "-I" + str(ROOT), "-I" + str(ROOT.parent/"net"),
                '-DNVS_DIR=std::string("' + str(temp / "nvs") + '/")',
                '-DVOLUME="' + str(temp / "volume") + '"',
                str(temp / "game_journal.cpp"), str(temp / "terminal_logs.cpp"),
                str(temp / "offline_cache.cpp"),
                str(temp / "active_game.cpp"),
                str(temp / "harness.cpp"), "-o", str(exe),
            ], check=True)
            subprocess.run([str(exe), "empty-proof"], check=True)
            subprocess.run([str(exe), "write"], check=True)
            subprocess.run([str(exe), "restart"], check=True)
            subprocess.run([str(exe), "capacity"], check=True)
            for scenario in ("clean", "pending", "unknown"):
                subprocess.run([str(exe), "prepare-" + scenario], check=True)
                subprocess.run([str(exe), "full-" + scenario], check=True)
            subprocess.run([str(exe), "write-errors"], check=True)
            subprocess.run([str(exe), "rate-limit"], check=True)
            for scenario in ("finish-created","finish-skipped","finish-fsync-ack","finish-retry","finish-remap",
                             "finish-normal","finish-ack-rewrite","finish-reboot-write","finish-reboot-read"):
                subprocess.run([str(exe),scenario],check=True)


if __name__ == "__main__":
    unittest.main()
