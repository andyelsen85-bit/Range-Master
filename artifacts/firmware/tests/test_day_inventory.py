"""Compile the real compact inventory store with strict host warnings and NVS faults."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class InventoryOutboxTest(unittest.TestCase):
    def test_reboot_replay_zero_counts_and_flash_failures(self):
        with tempfile.TemporaryDirectory() as folder:
            p = pathlib.Path(folder)
            (p / "freertos").mkdir()
            (p / "nvs.h").write_text(r"""
#pragma once
#include <stdint.h>
#include <stddef.h>
using nvs_handle_t=unsigned;using esp_err_t=int;
#define ESP_OK 0
int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
int nvs_commit(nvs_handle_t);
""")
            (p / "cJSON.h").write_text(r"""
#pragma once
#include <string.h>
struct cJSON {int type=0;double valuedouble=0;char *valuestring=nullptr;const char *string=nullptr;cJSON *child=nullptr,*next=nullptr;};
inline cJSON *cJSON_GetObjectItem(cJSON *o,const char *s){if(!o)return nullptr;for(auto c=o->child;c;c=c->next)if(c->string&&!strcmp(c->string,s))return c;return nullptr;}
inline bool cJSON_IsNumber(cJSON *c){return c&&c->type==1;}
inline bool cJSON_IsString(cJSON *c){return c&&c->type==2;}
inline bool cJSON_IsObject(cJSON *c){return c&&c->type==3;}
inline bool cJSON_IsTrue(cJSON *c){return c&&c->type==4;}
#define cJSON_ArrayForEach(e,a) for(e=(a)?(a)->child:nullptr;e;e=e->next)
""")
            (p / "esp_random.h").write_text("#pragma once\n#include <stdint.h>\nuint32_t esp_random();\n")
            (p / "esp_attr.h").write_text("#pragma once\n#define EXT_RAM_BSS_ATTR\n")
            (p / "app_config.h").write_text('#pragma once\n#define DISPLAY_LOGICAL_W 1280\n#define DISPLAY_LOGICAL_H 800\n#define CLUB_NAME "Host Check"\n')
            (p / "freertos/FreeRTOS.h").write_text("#pragma once\n#define portMAX_DELAY 0\n")
            (p / "freertos/semphr.h").write_text(r"""
#pragma once
#include <mutex>
using SemaphoreHandle_t=std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new std::mutex();}
inline void xSemaphoreTake(SemaphoreHandle_t s,int){s->lock();}
inline void xSemaphoreGive(SemaphoreHandle_t s){s->unlock();}
""")
            (p / "test.cpp").write_text(r"""
#include "day_inventory.h"
#include <vector>
#include <cassert>
#include <cstring>
static std::vector<unsigned char> disk,staged;
static bool failWrite,failCommit;
uint32_t esp_random(){static uint32_t n=123;return ++n;}
int nvs_set_blob(unsigned,const char*,const void *p,size_t n){if(failWrite)return 1;staged.assign((const unsigned char*)p,(const unsigned char*)p+n);return 0;}
int nvs_commit(unsigned){if(failCommit)return 1;disk=staged;return 0;}
int nvs_get_blob(unsigned,const char*,void *p,size_t *n){if(disk.empty()||*n<disk.size())return 1;memcpy(p,disk.data(),disk.size());*n=disk.size();return 0;}
int main(){
 day_inventory_init(1);int ids[]={7};
 assert(!day_inventory_queue(DAY_MAITRES,0,0,ids,1,false,"","1900-01-01"));assert(disk.empty());
 assert(day_inventory_queue(DAY_MAITRES,0,0,ids,1,false,""));
 assert(!day_inventory_queue(DAY_OPENING,0,0,nullptr,0,false,"","1900-01-01"));
 assert(day_inventory_queue(DAY_OPENING,0,0,nullptr,0,false,""));
 assert(!day_inventory_queue(DAY_CLOSING,-1,0,nullptr,0,false,""));
 DayEvent first;assert(day_inventory_peek(&first));assert(first.kind==DAY_MAITRES);
 day_inventory_init(1);DayEvent reboot;assert(day_inventory_peek(&reboot));assert(!strcmp(first.externalId,reboot.externalId));
 assert(day_inventory_ack(first.externalId));assert(day_inventory_peek(&reboot));assert(reboot.kind==DAY_OPENING);
 failCommit=true;assert(!day_inventory_ack(reboot.externalId));DayEvent retry;assert(day_inventory_peek(&retry));assert(!strcmp(retry.externalId,reboot.externalId));
 failCommit=false;day_inventory_init(1);assert(day_inventory_peek(&retry));assert(!strcmp(retry.externalId,reboot.externalId));assert(day_inventory_ack(retry.externalId));
 DayInventory state;day_inventory_snapshot(&state);assert(state.hasOpening&&state.opening12==0&&state.maitreCount==1);
 failWrite=true;assert(!day_inventory_queue(DAY_CLOSING,99,88,nullptr,0,false,"note"));day_inventory_snapshot(&state);assert(!state.hasClosing);
 failWrite=false;assert(day_inventory_queue(DAY_CLOSING,0,0,nullptr,0,false,"note"));assert(day_inventory_mark_backup(state.datum));
 day_inventory_init(1);day_inventory_snapshot(&state);assert(state.hasClosing&&!strcmp(state.closingNote,"note"));assert(!strcmp(state.backupDate,state.datum));
 cJSON today,root;today.type=3;today.string="today";root.type=3;root.child=&today;
 assert(!day_inventory_apply(&root,"1999-01-01"));assert(day_inventory_apply(&root,state.datum));
 day_inventory_snapshot(&state);assert(state.hasClosing); // pending close overlays a fresh empty portal snapshot
 while(day_inventory_peek(&retry))assert(day_inventory_ack(retry.externalId));
 assert(!day_inventory_peek(nullptr));
 for(int i=0;i<8;i++)assert(day_inventory_queue(DAY_CLOSING,i,0,nullptr,0,false,""));
 assert(!day_inventory_queue(DAY_CLOSING,9,0,nullptr,0,false,""));day_inventory_snapshot(&state);assert(state.closing12==7);
 return 0;
}
""")
            exe = p / "test"
            compile_result = subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                            "-I", str(p), "-I", str(ROOT / "main/store"),
                            str(ROOT / "main/store/day_inventory.cpp"), str(p / "test.cpp"),
                            "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(compile_result.returncode,0,compile_result.stderr)
            run_result=subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run_result.returncode,0,run_result.stderr)
            # When the real LVGL 9.5 headers are available, compile all three
            # modified production screens against them, not invented UI mocks.
            lvgl=pathlib.Path("/tmp/lvgl-gameday")
            if lvgl.exists():
                for name in ["screen_inventory","screen_resultate","screen_geschichte"]:
                    ui_compile=subprocess.run(["g++","-std=c++17","-Os","-Wall","-Wextra","-Werror","-Wno-unused-parameter",
                        "-DLV_CONF_SKIP","-I",str(p),"-I",str(lvgl),"-I",str(ROOT/"main/ui"),"-I",str(ROOT/"main/store"),
                        "-c",str(ROOT/f"main/ui/{name}.cpp"),"-o",str(p/f"{name}.o")],capture_output=True,text=True)
                    self.assertEqual(ui_compile.returncode,0,ui_compile.stderr)


if __name__ == "__main__":
    unittest.main()
