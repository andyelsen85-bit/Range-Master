#include "screen_inventory.h"
#include "ui_manager.h"
#include "ui_fonts.h"
#include "game_store.h"
#include "day_inventory.h"
#include "screen_dashboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static lv_obj_t *screen,*body,*keyboard,*message,*c12,*c20,*note,*carry,*sourceLabel,*differenceLabel;
static lv_obj_t *maitreBoxes[MAX_PORTAL_SPIELER];
static int maitreIds[MAX_PORTAL_SPIELER],maitreCount;
static bool closing,rebuild=true,skipConfirmed,editing,beforeShutdown;
static int renderedSold12=-1,renderedSold20=-1;
static DayInventory view;
static lv_obj_t *label(lv_obj_t *p,const char *text) {
    lv_obj_t *l=lv_label_create(p);lv_label_set_text(l,text);lv_obj_set_width(l,LV_PCT(100));
    lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);lv_obj_set_style_text_color(l,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_style_text_font(l,UI_FONT_16,0);return l;
}
static lv_obj_t *button(lv_obj_t *p,const char *text,lv_event_cb_t cb) {
    lv_obj_t *b=lv_button_create(p);lv_obj_add_style(b,&g_style_btn_secondary,0);
    lv_obj_set_size(b,LV_SIZE_CONTENT,46);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,NULL);
    lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);return b;
}
static void input_focus(lv_event_t *e) {
    editing=true;
    lv_keyboard_set_textarea(keyboard,(lv_obj_t*)lv_event_get_target(e));
    lv_keyboard_set_mode(keyboard,lv_event_get_target(e)==note?LV_KEYBOARD_MODE_TEXT_UPPER:LV_KEYBOARD_MODE_NUMBER);
    lv_obj_clear_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *input(lv_obj_t *p,const char *title,int value,bool valid) {
    label(p,title);lv_obj_t *a=lv_textarea_create(p);lv_obj_set_size(a,LV_PCT(100),46);lv_textarea_set_one_line(a,true);
    lv_textarea_set_accepted_chars(a,"0123456789");lv_textarea_set_max_length(a,7);
    if(valid){char text[16];snprintf(text,sizeof(text),"%d",value);lv_textarea_set_text(a,text);}else lv_textarea_set_text(a,"");
    lv_obj_add_event_cb(a,input_focus,LV_EVENT_FOCUSED,NULL);return a;
}
static bool number(lv_obj_t *a,int *out) {
    const char *s=lv_textarea_get_text(a);if(!s||!*s)return false;
    char *end=NULL;long n=strtol(s,&end,10);if(!end||*end||n<0||n>1000000)return false;
    *out=(int)n;return true;
}
static void difference() {
    if(!closing||!differenceLabel)return;
    int a,b;const BillDaySummary &s=g_store.billDayBaseline;
    if(view.hasOpening&&s.authoritative&&!strcmp(s.datum,view.datum)&&number(c12,&a)&&number(c20,&b)) {
        char text[160];snprintf(text,sizeof(text),"Voraussichtliche Differenz Kal. 12: %d / Kal. 20: %d (informativ)",view.opening12-s.ammoSold[0]-a,view.opening20-s.ammoSold[1]-b);
        lv_label_set_text(differenceLabel,text);
    } else lv_label_set_text(differenceLabel,"Differenz: nicht erfasst — bitte Bestände zählen.");
}
static void save(lv_event_t *) {
    DayInventory current;day_inventory_snapshot(&current);
    if(strcmp(current.datum,view.datum)){lv_label_set_text(message,"Der Tag hat gewechselt. Bitte die Seite neu öffnen und das Inventar für heute erfassen.");return;}
    int a,b,ids[4],n=0;
    bool taken=!closing&&carry&&lv_obj_has_state(carry,LV_STATE_CHECKED);
    if(taken){a=view.previous12;b=view.previous20;}
    else if(!number(c12,&a)||!number(c20,&b)){lv_label_set_text(message,"Bitte beide Bestände eingeben (0 bis 1.000.000).");return;}
    if(!closing) {
        for(int i=0;i<maitreCount;i++)if(lv_obj_has_state(maitreBoxes[i],LV_STATE_CHECKED)){
            if(n>=4){lv_label_set_text(message,"Höchstens vier Maîtres auswählen.");return;}ids[n++]=maitreIds[i];
        }
        if(n<1){lv_label_set_text(message,"Bitte mindestens einen Maître de Tir auswählen.");return;}
        if(!day_inventory_queue(DAY_MAITRES,0,0,ids,n,false,"")){lv_label_set_text(message,"Nicht gespeichert: Warteschlange voll oder Speicherfehler. Bitte synchronisieren.");return;}
    }
    if(!day_inventory_queue(closing?DAY_CLOSING:DAY_OPENING,a,b,NULL,0,taken,closing?lv_textarea_get_text(note):"")){
        lv_label_set_text(message,"Bestand nicht gespeichert. Bitte synchronisieren und erneut versuchen.");return;
    }
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(message,"Gespeichert. Wird beim nächsten Sync ans Portal gesendet.");
    editing=false;
    if(closing&&beforeShutdown){beforeShutdown=false;screen_dashboard_continue_shutdown();}
    else (void)store_sync(); // Existing persistent worker; never perform HTTP here.
}
static void carry_changed(lv_event_t *) {
    editing=true;
    bool on=lv_obj_has_state(carry,LV_STATE_CHECKED);
    if(on){char text[16];snprintf(text,sizeof(text),"%d",view.previous12);lv_textarea_set_text(c12,text);snprintf(text,sizeof(text),"%d",view.previous20);lv_textarea_set_text(c20,text);}
    if(on){lv_obj_add_state(c12,LV_STATE_DISABLED);lv_obj_add_state(c20,LV_STATE_DISABLED);}
    else {lv_obj_remove_state(c12,LV_STATE_DISABLED);lv_obj_remove_state(c20,LV_STATE_DISABLED);}
}
static void accept_expected(lv_event_t *) {
    int sold12=0,sold20=0;
    const BillDaySummary &s=g_store.billDayBaseline;
    if(s.authoritative&&!strcmp(s.datum,view.datum)){sold12=s.ammoSold[0];sold20=s.ammoSold[1];}
    if(!view.hasOpening||view.opening12<sold12||view.opening20<sold20){lv_label_set_text(message,"Erwarteter Bestand fehlt oder ist negativ. Bitte zählen.");return;}
    char text[16];snprintf(text,sizeof(text),"%d",view.opening12-sold12);lv_textarea_set_text(c12,text);
    snprintf(text,sizeof(text),"%d",view.opening20-sold20);lv_textarea_set_text(c20,text);
}
static void skip(lv_event_t *) {
    if(!skipConfirmed){skipConfirmed=true;lv_label_set_text(message,"Ohne Inventar beenden? Zur Bestätigung nochmals tippen. Der Sync vor dem Ausschalten bleibt erforderlich.");return;}
    skipConfirmed=false;screen_dashboard_shutdown_without_inventory();
}
void screen_inventory_open(bool end,bool shutdown) {closing=end;beforeShutdown=shutdown;rebuild=true;skipConfirmed=false;editing=false;ui_manager_show(SCREEN_INVENTORY);(void)store_sync();}
lv_obj_t *screen_inventory_create() {
    screen=lv_obj_create(NULL);lv_obj_set_style_bg_color(screen,lv_color_hex(CLR_BG),0);
    lv_obj_set_flex_flow(screen,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(screen,18,0);
    lv_obj_t *header=lv_obj_create(screen);lv_obj_set_size(header,LV_PCT(100),58);lv_obj_set_flex_flow(header,LV_FLEX_FLOW_ROW);
    button(header,"Zurück",[](lv_event_t*){ui_manager_show(SCREEN_DASHBOARD);});
    button(header,"Tagesbeginn",[](lv_event_t*){screen_inventory_open(false);});
    button(header,"Tagesschluss",[](lv_event_t*){screen_inventory_open(true);});
    body=lv_obj_create(screen);lv_obj_set_width(body,LV_PCT(100));lv_obj_set_flex_grow(body,1);lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(body,&g_style_card,0);
    keyboard=lv_keyboard_create(screen);lv_obj_set_size(keyboard,LV_PCT(100),220);lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard,[](lv_event_t*){lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);},LV_EVENT_READY,NULL);
    lv_obj_add_event_cb(keyboard,[](lv_event_t*){lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);},LV_EVENT_CANCEL,NULL);
    return screen;
}
void screen_inventory_refresh() {
    if(!rebuild)return;
    rebuild=false;day_inventory_snapshot(&view);lv_obj_clean(body);carry=NULL;differenceLabel=NULL;
    renderedSold12=g_store.billDayBaseline.ammoSold[0];renderedSold20=g_store.billDayBaseline.ammoSold[1];
    char text[512];snprintf(text,sizeof(text),"INVENTAR & TAG — %s — %s",view.datum,closing?"Tagesschluss":"Tagesbeginn");label(body,text);
    const char *source=closing?view.closingTerminal:view.openingTerminal;const char *at=closing?view.closingAt:view.openingAt;
    sourceLabel=label(body,"");
    if(*source){snprintf(text,sizeof(text),"Bereits erfasst von %.64s, %.31s. Behalten oder unten bewusst ändern.",source,at);lv_label_set_text(sourceLabel,text);}
    if(!closing) {
        label(body,"Maîtres de Tir (1 bis 4 aktive Mitglieder)");maitreCount=0;
        lv_obj_t *list=lv_obj_create(body);lv_obj_set_size(list,LV_PCT(100),120);lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);
        for(int i=0;i<g_store.portalSpielerCount&&maitreCount<MAX_PORTAL_SPIELER;i++){
            const PortalSpieler &p=g_store.portalSpieler[i];if(!p.aktiv||!p.isMaitreDeTir)continue;
            lv_obj_t *box=lv_checkbox_create(list);lv_checkbox_set_text(box,p.name);lv_obj_set_style_text_color(box,lv_color_hex(0xFFFFFF),0);
            lv_obj_add_event_cb(box,[](lv_event_t*){editing=true;},LV_EVENT_VALUE_CHANGED,NULL);
            for(int m=0;m<view.maitreCount;m++)if(view.maitreIds[m]==p.id)lv_obj_add_state(box,LV_STATE_CHECKED);
            maitreIds[maitreCount]=p.id;maitreBoxes[maitreCount++]=box;
        }
        if(!maitreCount)label(body,"Keine Maîtres verfügbar. Im Portal markieren und synchronisieren.");
        if(view.previousValid){snprintf(text,sizeof(text),"Letzter bekannter Inventartag: %s. Kal. 12: %d, Kal. 20: %d.",view.previousDate,view.previous12,view.previous20);label(body,text);}
        if(!view.previousValid||view.fromOpening)label(body,view.fromOpening?"Letzter Schlussbestand fehlt. Bitte neu zählen; Übernahme ist nicht möglich.":"Noch kein bestätigter Schlussbestand vorhanden. Bitte beide Bestände zählen.");
        label(body,"Auch Gastvereine müssen das Inventar beim Tagesbeginn korrekt erfassen.");
    } else {
        const BillDaySummary &s=g_store.billDayBaseline;bool valid=s.authoritative&&!strcmp(s.datum,view.datum);
        if(view.hasOpening&&valid){snprintf(text,sizeof(text),"Anfang Kal.12/20: %d / %d — im Portal verkauft: %d / %d — erwartet: %d / %d",view.opening12,view.opening20,s.ammoSold[0],s.ammoSold[1],view.opening12-s.ammoSold[0],view.opening20-s.ammoSold[1]);label(body,text);}
        else label(body,"Anfang oder Portal-Verkäufe noch nicht vollständig verfügbar. Bitte synchronisieren oder tatsächlich zählen.");
        if(valid&&s.openTotalCent>0)label(body,"Es sind noch Rechnungen offen. Der Schlussbestand darf trotzdem erfasst werden.");
        label(body,"Differenzen sind Hinweise zur Kontrolle, kein Fehler. Das Portal berechnet sie aus den Verkäufen neu.");
    }
    c12=input(body,"Kaliber 12 (Verkaufseinheiten)",closing?view.closing12:view.opening12,closing?view.hasClosing:view.hasOpening);
    c20=input(body,"Kaliber 20 (Verkaufseinheiten)",closing?view.closing20:view.opening20,closing?view.hasClosing:view.hasOpening);
    lv_obj_add_event_cb(c12,[](lv_event_t*){editing=true;difference();},LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(c20,[](lv_event_t*){editing=true;difference();},LV_EVENT_VALUE_CHANGED,NULL);
    if(!closing&&view.previousValid&&!view.fromOpening) {
        carry=lv_checkbox_create(body);lv_checkbox_set_text(carry,"Bestand vom letzten Schluss übernehmen");
        lv_obj_set_style_text_color(carry,lv_color_hex(0xFFFFFF),0);lv_obj_add_event_cb(carry,carry_changed,LV_EVENT_VALUE_CHANGED,NULL);
    }
    if(closing) {
        differenceLabel=label(body,"");difference();
        button(body,"Erwarteten Bestand übernehmen",accept_expected);label(body,"Notiz (optional)");
        note=lv_textarea_create(body);lv_obj_set_size(note,LV_PCT(100),46);lv_textarea_set_one_line(note,true);lv_textarea_set_max_length(note,240);
        lv_textarea_set_text(note,view.closingNote);
        lv_obj_add_event_cb(note,input_focus,LV_EVENT_FOCUSED,NULL);
    }
    message=label(body,"");button(body,"Speichern",save);
    if(closing)button(body,"Ohne Inventar beenden",skip);
}
void screen_inventory_tick() {
    DayInventory current;day_inventory_snapshot(&current);
    if(editing){
        const char *source=closing?current.closingTerminal:current.openingTerminal,*at=closing?current.closingAt:current.openingAt;
        if(sourceLabel&&*source){char text[224];snprintf(text,sizeof(text),"Aktueller Portalstand: %.64s, %.31s. Eingabe bleibt unverändert; bitte vor dem Speichern prüfen.",source,at);lv_label_set_text(sourceLabel,text);}
        difference();
        return;
    }
    if(memcmp(&current,&view,sizeof(view))||renderedSold12!=g_store.billDayBaseline.ammoSold[0]||renderedSold20!=g_store.billDayBaseline.ammoSold[1]){rebuild=true;screen_inventory_refresh();}
}
