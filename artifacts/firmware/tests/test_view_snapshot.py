"""ASan/UBSan regression: real LVGL + production snapshot/router/publication code.

Every screen route uses a deterministic rebuilding view fixture; hardware/store
services are stubbed. This is not a physical-terminal end-to-end test.
"""
from pathlib import Path
import os
import re
import subprocess
import textwrap

ROOT=Path(__file__).resolve().parents[1]
BUILD=Path("/tmp/trapmaster-view-snapshot")
BUILD.mkdir(parents=True,exist_ok=True)

def function(source, signature):
    start=source.index(signature);opening=source.index("{",start);depth=1;end=opening+1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[start:end]
def write(name,text):
    path=BUILD/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(textwrap.dedent(text))
write("driver/gpio.h","#pragma once\n")
write("lv_conf.h","""
    #pragma once
    #define LV_CONF_H
    #define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_XRGB8888
    #define LV_USE_OS LV_OS_NONE
    #define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
    #define LV_USE_LOG 1
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
""")
manager=(ROOT/"main/ui/ui_manager.cpp").read_text()
assert all("lv_obj_is_valid(" not in cpp.read_text()
    for cpp in (ROOT/"main").rglob("*.cpp")), "Unsafe LVGL validity call reintroduced"
refresh=function(manager,"static void refresh_screen(")
tick=function(manager,"void ui_manager_tick(void)")
show=function(manager,"void ui_manager_show(Screen s)")
code=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <type_traits>
#include "lvgl.h"
#include "ui_manager.h"
#include "view_snapshot.h"
GameStore g_store{};
static lv_obj_t *s_screens[SCREEN_COUNT],*s_sync_indicator;
static lv_indev_t *s_input_device;
static Screen s_current=SCREEN_COUNT;
static uint32_t s_content_hash[SCREEN_COUNT],s_last_input_tick,s_sync_publication_seen;
static SyncUiState published{};
static int rebuilds[SCREEN_COUNT];
static lv_obj_t *lists[SCREEN_COUNT],*drafts[SCREEN_COUNT],*row_fields[SCREEN_COUNT];
static lv_group_t *group;
static void remote_buttons_tick() {}
static bool offline_cache_storage_warning(char *out,size_t n){if(n)*out=0;return false;}
void store_get_sync_ui_state(SyncUiState *out){*out=published;}
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
static bool screen_has_return_button(Screen){return false;}
static void fixture_refresh(Screen s) {
    rebuilds[s]++;
    lv_obj_clean(lists[s]); // old snapshot row pointers die here
    row_fields[s]=lv_textarea_create(lists[s]);lv_textarea_set_text(row_fields[s],"new row");
    lv_group_add_obj(group,row_fields[s]);
    // A persistent edit form survives; its draft must be restored.
    lv_textarea_set_text(drafts[s],"server replacement");
}
'''
for name in sorted(set(re.findall(r"\b(screen_\w+_refresh)\(",refresh))):
    screen="SCREEN_"+name.removeprefix("screen_").removesuffix("_refresh").upper()
    code+=f"\nvoid {name}(){{fixture_refresh({screen});}}\n"
for name in sorted(set(re.findall(r"\b(screen_\w+_tick)\(",tick))):
    code+=f"\nvoid {name}(){{}}\n"
code+='\nvoid screen_kredite_sync_completed(bool,const char*){}\n'
code+=function(manager,"static uint32_t content_hash(Screen screen)")
code+=function(manager,"static bool scrolling(lv_obj_t *object)")
code+=refresh+show+tick
code+=r'''
static uint32_t pixels[1280*800];
static void flush(lv_display_t *d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);}
static void root_setup(int i) {
    auto s=(Screen)i;s_screens[i]=lv_obj_create(nullptr);
    lv_obj_set_flex_flow(s_screens[i],LV_FLEX_FLOW_COLUMN);
    lists[i]=lv_obj_create(s_screens[i]);lv_obj_set_size(lists[i],600,200);
    drafts[i]=lv_textarea_create(s_screens[i]);lv_group_add_obj(group,drafts[i]);
    fixture_refresh(s);
}
static void mutate_content(unsigned n) {
    g_store.portalSpielerCount=1;g_store.portalSpieler[0].id=1;
    snprintf(g_store.portalSpieler[0].name,sizeof(g_store.portalSpieler[0].name),"changed-%u",n);
    g_store.historyCount=1;
    ((unsigned char*)&g_store.history[0])[0]=(unsigned char)n;
    ((unsigned char*)&g_store.lastFinished)[0]=(unsigned char)n;
}
int main() {
    lv_init();auto *display=lv_display_create(1280,800);
    lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,flush);
    group=lv_group_create();lv_group_set_default(group);
    static_assert(!std::is_copy_constructible<ViewSnapshot>::value,"watcher addresses cannot move");
    for(int i=0;i<SCREEN_COUNT;i++)root_setup(i);
    published.status=SYNC_SUCCESS;
    unsigned generation=1;
    for(int round=0;round<3;round++)for(int i=0;i<SCREEN_COUNT;i++) {
        auto s=(Screen)i;
        // Boot-sync generation is pending before navigation, as in the panic.
        mutate_content(generation);published.publicationGeneration=++generation;
        g_store.screen=s;ui_manager_show(s);
        lv_textarea_set_text(drafts[i],"operator draft");
        lv_group_focus_obj(row_fields[i]); // this focus target will be deleted
        s_content_hash[i]=0;int before=rebuilds[i];
        ui_manager_tick();
        assert(s_current==s&&s_sync_publication_seen==published.publicationGeneration);
        if(s!=SCREEN_SPIEL) {
            assert(rebuilds[i]==before+1);
            assert(!strcmp(lv_textarea_get_text(drafts[i]),"operator draft"));
            assert(!strcmp(lv_textarea_get_text(row_fields[i]),"new row"));
        }
        // Another publication while focused in a surviving form preserves focus.
        lv_group_focus_obj(drafts[i]);lv_textarea_set_text(drafts[i],"second draft");
        mutate_content(generation);published.publicationGeneration=++generation;
        s_content_hash[i]=0;ui_manager_tick();
        assert(lv_group_get_focused(group)==drafts[i]);
        assert(!strcmp(lv_textarea_get_text(drafts[i]),"second draft"));
        printf("PASS pending/changed publication round=%d screen=%d\n",round,i);
    }
    // Delete every captured target, immediately create replacements, and ensure
    // neither text nor focus is restored onto a semantically unrelated widget.
    auto *root=lv_obj_create(nullptr);auto *list=lv_obj_create(root);
    auto *ta=lv_textarea_create(list);lv_textarea_set_text(ta,"deleted draft");
    lv_group_add_obj(group,ta);lv_group_focus_obj(ta);
    {
        ViewSnapshot snapshot;capture_view(root,snapshot);lv_obj_clean(root);
        assert(!snapshot.focused.get());
        for(int n=0;n<snapshot.text_count;n++)assert(!snapshot.text[n].object.get());
        auto *replacement=lv_textarea_create(root);lv_textarea_set_text(replacement,"new identity");
        restore_view(root,snapshot);assert(!strcmp(lv_textarea_get_text(replacement),"new identity"));
    }
    // Snapshot teardown removes watchers from widgets that survived the rebuild.
    auto *survivor=lv_textarea_create(root);
    uint32_t original=lv_obj_get_event_count(survivor);
    {ViewSnapshot snapshot;capture_view(root,snapshot);assert(lv_obj_get_event_count(survivor)>original);}
    assert(lv_obj_get_event_count(survivor)==original);lv_obj_clean(root);
    // Settings keeps a persistent latency-label reference via the same guard.
    SnapshotWidgetRef latency;
    auto *latency_label=lv_label_create(root);latency.reset(latency_label);
    lv_obj_delete(latency_label);assert(!latency.get());
    // A VALUE_CHANGED callback may delete later snapshot entries during restore.
    auto *one=lv_textarea_create(root),*two=lv_textarea_create(root);
    lv_textarea_set_text(one,"draft A");lv_textarea_set_text(two,"draft B");
    {
        ViewSnapshot snapshot;capture_view(root,snapshot);
        lv_textarea_set_text(one,"server A");lv_textarea_set_text(two,"server B");
        lv_obj_add_event_cb(one,[](lv_event_t *e){
            lv_obj_delete((lv_obj_t*)lv_event_get_user_data(e));
        },LV_EVENT_VALUE_CHANGED,two);
        restore_view(root,snapshot);assert(!strcmp(lv_textarea_get_text(one),"draft A"));
    }
    puts("PASS all screen routes: ASan/UBSan, deleted/recreated scroll/text/focus, watcher teardown, deletion during restore");
}
'''
write("main.cpp",code)
write("CMakeLists.txt",f'''
    cmake_minimum_required(VERSION 3.16)
    project(view_snapshot_test C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(LV_CONF_PATH "{BUILD/'lv_conf.h'}" CACHE PATH "" FORCE)
    add_subdirectory("{ROOT/'managed_components/lvgl__lvgl'}" lvgl)
    add_executable(view_snapshot_test main.cpp)
    target_compile_options(view_snapshot_test PRIVATE -UNDEBUG)
    target_include_directories(view_snapshot_test PRIVATE . "{ROOT}/main" "{ROOT}/main/ui" "{ROOT}/main/store")
    target_link_libraries(view_snapshot_test PRIVATE lvgl m)
''')
flags="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
subprocess.run(["cmake","-S",str(BUILD),"-B",str(BUILD/"build"),
    "-DCMAKE_BUILD_TYPE=Debug",f"-DCMAKE_C_FLAGS={flags}",f"-DCMAKE_CXX_FLAGS={flags}"],check=True)
subprocess.run(["cmake","--build",str(BUILD/"build"),"--target","view_snapshot_test","-j","6"],check=True)
env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0:detect_stack_use_after_return=1"}
exe=str(BUILD/"build/view_snapshot_test")
subprocess.run([exe],check=True,env=env)
