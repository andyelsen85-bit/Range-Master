"""Compile the actual persistence modules with a POSIX SDK shim.

These are storage/restart tests, not ESP32 timing or physical power-cut proof.
"""
import pathlib
import subprocess
import tempfile
import textwrap
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1] / "main" / "store"


class DurableGames(unittest.TestCase):
    def test_full_modules_storage_and_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            for folder in ("sdk/freertos", "volume", "nvs"):
                (temp / folder).mkdir(parents=True)
            headers = {
                "esp_vfs_fat.h": """
                    #pragma once
                    #include "esp_err.h"
                    #include <sys/statvfs.h>
                    #include <cstdint>
                    #include <cstdio>
                    #include <cerrno>
                    #include <unistd.h>
                    inline int fat_rename(const char*a,const char*b) {
                        if(access(b,F_OK)==0) {errno=EEXIST;return -1;}
                        return std::rename(a,b);
                    }
                    inline int esp_vfs_fat_info(const char*p,uint64_t*t,uint64_t*f) {
                        struct statvfs s{};if(statvfs(p,&s))return -1;
                        *t=(uint64_t)s.f_blocks*s.f_frsize;*f=(uint64_t)s.f_bavail*s.f_frsize;return 0;
                    }
                """,
                "esp_err.h": """
                    #pragma once
                    typedef int esp_err_t;
                    #define ESP_OK 0
                    #define ESP_FAIL -1
                    #define ESP_ERR_NVS_NOT_FOUND 1
                """,
                "esp_attr.h": "#pragma once\n#define EXT_RAM_BSS_ATTR\n",
                "esp_heap_caps.h": """
                    #pragma once
                    #include <cstdlib>
                    #define MALLOC_CAP_SPIRAM 1
                    #define MALLOC_CAP_8BIT 2
                    inline void *heap_caps_malloc(size_t n,int){return malloc(n);}
                    inline void *heap_caps_calloc(size_t n,size_t s,int){return calloc(n,s);}
                    inline void heap_caps_free(void *p){free(p);}
                """,
                "esp_random.h": """
                    #pragma once
                    #include <cstdint>
                    #include <unistd.h>
                    inline uint32_t esp_random(){static uint32_t s=getpid();return ++s;}
                """,
                "esp_log.h": """
                    #pragma once
                    #define ESP_LOGW(...) ((void)0)
                    #define ESP_LOGE(...) ((void)0)
                    #define ESP_LOGI(...) ((void)0)
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
                    struct cJSON {const char *text;};
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
                        if(s>*n){fclose(f);return -1;}
                        bool ok=fread(p,1,s,f)==s;*n=s;fclose(f);return ok?0:-1;
                    }
                    inline int nvs_set_u8(unsigned h,const char*k,uint8_t v){return nvs_set_blob(h,k,&v,1);}
                    inline int nvs_erase_key(unsigned h,const char*k){return std::filesystem::remove(nvs_path(h,k))?0:1;}
                """,
            }
            for name, content in headers.items():
                (temp / "sdk" / name).write_text(textwrap.dedent(content))
            for source in ("game_journal.cpp", "terminal_logs.cpp"):
                content = (ROOT / source).read_text().replace('"/fatfs', '"' + str(temp / "volume"))
                if source=="game_journal.cpp":
                    content=content.replace("rename(", "fat_rename(")
                (temp / source).write_text(content)
            harness = r'''
                #include "game_journal.h"
                #include "terminal_logs.h"
                #include <cassert>
                #include <cstdio>
                #include <cstring>
                #include <filesystem>
                GameStore g_store;
                bool store_sync_commit_begin(){return true;}
                void store_sync_commit_end(){}
                int main(int argc,char**argv) {
                    assert(argc==2);terminal_logs_init();g_store.offlineCacheHealthy=true;
                    assert(game_journal_init());
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
                    std::filesystem::rename(VOLUME "/g_reboot-game",VOLUME "/g_reboot-game.part");
                    g_store.pendingGamesCount=0;
                    assert(game_journal_reload_pending()==1);
                    assert(std::filesystem::exists(VOLUME "/g_reboot-game"));
                    // Corruption is latched and cannot silently enable new games.
                    FILE*f=fopen(VOLUME "/g_reboot-game","wb");fputs("truncated",f);fclose(f);
                    assert(game_journal_reload_pending()==-1);
                    assert(!game_journal_ready());
                }
            '''
            (temp / "harness.cpp").write_text(textwrap.dedent(harness))
            exe = temp / "test"
            subprocess.run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I" + str(temp / "sdk"), "-I" + str(ROOT),
                '-DNVS_DIR=std::string("' + str(temp / "nvs") + '/")',
                '-DVOLUME="' + str(temp / "volume") + '"',
                str(temp / "game_journal.cpp"), str(temp / "terminal_logs.cpp"),
                str(temp / "harness.cpp"), "-o", str(exe),
            ], check=True)
            subprocess.run([str(exe), "write"], check=True)
            subprocess.run([str(exe), "restart"], check=True)


if __name__ == "__main__":
    unittest.main()
