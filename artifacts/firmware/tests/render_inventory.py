"""Render/test the production LVGL inventory screen at 1280x800, without ESP hardware.

Uses the resolved LVGL source and project styles/fonts, not an HTML recreation.
Only store/transport/shutdown services are replaced by deterministic host fixtures.
Run: python artifacts/firmware/tests/render_inventory.py
"""
from pathlib import Path
import argparse
import subprocess
import textwrap
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--build-dir", default="/tmp/trapmaster-inventory-render")
parser.add_argument("--output", default="generated-artifacts/inventory")
args = parser.parse_args()
build = Path(args.build_dir)
build.mkdir(parents=True, exist_ok=True)
out = Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=True)

def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]

def write(name, content):
    path = build / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content))

write("nvs.h", "#pragma once\n#include <stdint.h>\ntypedef uint32_t nvs_handle_t;\n")
write("cJSON.h", "#pragma once\ntypedef struct cJSON cJSON;\n")
write("driver/gpio.h", "#pragma once\n")
conf = """
    #pragma once
    #define LV_CONF_H
    #define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_XRGB8888
    #define LV_USE_OS LV_OS_NONE
    #define LV_USE_LOG 1
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_MEM_SIZE (12*1024*1024)
    #define LV_USE_SNAPSHOT 1
"""
for size in (12, 14, 16, 18, 20, 22, 24, 28, 36, 48):
    conf += f"\n#define LV_FONT_MONTSERRAT_{size} 1\n"
write("lv_conf.h", conf)
manager = (ROOT / "main/ui/ui_manager.cpp").read_text()
styles = "\n".join(f"lv_style_t {name};" for name in (
    "g_style_card", "g_style_btn_primary", "g_style_btn_secondary", "g_style_btn_danger",
    "g_style_label_title", "g_style_label_mono", "g_style_sidebar"))
