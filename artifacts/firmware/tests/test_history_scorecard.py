#!/usr/bin/env python3
"""Compile real scorecard/history callbacks with actual store types and LVGL mocks."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("batch", Path(__file__).with_name("test_machine_batch.py"))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

STUBS = r"""
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <map>
#include "history_scorecard.h"
#include "ui_time_fmt.h"
GameStore g_store = {};
const char *modus_label(Modus mode) {
    const char *labels[]={"Normal","Harakiri","Custom 1","Custom 2","Custom 3","Custom 4","Harakiri Custom"};
    return labels[mode];
}
struct lv_draw_dsc_base_t { int part; uint32_t id1,id2; };
struct lv_draw_fill_dsc_t { int color,opa; };
struct lv_draw_task_t { lv_draw_dsc_base_t base; lv_draw_fill_dsc_t *fill; };
struct lv_event_t { void *data; lv_draw_task_t *task=nullptr; };
constexpr int LV_PART_ITEMS=1;
lv_draw_task_t *lv_event_get_draw_task(lv_event_t *e) { return e->task; }
void *lv_draw_task_get_draw_dsc(lv_draw_task_t *task) { return &task->base; }
lv_draw_fill_dsc_t *lv_draw_task_get_fill_dsc(lv_draw_task_t *task) { return task->fill; }
struct lv_obj_t {
    std::string label; std::vector<std::vector<std::string>> rows;
    std::vector<int> widths; std::vector<lv_obj_t*> children;
    lv_obj_t *parent=nullptr; int flags=0, scroll_x=0, scroll_y=0, cols=0;
    void (*callback)(lv_event_t*)=nullptr; void *event_data=nullptr;
};
constexpr int LV_OBJ_FLAG_HIDDEN=1, LV_ANIM_OFF=0, LV_TABLE_CELL_CTRL_TEXT_CROP=1;
constexpr int CLR_CARD=0, CLR_BORDER=0, CLR_MUTED=0, CLR_PRIMARY=0, CLR_TEXT=0;
constexpr int LV_OPA_COVER=0, LV_FLEX_FLOW_COLUMN=0, LV_FLEX_ALIGN_CENTER=0, LV_FLEX_ALIGN_START=0;
constexpr int UI_FONT_12=0, UI_FONT_14=0, UI_FONT_16=0, LV_LABEL_LONG_DOT=0, LV_EVENT_CLICKED=0;
#define LV_SYMBOL_CHARGE ""
#define LIST_W 300
struct cJSON {
    int type=0, valueint=0; double valuedouble=0;
    std::string text; char *valuestring=nullptr;
    std::map<std::string,cJSON*> fields;std::vector<cJSON*> elements;
};
bool cJSON_IsArray(cJSON *v) { return v && v->type==1; }
bool cJSON_IsString(cJSON *v) { return v && v->type==2; }
bool cJSON_IsNumber(cJSON *v) { return v && v->type==3; }
bool cJSON_IsBool(cJSON *v) { return v && v->type==4; }
bool cJSON_IsTrue(cJSON *v) { return cJSON_IsBool(v) && v->valueint; }
int cJSON_GetArraySize(cJSON *v) { return (int)v->elements.size(); }
cJSON *cJSON_GetObjectItemCaseSensitive(cJSON *v,const char *key) {
    auto i=v->fields.find(key);return i==v->fields.end()?nullptr:i->second;
}
#define cJSON_ArrayForEach(item,arr) for(size_t j=0;j<(arr)->elements.size() && ((item)=(arr)->elements[j],true);++j)
cJSON *number(int n) { auto *v=new cJSON;v->type=3;v->valueint=n;v->valuedouble=n;return v; }
cJSON *boolean(bool b) { auto *v=number(b);v->type=4;return v; }
cJSON *string(const char *s) { auto *v=new cJSON;v->type=2;v->text=s;v->valuestring=v->text.data();return v; }
cJSON *shot(bool repeated=false) {
    auto *v=new cJSON;
    v->fields={{"spielerId",number(11)},{"lauf",number(2)},{"taube",number(3)},
        {"maschine",string("H")},{"posten",number(4)},{"punkte",number(1)},
        {"schuss1",boolean(true)},{"schuss2",boolean(false)},{"wiederholt",boolean(repeated)}};
    return v;
}
static FinishedGame s_selected_game;
static bool s_has_selected_game, s_history_built;
static uint32_t s_history_signature;
static bool s_doublette_columns[MAX_ERGEBNISSE+2];
static lv_obj_t *s_list, *s_lbl_empty, *s_detail_card, *s_detail_hdr, *s_detail_date, *s_detail_table;
int table_writes=0, list_rebuilds=0;
void lv_label_set_text(lv_obj_t *o,const char *text) { o->label=text; }
void lv_table_set_col_cnt(lv_obj_t *o,int cols) {
    o->cols=cols;o->widths.resize(cols);for(auto &row:o->rows)row.resize(cols);
}
void lv_table_set_row_cnt(lv_obj_t *o,int rows) {
    o->rows.resize(rows);for(auto &row:o->rows)row.resize(o->cols);
}
void lv_table_set_col_width(lv_obj_t *o,int col,int width) { o->widths.at(col)=width; }
void lv_table_set_cell_value(lv_obj_t *o,int row,int col,const char *text) {
    o->rows.at(row).at(col)=text;++table_writes;
}
void lv_table_set_cell_ctrl(lv_obj_t *,int,int,int) {}
void lv_obj_add_flag(lv_obj_t *o,int f) { o->flags|=f; }
void lv_obj_clear_flag(lv_obj_t *o,int f) { o->flags&=~f; }
lv_obj_t *lv_obj_get_parent(lv_obj_t *o) { return o->parent; }
void lv_obj_scroll_to_x(lv_obj_t *o,int x,int) { o->scroll_x=x; }
void lv_obj_scroll_to_y(lv_obj_t *o,int y,int) { o->scroll_y=y; }
int lv_obj_get_scroll_y(lv_obj_t *o) { return o->scroll_y; }
void lv_obj_clean(lv_obj_t *o) { o->children.clear();if(o==s_list)++list_rebuilds; }
lv_obj_t *lv_list_add_btn(lv_obj_t *parent,void *,const char *) {
    auto *o=new lv_obj_t;o->parent=parent;parent->children.push_back(o);return o;
}
lv_obj_t *lv_label_create(lv_obj_t *parent) { return lv_list_add_btn(parent,nullptr,""); }
int lv_color_hex(int v) { return v; }
void *lv_event_get_user_data(lv_event_t *e) { return e->data; }
void lv_obj_add_event_cb(lv_obj_t *o,void (*fn)(lv_event_t*),int,void *data) {
    o->callback=fn;o->event_data=data;
}
"""

STYLES = "\n".join(
    f"template<typename... T> void {name}(T...) {{}}"
    for name in [
        "lv_obj_set_height", "lv_obj_set_width", "lv_obj_set_style_bg_color",
        "lv_obj_set_style_bg_opa", "lv_obj_set_style_border_color",
        "lv_obj_set_style_border_width", "lv_obj_set_style_radius",
        "lv_obj_set_style_pad_hor", "lv_obj_set_style_pad_ver",
        "lv_obj_set_flex_flow", "lv_obj_set_flex_align", "lv_obj_set_style_text_font",
        "lv_obj_set_style_text_color", "lv_label_set_long_mode"
    ]
) + "\n"

MAIN = r"""
FinishedGame game(Modus mode,int clays,int runs) {
    FinishedGame fg={};strcpy(fg.base.externalId,"selected-game");strcpy(fg.finishedAt,"2026-10-04T10:00:00.000Z");
    fg.base.modus=mode;fg.base.lauf=runs;fg.base.taubenProLauf=clays;
    fg.spieler_count=2;fg.spielerIds[0]=11;fg.spielerIds[1]=22;
    strcpy(fg.spielerNamen[0],"Andy");strcpy(fg.spielerNamen[1],"Ben");
    fg.base.teilnahmen_count=2;fg.base.teilnahmen[0].spielerId=11;fg.base.teilnahmen[1].spielerId=22;
    const int photo[]={1,0,2,1,2,2,1,0,0,1,2,2,1,0,2,2,2,2};
    for(int run=1;run<=runs;++run)for(int clay=1;clay<=clays;++clay)for(int player=0;player<2;++player) {
        auto &e=fg.base.ergebnisse[fg.base.ergebnisse_count++];
        e.spielerId=fg.spielerIds[player];e.lauf=run;e.taube=clay;
        e.maschine=(Maschine)((clay-1)%MASCHINE_COUNT);
        if(clays==9 && clay==9)e.maschine=MASCHINE_H;
        int position=clay-1-(clays==9 && clay==9 ? 1 : 0);
        e.posten=(position+player)%5+1;
        e.punkte=clays==9 && runs==2 && player==0 ? photo[(run-1)*clays+clay-1] : (clay+player)%3;
        fg.base.teilnahmen[player].punkte+=e.punkte;
    }
    return fg;
}
int main() {
    lv_obj_t list,empty,card,hdr,date,table,viewport;
    s_list=&list;s_lbl_empty=&empty;s_detail_card=&card;s_detail_hdr=&hdr;s_detail_date=&date;s_detail_table=&table;
    table.parent=&viewport;
    g_store.historyCount=1;g_store.history[0]=game(MODUS_NORMAL,9,2);
    screen_geschichte_refresh();assert(card.flags&LV_OBJ_FLAG_HIDDEN);
    show_detail(0);
    assert(table.rows.size()==3 && table.cols==20 && !(card.flags&LV_OBJ_FLAG_HIDDEN));
    assert(table.rows[1][0]=="Andy" && table.rows[2][0]=="Ben");
    assert(table.rows[1][1]=="1" && table.rows[1][2]=="0" && table.rows[1][19]=="23");
    const char *headings[]={"A","B","C","D","E","F","G","H","H","A","B","C","D","E","F","G","H","H"};
    for(int col=0;col<18;++col)assert(table.rows[0][col+1]==headings[col]);
    assert(table.rows[0][19]=="GESAMT");
    for(int col=0;col<20;++col)
        assert(s_doublette_columns[col]==(col==8 || col==9 || col==17 || col==18));
    // Actual draw callback: shade both headers and player cells, but never
    // names, totals, singles, labels/borders or unrelated drawing parts.
    for(int row=0;row<3;++row)for(int col=0;col<20;++col) {
        lv_draw_fill_dsc_t fill{CLR_CARD,17};
        lv_draw_task_t task{{LV_PART_ITEMS,(uint32_t)row,(uint32_t)col},&fill};
        lv_event_t event{nullptr,&task};scorecard_draw_cb(&event);
        assert(fill.color==(s_doublette_columns[col] ? 0x1C2B40 : CLR_CARD));
        assert(fill.opa==(s_doublette_columns[col] ? LV_OPA_COVER : 17));
    }
    lv_draw_fill_dsc_t ignored_fill{CLR_CARD,17};
    lv_draw_task_t other_part{{0,0,8},&ignored_fill};lv_event_t other_event{nullptr,&other_part};
    scorecard_draw_cb(&other_event);assert(ignored_fill.color==CLR_CARD);
    other_part.base.part=LV_PART_ITEMS;other_part.fill=nullptr;scorecard_draw_cb(&other_event);
    other_part.fill=&ignored_fill;other_part.base.id2=MAX_ERGEBNISSE+2;
    scorecard_draw_cb(&other_event);assert(ignored_fill.color==CLR_CARD);
    int width=0;for(int w:table.widths)width+=w;assert(width<=892); // normal scorecard fits detail viewport
    int writes=table_writes,rebuilds=list_rebuilds;
    list.scroll_y=149;viewport.scroll_x=60;viewport.scroll_y=18;
    screen_geschichte_refresh();assert(table_writes==writes && list_rebuilds==rebuilds);
    // Sync reorders/replaces the cache; selected results and both scroll positions must remain.
    g_store.history[1]=g_store.history[0];
    g_store.history[0]=game(MODUS_CUSTOM_4,16,1);strcpy(g_store.history[0].base.externalId,"new-game");
    g_store.historyCount=2;screen_geschichte_refresh();
    assert(table_writes==writes && table.rows[1][19]=="23" && !(card.flags&LV_OBJ_FLAG_HIDDEN));
    assert(list.scroll_y==149 && viewport.scroll_x==60 && viewport.scroll_y==18);
    for(int col=0;col<20;++col)
        assert(s_doublette_columns[col]==(col==8 || col==9 || col==17 || col==18));
    // Even eviction from the 20-game cache or an empty response cannot hide the open card.
    g_store.historyCount=0;screen_geschichte_refresh();
    assert(table.rows[1][19]=="23" && !(card.flags&LV_OBJ_FLAG_HIDDEN));
    // A live pull can enrich old totals-only snapshots, without clearing or
    // moving the open card and without reordering its player names.
    g_store.history[0]=game(MODUS_NORMAL,9,2);
    auto rich=g_store.history[0];g_store.history[0].base.ergebnisse_count=0;
    g_store.historyCount=1;show_detail(0);assert(table.rows[1][1]=="-");
    viewport.scroll_x=60;viewport.scroll_y=18;
    g_store.history[0]=rich;screen_geschichte_refresh();
    assert(table.rows[1][1]=="1" && table.rows[1][19]=="23" &&
        viewport.scroll_x==60 && viewport.scroll_y==18 && !(card.flags&LV_OBJ_FLAG_HIDDEN));
    writes=table_writes;
    g_store.history[0].base.ergebnisse[0].punkte=0;
    g_store.history[0].base.ergebnisse[g_store.history[0].base.ergebnisse_count++]=
        g_store.history[0].base.ergebnisse[0];
    screen_geschichte_refresh();
    assert(table_writes==writes && table.rows[1][1]=="1"); // never overwrite visible recorded results
    // Selecting another game deliberately replaces the card, with its own recorded shape.
    for(int mode=MODUS_NORMAL;mode<MODUS_COUNT;++mode) {
        int clays=mode>=MODUS_CUSTOM_1 ? 2+mode*2 : 9;
        int runs=mode>=MODUS_CUSTOM_1 ? 1 : 2;
        g_store.history[0]=game((Modus)mode,clays,runs);g_store.historyCount=1;
        show_detail(0);assert(table.cols==clays*runs+2 && table.rows.size()==3);
        assert(table.rows[1].back()==std::to_string(g_store.history[0].base.teilnahmen[0].punkte));
        for(int col=1;col<=clays*runs;++col) {
            int clay=(col-1)%clays+1;
            assert(s_doublette_columns[col]==(clays==9 && (clay==8 || clay==9)));
        }
    }
    // Custom A/B and E/F pairs, with C/D singles, in both recorded rounds.
    for(int mode=MODUS_CUSTOM_1;mode<=MODUS_CUSTOM_4;++mode) {
        auto custom=game((Modus)mode,6,2);
        const int stands[]={1,1,2,3,4,4};
        for(int i=0;i<custom.base.ergebnisse_count;++i) {
            auto &e=custom.base.ergebnisse[i];
            e.posten=(stands[e.taube-1]-1+(e.spielerId==22 ? 1 : 0)+4*(e.lauf-1))%5+1;
        }
        g_store.history[0]=custom;show_detail(0);
        for(int col=1;col<=12;++col) {
            int clay=(col-1)%6+1;
            assert(s_doublette_columns[col]==(clay==1 || clay==2 || clay==5 || clay==6));
        }
        assert(!s_doublette_columns[0] && !s_doublette_columns[13]);
        // Evidence from another player still marks the whole pair if one
        // player's individual result is missing.
        for(int i=0;i<custom.base.ergebnisse_count;++i)
            if(custom.base.ergebnisse[i].spielerId==11 && custom.base.ergebnisse[i].taube==2)
                custom.base.ergebnisse[i].wiederholt=true;
        assert(history_scorecard_doublette(&custom,1,1) && history_scorecard_doublette(&custom,1,2));
    }
    auto singles=game(MODUS_CUSTOM_4,2,2);
    for(int i=0;i<singles.base.ergebnisse_count;++i)singles.base.ergebnisse[i].maschine=MASCHINE_H;
    g_store.history[0]=singles;show_detail(0);
    for(bool marked:s_doublette_columns)assert(!marked); // H/H singles are not a doublette
    auto boundary=game(MODUS_CUSTOM_1,1,2);
    assert(!history_scorecard_doublette(&boundary,1,1) && !history_scorecard_doublette(&boundary,2,1));
    auto unknown=game(MODUS_CUSTOM_2,2,1);
    for(int i=0;i<unknown.base.ergebnisse_count;++i)unknown.base.ergebnisse[i].posten=0;
    assert(!history_scorecard_doublette(&unknown,1,1)); // unknown stand is not pair evidence
    auto fg=game(MODUS_CUSTOM_2,3,2);
    // Array order is irrelevant; identify results by player + run + clay.
    std::swap(fg.base.ergebnisse[0],fg.base.ergebnisse[11]);
    assert(history_scorecard_result(&fg,11,1,1)->punkte==1);
    auto duplicate=*history_scorecard_result(&fg,11,1,1);duplicate.wiederholt=true;duplicate.punkte=2;
    fg.base.ergebnisse[fg.base.ergebnisse_count++]=duplicate;
    assert(history_scorecard_result(&fg,11,1,1)->punkte==1);
    for(int i=0;i<fg.base.ergebnisse_count;++i)
        if(fg.base.ergebnisse[i].spielerId==11 && fg.base.ergebnisse[i].lauf==1 && fg.base.ergebnisse[i].taube==1)
            fg.base.ergebnisse[i].wiederholt=true;
    g_store.history[0]=fg;show_detail(0);assert(table.rows[1][1]=="-");
    fg.base.teilnahmen_count=0;
    int fallback=0;
    for(int run=1;run<=2;++run)for(int clay=1;clay<=3;++clay) {
        auto *e=history_scorecard_result(&fg,11,run,clay);if(e)fallback+=e->punkte;
    }
    assert(history_scorecard_total(&fg,11)==fallback);
    fg.base.taubenProLauf=0;fg.base.lauf=0;
    auto shape=history_scorecard_shape(&fg);assert(shape.clays==3 && shape.runs==2);
    fg.base.taubenProLauf=100000;fg.base.lauf=100000;
    shape=history_scorecard_shape(&fg);assert(shape.clays*shape.runs<=MAX_ERGEBNISSE);
    g_store.history[0]={};g_store.history[0].spieler_count=1;g_store.historyCount=1;
    show_detail(0);assert(table.cols==2);
    char unknown_heading[8];
    history_scorecard_heading(&g_store.history[0],1,1,unknown_heading,sizeof(unknown_heading));
    assert(std::string(unknown_heading)=="-");
    FinishedGame downloaded={};cJSON results;results.type=1;
    results.elements={shot(),shot(true)};
    assert(parse_history_results(&results,&downloaded) && downloaded.base.ergebnisse_count==2);
    const auto &decoded=downloaded.base.ergebnisse[0];
    assert(decoded.spielerId==11 && decoded.lauf==2 && decoded.taube==3 &&
        decoded.maschine==MASCHINE_H && decoded.posten==4 && decoded.punkte==1 &&
        decoded.schuss1 && !decoded.schuss2 && !decoded.wiederholt);
    assert(downloaded.base.ergebnisse[1].wiederholt);
    results.elements[0]->fields.erase("punkte");
    assert(!parse_history_results(&results,&downloaded));
    assert(parse_history_results(nullptr,&downloaded)); // legacy totals-only response
    results.elements={shot()};results.elements[0]->fields["lauf"]->valuedouble=1.5;
    assert(!parse_history_results(&results,&downloaded));
    results.elements.assign(MAX_ERGEBNISSE+1,nullptr);
    assert(!parse_history_results(&results,&downloaded));
    puts("PASS: one player per row; photo-style 18-clay scorecard and total 23; all six modes use recorded runs/sequence");
    puts("PASS: zero versus missing results; repeated attempts excluded; unordered results mapped; totals and metadata fallback");
    puts("PASS: sync/no-change/reorder/eviction retains open results and scroll; deliberate game selection replaces the scorecard");
    puts("PASS: downloaded clay results retain machine/round/player/shots/repetition; malformed and oversized data rejected before publication");
    puts("PASS: normal H/H and all Custom A-G doublettes shade both columns across rounds; singles/unknown data/boundaries stay plain");
    puts("PASS: real draw callback shades headers and player cells only; switching games resets shading; sync keeps the saved pair mask");
}
"""

if __name__ == "__main__":
    source = (ROOT / "main/ui/screen_geschichte.cpp").read_text()
    functions = "\n".join(fixture.function(source, name) for name in [
        "static void scorecard_draw_cb", "static void render_selected_detail", "static void show_detail",
        "static uint32_t history_rows_signature", "static void build_history_rows",
        "static void refresh_selected_history_results",
        "void screen_geschichte_refresh"
    ])
    network = (ROOT / "main/net/http_sync.cpp").read_text()
    assert "scorecard_draw_cb, LV_EVENT_DRAW_TASK_ADDED" in source
    assert "LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS" in source
    assert 'strcmp(ms, "HARAKIRI")' in fixture.function(network, "esp_err_t http_fetch_spielhistorie")
    assert "parse_history_results(" in fixture.function(network, "esp_err_t http_fetch_spielhistorie")
    parser = fixture.function(network, "static bool parse_history_results")
    with tempfile.TemporaryDirectory(prefix="tm-history-scorecard-") as directory:
        path = Path(directory)
        (path / "test.cpp").write_text(STUBS + STYLES + parser + functions + MAIN)
        subprocess.run(["g++", "-std=c++17", "-Os", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "main/ui"), "-I", str(ROOT / "main/store"),
                        str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)