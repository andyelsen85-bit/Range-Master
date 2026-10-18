#include "screen_inventory.h"
#include "ui_manager.h"
#include "ui_fonts.h"
#include "display_text.h"
#include "game_store.h"
#include "day_inventory.h"
#include "screen_dashboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Widgets are built once. Sync updates labels/models, never the focused input
// or its scroll parent. All callbacks run under the existing LVGL publication lock.
static lv_obj_t *screen,*body,*keyboard,*dateLabel,*search,*results,*chips,*carry;
static lv_obj_t *cards[2],*status[2],*messages[2],*numbers[2][2],*note,*summary,*differenceLabel,*skipButton,*previousLabel;
static DayInventory view{};
static int selected[4],selectedCount;
static int sold[2],openBills;
static bool authoritative,dirty[2],selectionDirty,loading,reload=true,beforeShutdown,focusClosing,skipConfirmed;
static char openedDate[11];
static uint32_t rosterRevision;
static const char *numberMap[]={"1","2","3","\n","4","5","6","\n","7","8","9","\n",LV_SYMBOL_BACKSPACE,"0",LV_SYMBOL_OK,""};
static constexpr auto keyWidth=(lv_buttonmatrix_ctrl_t)1;
static const lv_buttonmatrix_ctrl_t numberControls[]={keyWidth,keyWidth,keyWidth,keyWidth,keyWidth,keyWidth,keyWidth,keyWidth,keyWidth,keyWidth,keyWidth,(lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_NO_REPEAT|1)};

