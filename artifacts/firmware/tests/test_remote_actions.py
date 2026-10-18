#!/usr/bin/env python3
"""Compile and execute the actual shared touch/remote actions and UI dispatcher."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",ROOT.parent/"lora-gateway/tests/test_nonce.py")
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)

STUBS=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
enum SpielActionResult { SPIEL_ACTION_OK,SPIEL_ACTION_IGNORED,SPIEL_ACTION_SCORE_FIRST,SPIEL_ACTION_RADIO_BUSY };
enum {SCREEN_SPIEL,SCREEN_DASHBOARD,MASCHINE_H=7};
struct SequenzEintrag { bool isPair,isDoublette;int maschine,partner,delayMs; };
struct {int taubeIndex=0,sequenzLen=2,ergebnisseCount=0,spielerCount=1,screen=SCREEN_SPIEL;
    int spielerIndex=0;
    bool activeGameStorageError=false,harakiriOrdersReady=false;
    bool currentFireSent=false,remoteButtonsEnabled=true;
    SequenzEintrag sequenz[2]{};char gatewayUrl[256]="http://gateway",gatewayToken[256]="synthetic-auth-key";
} g_store;
template<class Store> static const SequenzEintrag *store_sequence_entry(const Store *s,int,int c){return &s->sequenz[c];}
void game_store_save(){}
static bool busy=false,queueOk=true;static unsigned launches=0,scores=0,repeats=0,skips=0,refreshes=0,clicks=0;
bool lora_request_busy(){return busy;}
bool lora_fire_machine_game(int){if(queueOk) ++launches;return queueOk;}
bool lora_fire_doublette_game(int,int,int){if(queueOk) ++launches;return queueOk;}
void screen_spiel_refresh(){++refreshes;}
void store_eintragen(int){++scores;++g_store.ergebnisseCount;g_store.currentFireSent=false;}
void store_wiederholen(){++repeats;g_store.currentFireSent=false;}
void store_skip_taube(){++skips;g_store.currentFireSent=false;}
struct ButtonEventsConfig {bool enabled,gameActive;char url[256],key[256];};
struct RemoteButtonEvent {uint8_t src,btn;uint32_t receivedMs,generation;};
std::deque<RemoteButtonEvent> events;
void button_events_publish(const ButtonEventsConfig&){}
bool button_events_take(RemoteButtonEvent *out){if(events.empty())return false;*out=events.front();events.pop_front();return true;}
static unsigned now=1000;uint32_t lv_tick_get(){return now;}
int64_t esp_timer_get_time(){return now*1000;}
static int s_current=SCREEN_SPIEL;
static void *s_remote_toast=nullptr;static uint32_t s_remote_toast_until=0,s_remote_last_action=0;
static bool s_remote_has_action=false;
constexpr int LV_OBJ_FLAG_HIDDEN=1;
void lv_obj_add_flag(void*,int){}
static const char *toast="";
void remote_toast(const char *text){toast=text;}
void click_sound_play_if_enabled(){++clicks;}
'''
MAIN=r'''
void push(uint8_t sender,uint8_t btn){events.push_back({sender,btn,now,0});remote_buttons_tick();}
void wait(){now+=501;}
int main(){
    // Touch first, then two independent handhelds must still obey the same flag.
    assert(spiel_action_fire()==SPIEL_ACTION_OK&&launches==1&&g_store.currentFireSent);
    push(1,1);assert(launches==1&&!strcmp(toast,"Erst Punkte eintragen"));
    wait();push(2,1);assert(launches==1);
    push(1,4);assert(scores==1&&!g_store.currentFireSent);
    wait();push(2,1);assert(launches==2);
    wait();push(1,1);assert(launches==2);
    push(2,3);assert(skips==1&&!g_store.currentFireSent);
    wait();push(1,1);assert(launches==3);
    wait();push(2,2);assert(repeats==1&&!g_store.currentFireSent);
    wait();push(2,1);assert(launches==4);
    // Remote-wide lockout, including different buttons and senders.
    push(1,4);assert(scores==1);now+=499;push(2,3);assert(skips==1);
    now+=1;push(2,4);assert(scores==2);
    wait();g_store.sequenz[0].isPair=true;
    push(1,5);assert(scores==2);push(2,6);assert(scores==3);
    wait();g_store.sequenz[0].isDoublette=true;
    push(1,1);assert(launches==4&&!strcmp(toast,"Erst Punkte eintragen"));
    g_store.sequenz[0]={};busy=true;push(1,1);assert(launches==4&&!strcmp(toast,"Funk belegt"));
    busy=false;queueOk=false;assert(spiel_action_fire()!=SPIEL_ACTION_OK&&!g_store.currentFireSent);
    queueOk=true;g_store.ergebnisseCount=0;assert(spiel_action_repeat()==SPIEL_ACTION_IGNORED);
    s_current=SCREEN_DASHBOARD;wait();push(2,1);assert(launches==4&&!strcmp(toast,"Taster ignoriert"));
    s_current=SCREEN_SPIEL;g_store.screen=SCREEN_DASHBOARD;push(2,6);assert(scores==3);
    g_store.screen=SCREEN_SPIEL;g_store.spielerCount=0;push(1,1);assert(launches==4);
    g_store.spielerCount=1;events.push_back({1,1,now-3001,0});remote_buttons_tick();assert(launches==4);
    assert(clicks==8);
    puts("PASS actual shared/UI actions: touch + two remotes, fire guard, queue rejection, score/skip/repeat");
    puts("PASS pair P1, second half, busy, non-game, staleness, 500ms lockout and accepted-only sound");
}
'''
if __name__=="__main__":
    spiel=(ROOT/"main/ui/screen_spiel.cpp").read_text()
    ui=(ROOT/"main/ui/ui_manager.cpp").read_text()
    functions="\n".join(fixture.function(spiel,sig) for sig in [
        "SpielActionResult spiel_action_score","SpielActionResult spiel_action_fire",
        "SpielActionResult spiel_action_repeat","SpielActionResult spiel_action_skip"])
    functions+="\n"+fixture.function(ui,"static void remote_buttons_tick")
    with tempfile.TemporaryDirectory(prefix="tm-actions-") as directory:
        cpp=Path(directory)/"test.cpp";cpp.write_text(STUBS+functions+MAIN)
        exe=Path(directory)/"test"
        subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",str(cpp),"-o",str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