styles += "\n" + function(manager, "static void styles_init(void)")
styles += "\n" + function(manager, "void screen_base_init(lv_obj_t *scr)")
harness = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include "screen_inventory.cpp"
#include "terminal_logs.h"
GameStore g_store{};
static DayInventory fixture{};
static std::vector<DayEventKind> queued;
static int syncs=0,shutdowns=0,glyphCalls=0;
static bool queueOK=true;
void terminal_log_add(const char *type,const char*,const char*,const cJSON*) {if(!strcmp(type,"unsupported_glyph"))glyphCalls++;}
void day_inventory_snapshot(DayInventory *out) {*out=fixture;}
bool day_inventory_queue(DayEventKind kind,int a,int b,const int *ids,int count,bool taken,const char *text,const char *expectedDate) {
    if(!queueOK)return false;
    if(expectedDate&&strcmp(expectedDate,fixture.datum))return false;
    queued.push_back(kind);
    if(kind==DAY_MAITRES){fixture.maitreCount=count;memcpy(fixture.maitreIds,ids,count*sizeof(int));}
    if(kind==DAY_OPENING){fixture.hasOpening=true;fixture.opening12=a;fixture.opening20=b;fixture.takenOver=taken;}
    if(kind==DAY_CLOSING){fixture.hasClosing=true;fixture.closing12=a;fixture.closing20=b;snprintf(fixture.closingNote,sizeof(fixture.closingNote),"%s",text);}
    return true;
}
bool store_sync() {syncs++;return true;}
void screen_dashboard_continue_shutdown() {shutdowns++;}
void screen_dashboard_shutdown_without_inventory() {shutdowns++;}
void ui_manager_show(Screen) {screen_inventory_refresh();}
'''
harness += styles
harness += r'''
static unsigned char pixels[1280*800*4];
static void flush(lv_display_t *d,const lv_area_t*,uint8_t*) {lv_display_flush_ready(d);}
static void capture(const char *name) {
    lv_obj_update_layout(screen);lv_refr_now(nullptr);
    char path[512];snprintf(path,sizeof(path),"%s/%s.ppm",OUTPUT,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1280 800\n255\n");
    for(int i=0;i<1280*800;i++){unsigned char rgb[]={pixels[i*4+2],pixels[i*4+1],pixels[i*4]};fwrite(rgb,1,3,f);}
    fclose(f);
}
static void click(lv_obj_t *o) {lv_obj_send_event(o,LV_EVENT_CLICKED,nullptr);}
int main() {
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();
    lv_init();ui_fonts_init();styles_init();
    lv_display_t *d=lv_display_create(1280,800);
    lv_display_set_theme(d,lv_theme_default_init(d,lv_color_hex(CLR_PRIMARY),lv_color_hex(CLR_SUCCESS),true,UI_FONT_18));
    lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
    screen_inventory_create();lv_screen_load(screen);
    strcpy(fixture.datum,"2026-10-18");screen_inventory_open(false);
    capture("01-empty");
    const char *names[]={"José Muñoz","Zoë","François","Çağlar","ÉMILIE","Inactive"};
    g_store.portalSpielerCount=6;
    for(int i=0;i<6;i++){auto &p=g_store.portalSpieler[i];p.id=i+1;p.aktiv=i!=5;p.isMaitreDeTir=true;strcpy(p.name,names[i]);}
    screen_inventory_tick();
    for(int i=1;i<=4;i++)add_maitre(i);
    add_maitre(5);assert(selectedCount==4);add_maitre(1);assert(selectedCount==4);
    assert(display_name_matches("José Muñoz","JOSÉ")&&display_name_matches("José Muñoz","jose"));
    assert(display_name_matches("Çağlar","çaĞLAR")&&!display_name_matches("Zoë","François"));
    assert(!maitre(6));capture("02-chosen");
    fixture.previousValid=true;fixture.previous12=120;fixture.previous20=40;strcpy(fixture.previousDate,"2026-10-17");
    screen_inventory_tick();lv_obj_add_state(carry,LV_STATE_CHECKED);lv_obj_send_event(carry,LV_EVENT_VALUE_CHANGED,nullptr);
    assert(!strcmp(lv_textarea_get_text(numbers[0][0]),"120")&&lv_obj_has_state(numbers[0][0],LV_STATE_DISABLED));
    capture("03-carry-over");
    auto *identity=numbers[0][0];int scroll=lv_obj_get_scroll_y(cards[0]);
    fixture.closing12=17;screen_inventory_tick();
    assert(numbers[0][0]==identity&&lv_obj_get_scroll_y(cards[0])==scroll);
    assert(!strcmp(lv_textarea_get_text(numbers[0][0]),"120")); // sync preserves draft
    lv_obj_send_event(numbers[1][0],LV_EVENT_FOCUSED,nullptr);
    assert(!lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));
    lv_obj_update_layout(screen);
    assert(lv_obj_get_y2(numbers[1][0])<lv_obj_get_y(keyboard));hide_keyboard();
    // Save opening, preserve immutable queue order and selected IDs.
    lv_obj_t *saveOpening=nullptr;
    for(uint32_t i=0;i<lv_obj_get_child_count(cards[0]);i++){
        auto *child=lv_obj_get_child(cards[0],i);
        if(lv_obj_check_type(child,&lv_button_class))saveOpening=child;
    }
    assert(saveOpening);click(saveOpening);
    assert(queued.size()==2&&queued[0]==DAY_MAITRES&&queued[1]==DAY_OPENING);
    g_store.billDayBaseline.authoritative=true;strcpy(g_store.billDayBaseline.datum,fixture.datum);
    g_store.billDayBaseline.ammoSold[0]=20;g_store.billDayBaseline.ammoSold[1]=5;g_store.billDayBaseline.openTotalCent=1200;
    strcpy(fixture.openingTerminal,"ESP32-P4-102030405060");strcpy(fixture.openingAt,"2026-10-18T08:10:00Z");
    screen_inventory_open(true,true);
    fill_number(numbers[1][0],97,true);fill_number(numbers[1][1],35,true);
    lv_textarea_set_text(note,"François: 3 Einheiten fehlen.");difference();hide_keyboard();
    assert(strstr(lv_label_get_text(differenceLabel),"+3"));
    capture("04-end-of-day-difference");
    // Live portal updates do not replace inputs or note while typing.
    fixture.previous12=121;screen_inventory_tick();
    assert(!strcmp(lv_textarea_get_text(numbers[1][0]),"97"));
    assert(!strcmp(lv_textarea_get_text(note),"François: 3 Einheiten fehlen."));
    // Day rollover: no new event and no implicit discard of the draft.
    strcpy(fixture.datum,"2026-10-19");size_t before=queued.size();
    click(saveOpening);assert(queued.size()==before);
    assert(!strcmp(lv_textarea_get_text(numbers[1][0]),"97"));
    // Skip exists only in shutdown context and requires two taps.
    int old=shutdowns;click(skipButton);assert(shutdowns==old);click(skipButton);assert(shutdowns==old+1);
    screen_inventory_open(false,false);assert(lv_obj_has_flag(skipButton,LV_OBJ_FLAG_HIDDEN));
    // Unknown/invalid scalars become '?', Latin names are preserved exactly.
    char safe[128];utf8_to_display(safe,"José Muñoz / Zoë / François / Çağlar / 🧪",sizeof(safe));
    assert(!strcmp(safe,"José Muñoz / Zoë / François / Çağlar / ?")&&glyphCalls==1);
    utf8_to_display(safe,"\xED\xA0\x80",sizeof(safe));assert(!strcmp(safe,"???"));
    for(uint32_t cp=0xa0;cp<=0x17f;cp++){
        lv_font_glyph_dsc_t gd{};assert(lv_font_get_glyph_dsc(UI_FONT_20,&gd,cp,0));
    }
    puts("PASS actual LVGL inventory: selection/search, carry locks, immutable enqueue, keyboard visibility, sync/draft/scroll preservation, date guard, shutdown two-tap, Latin glyphs and safe replacements");
}
'''
harness = harness.replace("OUTPUT", '"' + str(out) + '"')
write("main.cpp", harness)
write("CMakeLists.txt", f'''
    cmake_minimum_required(VERSION 3.16)
    project(inventory_render C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(LV_CONF_PATH "{build / 'lv_conf.h'}" CACHE PATH "" FORCE)
    set(LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    add_subdirectory("{ROOT / 'managed_components/lvgl__lvgl'}" lvgl)
    file(GLOB FONTS "{ROOT}/main/ui/ui_font_fallback_*.c")
    add_executable(inventory_render main.cpp "{ROOT}/main/ui/ui_fonts.cpp" "{ROOT}/main/ui/display_text.cpp" ${{FONTS}})
    target_compile_options(inventory_render PRIVATE -UNDEBUG)
    target_include_directories(inventory_render PRIVATE . "{ROOT}/main" "{ROOT}/main/ui" "{ROOT}/main/store")
    target_link_libraries(inventory_render PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(build), "-B", str(build / "build"),
                "-DCMAKE_BUILD_TYPE=Release"], check=True)
subprocess.run(["cmake", "--build", str(build / "build"), "--target", "inventory_render", "-j", "6"], check=True)
subprocess.run([str(build / "build/inventory_render")], check=True)
for ppm in out.glob("*.ppm"):
    rgb = ppm.read_bytes().split(b"\n", 3)[3]
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgb[y*1280*3:(y+1)*1280*3] for y in range(800))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB",1280,800,8,2,0,0,0))
    png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND",b"")
    ppm.with_suffix(".png").write_bytes(png)
