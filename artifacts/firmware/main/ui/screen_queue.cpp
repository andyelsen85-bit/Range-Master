#include "screen_queue.h"
#include "portal_runtime.h"
#include "game_store.h"
#include "ui_fonts.h"
#include "screen_start.h"
#include "coprocessor.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *buttons[2],*labels[2],*modal,*message;
static PortalQueueSnapshot shown;
static int choice=-1,lastCount=-1;
static bool lastReserved;
static bool lastEnabled;
static bool lastConnected;
static bool modalTaking;
static unsigned appliedSeen;
static char lastMessage[128];
static void deleted(lv_event_t *e) {
 auto *obj=lv_event_get_target_obj(e);
 if(obj==modal){modal=nullptr;message=nullptr;}
 for(int i=0;i<2;i++)if(buttons[i]==obj){buttons[i]=nullptr;labels[i]=nullptr;}
}
static void close(lv_event_t*){if(modal)lv_obj_delete(modal);}
static void refresh(lv_event_t*){portal_queue_refresh();if(message)lv_label_set_text(message,"Wird geladen. Bitte Warteschlange erneut öffnen.");}
static void release(lv_event_t*) {
 bool ok=portal_queue_release();
 if(message)lv_label_set_text(message,ok?"Reservierung wird freigegeben.":"Freigabe konnte nicht gespeichert werden.");
}
static void apply(lv_event_t *e) {
 if(choice<0||choice>=shown.count)return;
 bool merge=(intptr_t)lv_event_get_user_data(e)==1;
 bool ok=portal_queue_take(&shown.games[choice],merge);
 modalTaking=ok;
 if(message)lv_label_set_text(message,ok?"Warteschlange wird übernommen …":"Nicht verfügbar. WLAN prüfen oder Reservierung freigeben.");
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,lv_event_cb_t cb,void *data=nullptr) {
 auto *b=lv_btn_create(parent);lv_obj_set_size(b,260,56);
 auto *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_set_style_text_font(l,UI_FONT_16,0);lv_obj_center(l);
 lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data);return b;
}
static void select(lv_event_t *e) {
 choice=(int)(intptr_t)lv_event_get_user_data(e);
 if(message)lv_label_set_text(message,"Aufstellung ersetzen oder ergänzen? Maximal 6 Spieler.");
}
static void open(lv_event_t*) {
 if(modal)return;
 shown={};(void)portal_queue_snapshot(&shown);PortalQueueGame own={};bool reserved=portal_queue_current(&own);
 if(reserved) {
  bool included=false;for(int i=0;i<shown.count;i++)if(shown.games[i].id==own.id)included=true;
  if(!included) {int n=shown.count<6?shown.count++:5;for(int i=n;i>0;i--)shown.games[i]=shown.games[i-1];shown.games[0]=own;shown.games[0].status=1;}
 }
 choice=-1;
 for(int i=0;i<shown.count;i++)if(!shown.games[i].status||(reserved&&shown.games[i].id==own.id)){choice=i;break;}
 modalTaking=false;portal_queue_status(lastMessage,sizeof(lastMessage));
 portal_queue_refresh();
 modal=lv_obj_create(lv_layer_top());lv_obj_set_size(modal,900,650);lv_obj_center(modal);
 lv_obj_set_style_bg_color(modal,lv_color_hex(0x172133),0);lv_obj_set_style_text_color(modal,lv_color_hex(0xffffff),0);
 lv_obj_set_layout(modal,LV_LAYOUT_FLEX);lv_obj_set_flex_flow(modal,LV_FLEX_FLOW_COLUMN);
 lv_obj_add_event_cb(modal,deleted,LV_EVENT_DELETE,nullptr);
 auto *title=lv_label_create(modal);lv_label_set_text(title,"Warteschlange – freiwillige Übernahme");
 message=lv_label_create(modal);lv_obj_set_width(message,840);lv_label_set_text(message,"Spiel auswählen. Unbekannte Spieler zuerst synchronisieren.");
 for(int i=0;i<shown.count;i++) {
  const auto &g=shown.games[i];char text[420];int offset=snprintf(text,sizeof(text),"%d. %s: ",i+1,modus_label((Modus)g.mode));
  for(int p=0;p<g.count&&offset<(int)sizeof(text)-1;p++)
   offset+=snprintf(text+offset,sizeof(text)-offset,"%s%.63s",p?", ":"",g.players[p].name);
  auto *b=button(modal,text,select,(void*)(intptr_t)i);lv_obj_set_width(b,840);
  if(g.status&&(!reserved||own.id!=g.id))lv_obj_add_state(b,LV_STATE_DISABLED);
  lv_obj_set_width(lv_obj_get_child(b,0),810);lv_label_set_long_mode(lv_obj_get_child(b,0),LV_LABEL_LONG_DOT);
 }
 auto *actions=lv_obj_create(modal);lv_obj_set_size(actions,850,88);lv_obj_set_layout(actions,LV_LAYOUT_FLEX);
 button(actions,"Ersetzen",apply);button(actions,"Ergänzen",apply,(void*)1);button(actions,"Schließen",close);
 auto *bottom=lv_obj_create(modal);lv_obj_set_size(bottom,850,88);lv_obj_set_layout(bottom,LV_LAYOUT_FLEX);
 button(bottom,"Aktualisieren",refresh);if(portal_queue_reserved())button(bottom,"Reservierung freigeben",release);
}
void screen_queue_button(lv_obj_t *screen) {
 int index=buttons[0]?1:0;
 if(buttons[index])return;
 buttons[index]=button(screen,"Warteschlange",open);labels[index]=lv_obj_get_child(buttons[index],0);
 lv_obj_add_flag(buttons[index],LV_OBJ_FLAG_FLOATING);lv_obj_align(buttons[index],LV_ALIGN_TOP_RIGHT,-100,12);
 lv_obj_add_event_cb(buttons[index],deleted,LV_EVENT_DELETE,nullptr);
 lastCount=-1;
}
void screen_queue_tick() {
 PortalQueueSnapshot current={};bool read=portal_queue_snapshot(&current),reserved=portal_queue_reserved(),connected=cop_wifi_is_connected();
 if(read&&(current.total!=lastCount||reserved!=lastReserved||current.enabled!=lastEnabled||connected!=lastConnected)) {
  lastConnected=connected;
  lastEnabled=current.enabled;
  lastCount=current.total;lastReserved=reserved;
  char text[64];snprintf(text,sizeof(text),"Warteschlange (%d)",current.total);
  for(int i=0;i<2;i++)if(buttons[i]) {
   lv_label_set_text(labels[i],text);
   if(current.enabled||reserved)lv_obj_remove_flag(buttons[i],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(buttons[i],LV_OBJ_FLAG_HIDDEN);
   if(connected)lv_obj_remove_state(buttons[i],LV_STATE_DISABLED);else lv_obj_add_state(buttons[i],LV_STATE_DISABLED);
  }
 }
 unsigned generation=portal_queue_generation();
 if(generation!=appliedSeen){appliedSeen=generation;if(g_store.screen==SCREEN_START)screen_start_refresh();}
 if(message) {
  char text[128];portal_queue_status(text,sizeof(text));
  if(text[0]&&strcmp(text,lastMessage)) {
   snprintf(lastMessage,sizeof(lastMessage),"%s",text);lv_label_set_text(message,text);
   if(modalTaking&&!strcmp(text,"Warteschlange übernommen")&&g_store.screen==SCREEN_START) {
    if(modal)lv_obj_delete(modal);
   }
  }
 }
}