static void dark_scrollbar(lv_obj_t *o) {
    lv_obj_set_style_bg_color(o,lv_color_hex(0x718096),LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,LV_PART_SCROLLBAR);
    lv_obj_set_style_width(o,6,LV_PART_SCROLLBAR);
}
static lv_obj_t *container(lv_obj_t *parent,bool card=false) {
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    if(card)lv_obj_add_style(o,&g_style_card,0);
    lv_obj_set_width(o,LV_PCT(100));lv_obj_set_height(o,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(o,10,0);
    lv_obj_set_style_text_color(o,lv_color_hex(CLR_TEXT),0);lv_obj_set_style_text_font(o,UI_FONT_18,0);
    dark_scrollbar(o);return o;
}
static lv_obj_t *label(lv_obj_t *p,const char *text,const lv_font_t *font=UI_FONT_18) {
    lv_obj_t *l=lv_label_create(p);lv_label_set_text(l,text);lv_obj_set_width(l,LV_PCT(100));
    lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);lv_obj_set_style_text_font(l,font,0);return l;
}
static void feedback(int column,const char *text,bool ok=false) {
    lv_label_set_text(messages[column],text);
    lv_obj_set_style_text_color(messages[column],lv_color_hex(ok?CLR_SUCCESS:CLR_WARN),0);
}
static lv_obj_t *button(lv_obj_t *p,const char *text,lv_event_cb_t cb,void *data=nullptr,bool primary=false) {
    lv_obj_t *b=lv_button_create(p);lv_obj_remove_style_all(b);
    lv_obj_add_style(b,primary?&g_style_btn_primary:&g_style_btn_secondary,0);
    lv_obj_set_size(b,LV_PCT(100),56);lv_obj_set_style_text_font(b,UI_FONT_20,0);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data);
    lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_set_width(l,LV_PCT(100));
    lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);return b;
}
static void hide_keyboard() {
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);lv_keyboard_set_textarea(keyboard,nullptr);
    lv_obj_add_flag(results,LV_OBJ_FLAG_HIDDEN);
}
static void show_results();
static void input_focus(lv_event_t *e) {
    lv_obj_t *ta=(lv_obj_t*)lv_event_get_target(e);
    skipConfirmed=false;
    if(ta==search){dirty[0]=true;selectionDirty=true;}
    else if(ta==note)dirty[1]=true;
    else for(int col=0;col<2;col++)for(int cal=0;cal<2;cal++)if(ta==numbers[col][cal])dirty[col]=true;
    bool text=ta==note||ta==search;
    lv_keyboard_set_textarea(keyboard,ta);
    lv_keyboard_set_mode(keyboard,text?LV_KEYBOARD_MODE_TEXT_LOWER:LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_height(keyboard,text?320:280);lv_obj_clear_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    if(ta==search)show_results();else lv_obj_add_flag(results,LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(screen);lv_obj_scroll_to_view_recursive(ta,LV_ANIM_OFF);
}
static lv_obj_t *input(lv_obj_t *p,bool numeric) {
    lv_obj_t *a=lv_textarea_create(p);lv_obj_remove_style_all(a);
    lv_obj_set_size(a,LV_PCT(100),56);lv_textarea_set_one_line(a,true);
    lv_obj_set_style_bg_color(a,lv_color_hex(CLR_BORDER),0);lv_obj_set_style_bg_opa(a,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(a,lv_color_hex(CLR_TEXT),0);lv_obj_set_style_text_font(a,UI_FONT_20,0);
    lv_obj_set_style_border_width(a,1,0);lv_obj_set_style_border_color(a,lv_color_hex(0x526079),0);
    lv_obj_set_style_border_color(a,lv_color_hex(CLR_PRIMARY),LV_STATE_FOCUSED);
    lv_obj_set_style_radius(a,8,0);lv_obj_set_style_pad_all(a,14,0);
    lv_obj_set_style_text_color(a,lv_color_hex(0xB8C5D8),LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_font(a,UI_FONT_20,LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_opa(a,LV_OPA_TRANSP,LV_PART_CURSOR);
    lv_obj_set_style_border_width(a,2,LV_PART_CURSOR);lv_obj_set_style_border_side(a,LV_BORDER_SIDE_LEFT,LV_PART_CURSOR);
    lv_obj_set_style_border_color(a,lv_color_hex(CLR_TEXT),LV_PART_CURSOR);
    lv_obj_set_style_border_opa(a,LV_OPA_TRANSP,LV_PART_CURSOR);
    lv_obj_set_style_border_opa(a,LV_OPA_COVER,LV_PART_CURSOR|LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(a,400,LV_PART_CURSOR);dark_scrollbar(a);
    lv_textarea_set_max_length(a,numeric?7:240);
    if(numeric){lv_textarea_set_accepted_chars(a,"0123456789");lv_textarea_set_placeholder_text(a,"0");}
    lv_obj_add_event_cb(a,input_focus,LV_EVENT_FOCUSED,nullptr);
    lv_obj_add_event_cb(a,input_focus,LV_EVENT_CLICKED,nullptr);return a;
}
static bool number(lv_obj_t *a,int *out) {
    const char *s=lv_textarea_get_text(a);if(!s||!*s)return false;
    char *end=nullptr;long n=strtol(s,&end,10);
    if(!end||*end||n<0||n>1000000)return false;
    *out=(int)n;return true;
}
static void fill_number(lv_obj_t *a,int value,bool valid) {
    char text[16]="";if(valid)snprintf(text,sizeof(text),"%d",value);
    lv_textarea_set_text(a,text);
}
static void difference() {
    int a,b;
    if(view.hasOpening&&authoritative&&number(numbers[1][0],&a)&&number(numbers[1][1],&b)) {
        int x=view.opening12-sold[0]-a,y=view.opening20-sold[1]-b;
        char text[144];snprintf(text,sizeof(text),"Differenz Kal. 12: %+d / Kal. 20: %+d",x,y);
        lv_label_set_text(differenceLabel,text);
        lv_obj_set_style_text_color(differenceLabel,lv_color_hex(x||y?CLR_WARN:CLR_SUCCESS),0);
    } else {
        lv_label_set_text(differenceLabel,"Differenz: Anfang, Portalstand und Zählung erforderlich.");
        lv_obj_set_style_text_color(differenceLabel,lv_color_hex(CLR_WARN),0);
    }
}
static const PortalSpieler *maitre(int id) {
    for(int i=0;i<g_store.portalSpielerCount;i++) {
        const PortalSpieler &p=g_store.portalSpieler[i];
        if(p.id==id&&p.aktiv&&p.isMaitreDeTir)return &p;
    }
    return nullptr;
}
static bool chosen(int id) {for(int i=0;i<selectedCount;i++)if(selected[i]==id)return true;return false;}
static void render_chips();
static void add_maitre(int id) {
    if(!maitre(id))return;
    if(chosen(id)){feedback(0,"Dieser Maître ist bereits ausgewählt.");return;}
    if(selectedCount>=4){feedback(0,"Höchstens vier Maîtres auswählen.");return;}
    selected[selectedCount++]=id;selectionDirty=dirty[0]=true;render_chips();
    skipConfirmed=false;
    loading=true;lv_textarea_set_text(search,"");loading=false;hide_keyboard();
    feedback(0,"Auswahl geändert. Bitte Tagesbeginn speichern.");
}
static void render_chips() {
    lv_obj_clean(chips);
    for(int i=0;i<selectedCount;i++) {
        const PortalSpieler *p=maitre(selected[i]);char name[MAX_NAME_LEN];
        utf8_to_display(name,p?p->name:"Mitglied nicht mehr aktiv",sizeof(name));
        char text[MAX_NAME_LEN+12];snprintf(text,sizeof(text),"%s  " LV_SYMBOL_CLOSE,name);
        lv_obj_t *chip=button(chips,text,[](lv_event_t *e){
            int id=(int)(intptr_t)lv_event_get_user_data(e);
            for(int j=0;j<selectedCount;j++)if(selected[j]==id){memmove(selected+j,selected+j+1,(selectedCount-j-1)*sizeof(int));selectedCount--;break;}
            selectionDirty=dirty[0]=true;render_chips();show_results();
            skipConfirmed=false;
        },(void*)(intptr_t)selected[i]);
        lv_obj_set_width(chip,LV_PCT(48));
    }
    if(!selectedCount) {
        bool available=false;
        for(int i=0;i<g_store.portalSpielerCount;i++)if(g_store.portalSpieler[i].aktiv&&g_store.portalSpieler[i].isMaitreDeTir)available=true;
        label(chips,available?"Noch kein Maître ausgewählt.":"Keine Maîtres. Im Portal markieren und synchronisieren.");
    }
}
static int first_match() {
    const char *query=lv_textarea_get_text(search);
    for(int i=0;i<g_store.portalSpielerCount;i++) {
        const PortalSpieler &p=g_store.portalSpieler[i];
        if(p.aktiv&&p.isMaitreDeTir&&!chosen(p.id)&&display_name_matches(p.name,query))return p.id;
    }
    return 0;
}
static void show_results() {
    lv_obj_clean(results);int eligible=0,matches=0;
    const char *query=lv_textarea_get_text(search);
    for(int i=0;i<g_store.portalSpielerCount;i++) {
        const PortalSpieler &p=g_store.portalSpieler[i];if(!p.aktiv||!p.isMaitreDeTir)continue;
        eligible++;
        if(chosen(p.id)||!display_name_matches(p.name,query))continue;
        matches++;char name[MAX_NAME_LEN];utf8_to_display(name,p.name,sizeof(name));
        button(results,name,[](lv_event_t *e){add_maitre((int)(intptr_t)lv_event_get_user_data(e));},(void*)(intptr_t)p.id);
    }
    if(!eligible)label(results,"Keine Maîtres. Im Portal markieren und synchronisieren.");
    else if(selectedCount>=4)label(results,"Vier Maîtres ausgewählt. Zum Ändern zuerst einen entfernen.");
    else if(!matches)label(results,"Kein Treffer für diesen Namen.");
    lv_obj_clear_flag(results,LV_OBJ_FLAG_HIDDEN);
}
static void carry_changed(lv_event_t *) {
    if(loading)return;
    skipConfirmed=false;
    bool on=lv_obj_has_state(carry,LV_STATE_CHECKED);
    if(on&&(!view.previousValid||view.fromOpening)) {
        lv_obj_remove_state(carry,LV_STATE_CHECKED);feedback(0,"Kein bestätigter Schlussbestand. Bitte neu zählen.");return;
    }
    loading=true;
    for(int i=0;i<2;i++) {
        if(on){fill_number(numbers[0][i],i?view.previous20:view.previous12,true);lv_obj_add_state(numbers[0][i],LV_STATE_DISABLED);}
        else lv_obj_remove_state(numbers[0][i],LV_STATE_DISABLED);
    }
    loading=false;dirty[0]=true;feedback(0,"Eingabe geändert. Bitte Tagesbeginn speichern.");
}
static void accept_expected(lv_event_t *) {
    skipConfirmed=false;
    if(!authoritative||!view.hasOpening||view.opening12<sold[0]||view.opening20<sold[1]) {
        feedback(1,"Bestätigter Anfang/Portalstand fehlt oder ist negativ. Bitte zählen.");return;
    }
    fill_number(numbers[1][0],view.opening12-sold[0],true);
    fill_number(numbers[1][1],view.opening20-sold[1],true);dirty[1]=true;difference();hide_keyboard();
}
static void save(lv_event_t *e) {
    skipConfirmed=false;
    int col=(int)(intptr_t)lv_event_get_user_data(e),a,b;
    DayInventory current;day_inventory_snapshot(&current);
    if(strcmp(current.datum,openedDate)){
        feedback(col,"Der Tag hat gewechselt. Seite neu öffnen und das Inventar für heute erfassen.");return;
    }
    if(!number(numbers[col][0],&a)||!number(numbers[col][1],&b)){
        feedback(col,"Bitte beide Bestände eingeben (0 bis 1.000.000).");return;
    }
    bool taken=col==0&&lv_obj_has_state(carry,LV_STATE_CHECKED);
    if(taken&&(!current.previousValid||current.fromOpening||a!=current.previous12||b!=current.previous20)){
        feedback(0,"Letzter Schlussbestand geändert. Übernahme erneut prüfen oder neu zählen.");return;
    }
    if(col==0) {
        if(selectedCount<1){feedback(0,"Bitte mindestens einen Maître de Tir auswählen.");return;}
        for(int i=0;i<selectedCount;i++)if(!maitre(selected[i])) {
            feedback(0,"Auswahl nicht mehr aktiv. Bitte Maîtres erneut prüfen.");return;
        }
        // Preserve the existing durable immutable outbox, including partial
        // failure semantics: never claim a saved opening if its enqueue fails.
        if(!day_inventory_queue(DAY_MAITRES,0,0,selected,selectedCount,false,"",openedDate)){
            feedback(0,"Nicht gespeichert: Warteschlange voll oder Speicherfehler. Bitte synchronisieren.");return;
        }
    }
    if(!day_inventory_queue(col?DAY_CLOSING:DAY_OPENING,a,b,nullptr,0,taken,col?lv_textarea_get_text(note):"",openedDate)){
        feedback(col,"Bestand nicht gespeichert. Bitte synchronisieren und erneut versuchen.");return;
    }
    dirty[col]=false;if(!col)selectionDirty=false;
    feedback(col,"Gespeichert. Wird beim nächsten Sync gesendet.",true);hide_keyboard();
    if(col&&beforeShutdown){beforeShutdown=false;screen_dashboard_continue_shutdown();}
    else (void)store_sync();
}
static void skip(lv_event_t *) {
    if(!beforeShutdown)return;
    if(!skipConfirmed){skipConfirmed=true;feedback(1,"Ohne Inventar beenden? Nochmals tippen. Der Sync vor dem Ausschalten bleibt erforderlich.");return;}
    skipConfirmed=false;screen_dashboard_shutdown_without_inventory();
}
void screen_inventory_open(bool end,bool shutdown) {
    beforeShutdown=shutdown;focusClosing=end;reload=true;skipConfirmed=false;
    ui_manager_show(SCREEN_INVENTORY);(void)store_sync();
}
lv_obj_t *screen_inventory_create() {
    screen=lv_obj_create(nullptr);screen_base_init(screen);lv_obj_set_style_text_font(screen,UI_FONT_18,0);
    lv_obj_set_flex_flow(screen,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(screen,16,0);lv_obj_set_style_pad_row(screen,12,0);
    lv_obj_t *header=container(screen);lv_obj_set_size(header,LV_PCT(100),60);lv_obj_set_flex_flow(header,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);lv_obj_set_style_pad_column(header,20,0);
    lv_obj_t *back=button(header,"Zurück",[](lv_event_t*){hide_keyboard();ui_manager_show(SCREEN_DASHBOARD);});
    lv_obj_set_width(back,140);lv_obj_t *title=label(header,"Inventar & Tag",UI_FONT_28);lv_obj_set_width(title,LV_SIZE_CONTENT);
    dateLabel=label(header,"",UI_FONT_20);lv_obj_set_flex_grow(dateLabel,1);lv_obj_set_style_text_align(dateLabel,LV_TEXT_ALIGN_RIGHT,0);
    body=container(screen);lv_obj_set_height(body,0);lv_obj_set_flex_grow(body,1);lv_obj_set_flex_flow(body,LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body,16,0);lv_obj_clear_flag(body,LV_OBJ_FLAG_SCROLLABLE);
    for(int col=0;col<2;col++) {
        cards[col]=container(body,true);lv_obj_set_width(cards[col],0);lv_obj_set_flex_grow(cards[col],1);lv_obj_set_height(cards[col],LV_PCT(100));
        lv_obj_set_style_pad_all(cards[col],18,0);lv_obj_set_style_pad_row(cards[col],10,0);
        label(cards[col],col?"Tagesschluss":"Tagesbeginn",UI_FONT_24);status[col]=label(cards[col],"Noch nicht erfasst");
    }
    label(cards[0],"Maître de Tir (1–4)",UI_FONT_20);
    lv_obj_t *row=container(cards[0]);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(row,10,0);
    search=input(row,false);lv_obj_set_width(search,0);lv_obj_set_flex_grow(search,1);lv_textarea_set_max_length(search,63);
    lv_textarea_set_placeholder_text(search,"Name suchen …");
    lv_obj_add_event_cb(search,[](lv_event_t*){if(!loading)show_results();},LV_EVENT_VALUE_CHANGED,nullptr);
    lv_obj_t *add=button(row,"Hinzufügen",[](lv_event_t*){
        int id=first_match();if(id)add_maitre(id);
        else {show_results();feedback(0,selectedCount>=4?"Höchstens vier Maîtres auswählen.":"Kein passender Maître verfügbar.");}
    });lv_obj_set_width(add,165);
    results=container(cards[0],true);lv_obj_set_height(results,178);lv_obj_set_style_pad_all(results,6,0);lv_obj_add_flag(results,LV_OBJ_FLAG_HIDDEN);
    chips=container(cards[0]);lv_obj_set_flex_flow(chips,LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(chips,8,0);lv_obj_set_style_pad_column(chips,10,0);
    previousLabel=label(cards[0],"");
    carry=lv_checkbox_create(cards[0]);lv_obj_remove_style_all(carry);lv_checkbox_set_text(carry,"Übernehmen");
    lv_obj_set_size(carry,LV_PCT(100),56);lv_obj_set_style_text_font(carry,UI_FONT_20,0);lv_obj_set_style_pad_all(carry,10,0);
    lv_obj_set_style_bg_color(carry,lv_color_hex(CLR_BORDER),LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(carry,LV_OPA_COVER,LV_PART_INDICATOR);lv_obj_set_style_pad_all(carry,6,LV_PART_INDICATOR);
    lv_obj_set_style_border_width(carry,1,LV_PART_INDICATOR);lv_obj_set_style_border_color(carry,lv_color_hex(0x718096),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(carry,lv_color_hex(CLR_PRIMARY),LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_bg_image_src(carry,LV_SYMBOL_OK,LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_bg_image_recolor(carry,lv_color_hex(CLR_TEXT),LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_add_event_cb(carry,carry_changed,LV_EVENT_VALUE_CHANGED,nullptr);
    lv_obj_t *summaryCard=container(cards[1],true);
    lv_obj_set_style_pad_all(summaryCard,10,0);
    lv_obj_set_style_bg_color(summaryCard,lv_color_hex(CLR_BG),0);
    summary=label(summaryCard,"");
    for(int col=0;col<2;col++) {
        lv_obj_t *r=container(cards[col]);lv_obj_set_flex_flow(r,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(r,12,0);
        for(int cal=0;cal<2;cal++) {
            lv_obj_t *field=container(r);lv_obj_set_width(field,0);lv_obj_set_flex_grow(field,1);
            label(field,col?(cal?"Gezählt Kal. 20":"Gezählt Kal. 12"):(cal?"Kaliber 20 (Einheiten)":"Kaliber 12 (Einheiten)"),UI_FONT_20);
            numbers[col][cal]=input(field,true);
            lv_obj_add_event_cb(numbers[col][cal],[](lv_event_t *e){
                if(!loading){skipConfirmed=false;int col=(int)(intptr_t)lv_event_get_user_data(e);dirty[col]=true;feedback(col,"Eingabe geändert. Bitte speichern.");difference();}
            },LV_EVENT_VALUE_CHANGED,(void*)(intptr_t)col);
        }
        if(col) {
            button(cards[1],"Erwarteten Bestand übernehmen",accept_expected);
            differenceLabel=label(cards[1],"");label(cards[1],"Notiz (optional)",UI_FONT_20);note=input(cards[1],false);
            lv_textarea_set_placeholder_text(note,"Hinweis zum Schlussbestand");
            lv_obj_add_event_cb(note,[](lv_event_t*){if(!loading){skipConfirmed=false;dirty[1]=true;feedback(1,"Notiz geändert. Bitte speichern.");}},LV_EVENT_VALUE_CHANGED,nullptr);
        } else label(cards[0],"Gastvereine: Bestand neu zählen, nicht übernehmen.");
        button(cards[col],"Speichern",save,(void*)(intptr_t)col,true);
        messages[col]=label(cards[col],"");
    }
    skipButton=button(cards[1],"Ohne Inventar beenden",skip);lv_obj_add_flag(skipButton,LV_OBJ_FLAG_HIDDEN);
    keyboard=lv_keyboard_create(screen);lv_obj_set_size(keyboard,LV_PCT(100),280);
    lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_NUMBER,numberMap,numberControls);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(CLR_BG),0);lv_obj_set_style_bg_opa(keyboard,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(keyboard,0,0);lv_obj_set_style_pad_all(keyboard,8,0);
    lv_obj_set_style_pad_row(keyboard,8,0);lv_obj_set_style_pad_column(keyboard,8,0);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(CLR_BORDER),LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard,lv_color_hex(CLR_TEXT),LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard,UI_FONT_20,LV_PART_ITEMS);lv_obj_set_style_border_width(keyboard,0,LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(CLR_PRIMARY),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard,[](lv_event_t*){hide_keyboard();},LV_EVENT_READY,nullptr);
    lv_obj_add_event_cb(keyboard,[](lv_event_t*){hide_keyboard();},LV_EVENT_CANCEL,nullptr);
    return screen;
}
static uint32_t roster_hash() {
    uint32_t hash=2166136261u;
    for(int i=0;i<g_store.portalSpielerCount;i++) {
        const PortalSpieler &p=g_store.portalSpieler[i];
        hash=(hash^(uint32_t)p.id)*16777619u;hash=(hash^p.aktiv)*16777619u;hash=(hash^p.isMaitreDeTir)*16777619u;
        for(const unsigned char *s=(const unsigned char*)p.name;*s;s++)hash=(hash^*s)*16777619u;
    }
    return hash;
}
static void source_status(int col) {
    const char *source=col?view.closingTerminal:view.openingTerminal,*at=col?view.closingAt:view.openingAt;
    bool known=col?view.hasClosing:view.hasOpening;
    char text[192];
    // UTC civil days -> epoch -> configured local timezone, without timegm
    // (unavailable in target newlib) or changing process-global TZ.
    struct tm utc{};char clock[12]="";
    if(*at&&sscanf(at,"%4d-%2d-%2dT%2d:%2d:%2d",&utc.tm_year,&utc.tm_mon,&utc.tm_mday,&utc.tm_hour,&utc.tm_min,&utc.tm_sec)==6 &&
       utc.tm_mon>=1&&utc.tm_mon<=12&&utc.tm_mday>=1&&utc.tm_mday<=31) {
        int year=utc.tm_year-(utc.tm_mon<=2),era=year/400;
        unsigned yoe=(unsigned)(year-era*400),month=(unsigned)(utc.tm_mon+(utc.tm_mon>2?-3:9));
        unsigned doy=(153*month+2)/5+(unsigned)utc.tm_mday-1;
        int64_t days=(int64_t)era*146097+(yoe*365+yoe/4-yoe/100+doy)-719468;
        time_t stamp=(time_t)(days*86400+utc.tm_hour*3600+utc.tm_min*60+utc.tm_sec);
        struct tm local{};localtime_r(&stamp,&local);strftime(clock,sizeof(clock),"%H:%M",&local);
    }
    if(!known)snprintf(text,sizeof(text),"Noch nicht erfasst");
    else if(*source&&strncmp(source,"lokal",5))snprintf(text,sizeof(text),"Erfasst von Terminal %.64s%s%.11s",source,*clock?", um ":"",clock);
    else snprintf(text,sizeof(text),"Lokal erfasst; wird beim nächsten Sync gesendet");
    lv_label_set_text(status[col],text);
}
void screen_inventory_refresh() {
    DayInventory current;day_inventory_snapshot(&current);
    bool reset=reload;
    if(!reset&&strcmp(current.datum,openedDate)) {
        feedback(0,"Tag gewechselt. Eingabe bleibt erhalten; Seite vor dem Speichern neu öffnen.");
        feedback(1,"Tag gewechselt. Eingabe bleibt erhalten; Seite vor dem Speichern neu öffnen.");return;
    }
    uint32_t hash=roster_hash();
    const BillDaySummary &baseline=g_store.billDayBaseline;
    bool valid=baseline.authoritative&&!strcmp(baseline.datum,current.datum);
    if(!reset&&!memcmp(&current,&view,sizeof(view))&&hash==rosterRevision&&
       sold[0]==baseline.ammoSold[0]&&sold[1]==baseline.ammoSold[1]&&
       openBills==baseline.openTotalCent&&authoritative==valid)return;
    int scroll[2]={lv_obj_get_scroll_y(cards[0]),lv_obj_get_scroll_y(cards[1])};
    bool rosterChanged=hash!=rosterRevision;
    reload=false;view=current;rosterRevision=hash;authoritative=valid;
    sold[0]=baseline.ammoSold[0];sold[1]=baseline.ammoSold[1];openBills=baseline.openTotalCent;
    loading=true;
    if(reset) {
        memcpy(openedDate,view.datum,sizeof(openedDate));dirty[0]=dirty[1]=selectionDirty=false;
        lv_textarea_set_text(search,"");feedback(0,"");feedback(1,"");hide_keyboard();
        for(int c=0;c<2;c++){
            lv_obj_set_style_border_color(cards[c],lv_color_hex(c&&focusClosing?CLR_PRIMARY:CLR_BORDER),0);
            if(c&&focusClosing)lv_obj_add_state(cards[c],LV_STATE_FOCUSED);else lv_obj_remove_state(cards[c],LV_STATE_FOCUSED);
        }
        if(beforeShutdown)lv_obj_clear_flag(skipButton,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(skipButton,LV_OBJ_FLAG_HIDDEN);
    }
    char text[320];snprintf(text,sizeof(text),"%.2s.%.2s.%.4s",view.datum+8,view.datum+5,view.datum);
    lv_label_set_text(dateLabel,*view.datum?text:"Datum nicht verfügbar");
    source_status(0);source_status(1);
    if(!selectionDirty) {
        int count=view.maitreCount>4?4:view.maitreCount;
        bool changed=count!=selectedCount||memcmp(selected,view.maitreIds,count*sizeof(int));
        selectedCount=count;memcpy(selected,view.maitreIds,selectedCount*sizeof(int));
        if(reset||changed||rosterChanged)render_chips();
    } else if(rosterChanged)render_chips();
    if(view.previousValid&&!view.fromOpening) {
        snprintf(text,sizeof(text),"Bestand letzter Schluss: Kal. 12: %d / Kal. 20: %d, vom %.2s.%.2s.",
                 view.previous12,view.previous20,view.previousDate+8,view.previousDate+5);
        lv_obj_remove_state(carry,LV_STATE_DISABLED);
    } else {
        snprintf(text,sizeof(text),"Kein bestätigter Schlussbestand. Bestände neu zählen.");
        lv_obj_add_state(carry,LV_STATE_DISABLED);
        if(dirty[0]&&lv_obj_has_state(carry,LV_STATE_CHECKED)){
            lv_obj_remove_state(carry,LV_STATE_CHECKED);
            for(int i=0;i<2;i++)lv_obj_remove_state(numbers[0][i],LV_STATE_DISABLED);
            feedback(0,"Schlussbestand nicht mehr bestätigt. Eingaben prüfen und neu zählen.");
        }
    }
    lv_label_set_text(previousLabel,text);
    lv_obj_set_style_text_color(previousLabel,lv_color_hex(view.previousValid&&!view.fromOpening?CLR_TEXT:CLR_WARN),0);
    if(!dirty[0]) {
        fill_number(numbers[0][0],view.opening12,view.hasOpening);fill_number(numbers[0][1],view.opening20,view.hasOpening);
        bool taken=view.takenOver&&view.previousValid&&!view.fromOpening;
        if(taken)lv_obj_add_state(carry,LV_STATE_CHECKED);else lv_obj_remove_state(carry,LV_STATE_CHECKED);
        for(int i=0;i<2;i++){if(taken)lv_obj_add_state(numbers[0][i],LV_STATE_DISABLED);else lv_obj_remove_state(numbers[0][i],LV_STATE_DISABLED);}
    }
    if(!dirty[1]) {
        fill_number(numbers[1][0],view.closing12,view.hasClosing);fill_number(numbers[1][1],view.closing20,view.hasClosing);
        char safe[241];utf8_to_display(safe,view.closingNote,sizeof(safe));lv_textarea_set_text(note,safe);
    }
    if(view.hasOpening&&valid)snprintf(text,sizeof(text),
        "Anfang Kal. 12 / 20: %d / %d\nIm Portal verkauft: %d / %d\nErwartet: %d / %d%s",
        view.opening12,view.opening20,sold[0],sold[1],view.opening12-sold[0],view.opening20-sold[1],
        openBills>0?"\nOffene Rechnungen: Schluss darf erfasst werden.":"");
    else snprintf(text,sizeof(text),"Anfang oder Portal-Verkäufe fehlen.\nBitte synchronisieren oder tatsächlich zählen.");
    lv_label_set_text(summary,text);difference();loading=false;
    lv_obj_update_layout(screen);
    for(int c=0;c<2;c++)lv_obj_scroll_to_y(cards[c],reset?0:scroll[c],LV_ANIM_OFF);
    if(reset&&focusClosing)lv_obj_scroll_to_view(cards[1],LV_ANIM_OFF);
}
void screen_inventory_tick() {screen_inventory_refresh();}
