#!/usr/bin/env python3
"""Actual sequence/flow/undo/scorecard and two-slot persistence, no hardware fire."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",Path(__file__).with_name("test_machine_batch.py"))
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)

STUBS=r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>
#include <array>
#include <sys/stat.h>
#include <unistd.h>
#include "game_store.h"
#include "active_game.h"
#include "history_scorecard.h"
#include "active_scorecard.h"
#include "scorecard_layout.h"
GameStore g_store{};
static bool credit_cancelled=false;
bool store_credit_game_cancelled(const char*){return credit_cancelled;}
static std::set<std::string> ended_games;
bool store_game_was_ended(const char *id){return ended_games.count(id)>0;}
void store_record_game_end(const char *id){if(id[0])ended_games.insert(id);}
static bool persist=false;
static bool remapped=false;
static int journal_status=0;
static int finished=0;
static std::mt19937 rng(173128);
static uint32_t esp_random(){return rng();}
void game_store_save(){if(persist)assert(active_game_checkpoint());}
void game_store_save_game_checkpoint(){game_store_save();}
void portal_live_changed(int){}
bool portal_queue_game_started(){return true;}
bool portal_queue_release(){return true;}
bool portal_queue_busy(){return false;}
static void _store_finish_game(){finished++;}
enum SpielActionResult {SPIEL_ACTION_OK,SPIEL_ACTION_IGNORED,SPIEL_ACTION_SCORE_FIRST,SPIEL_ACTION_RADIO_BUSY};
static int fired_machine=-1,fired_partner=-1;
static bool lora_request_busy(){return false;}
static bool lora_fire_machine_game(Maschine m){fired_machine=m;fired_partner=-1;return true;}
static bool lora_fire_doublette_game(Maschine m,Maschine partner,uint16_t){fired_machine=m;fired_partner=partner;return true;}
static void screen_spiel_refresh(){}
void game_journal_storage_lock(){}
void game_journal_storage_unlock(){}
int game_journal_contains(const char*){return journal_status;}
int game_journal_resolve_player_locked(int id){return remapped&&id==1?101:id;}
void terminal_log_add(const char*,const char*,const char*,const void*){}
bool ui_manager_snapshot_lock(bool *acquired){*acquired=false;return true;}
void ui_manager_unlock(){}
'''
MAIN=r'''
static void setup(int mode,int mask,int players) {
    memset(&g_store,0,sizeof(g_store));finished=0;
    g_store.modus=(Modus)mode;g_store.lauf=1;g_store.screen=SCREEN_SPIEL;
    g_store.spielerCount=players;g_store.customLaeufe[4]=2;
    for(int i=0;i<players;i++) {
        g_store.spieler[i].id=i+1;g_store.spieler[i].startPosten=i%5+1;
        snprintf(g_store.spieler[i].name,MAX_NAME_LEN,"Player %d",i+1);
    }
    for(int m=0;m<8;m++)g_store.maschinenAktiv[m]=(mask&(1<<m))!=0;
    CustomSequenzEintrag pair{MASCHINE_A,MASCHINE_F,true,1234};
    g_store.sequenzLen=generate_sequenz(g_store.sequenz,(Modus)mode,g_store.maschinenAktiv,&pair,1);
    generate_player_orders(&g_store);
    snprintf(g_store.spielId,sizeof(g_store.spielId),"synthetic-game");
}
static void verify_permutation() {
    for(int p=0;p<g_store.spielerCount;p++) {
        bool seen[HARAKIRI_MAX_CLAYS]={};int units=0;
        for(int c=0;c<g_store.sequenzLen;c++) {
            auto *entry=store_sequence_entry(&g_store,p,c);
            int index=g_store.harakiriOrder[p][c];
            assert(!seen[index]);seen[index]=true;
            if(!entry->isDoublette)units++;
            if(entry->isPair&&!entry->isDoublette) {
                assert(c+1<g_store.sequenzLen);
                auto *second=store_sequence_entry(&g_store,p,c+1);
                assert(second->isPair&&second->isDoublette&&second->maschine==entry->partner);
            }
        }
        assert(units==store_sequence_unit(&g_store,p,g_store.sequenzLen));
    }
}
int main() {
    int games=0,different=0;
    for(int mode:{MODUS_NORMAL,MODUS_HARAKIRI}) {
        setup(mode,255,6);FinishedGame view{};
        int current=active_scorecard_project(&g_store,&view);
        auto shape=history_scorecard_shape(&view);
        assert(shape.runs==2&&shape.clays==9&&view.spieler_count==6&&current>=1&&current<=18);
        auto fit=scorecard_layout(950,260,6,18);
        assert(fit.name+fit.total+18*fit.clay<=950&&7*fit.row<=260&&fit.font>=12);
    }
    for(int mode:{MODUS_NORMAL,MODUS_HARAKIRI}) {
        setup(mode,127,3); // singles only: a half-scored H pair is a separate case
        g_store.currentFireSent=true;
        store_wiederholen();
        assert(!g_store.currentFireSent&&g_store.repeatCurrentLaunch);
        assert(g_store.spielerIndex==0&&g_store.taubeIndex==0&&g_store.ergebnisseCount==0);
        store_wiederholen();
        assert(g_store.spielerIndex==0&&g_store.ergebnisseCount==0);
        g_store.currentFireSent=true;store_eintragen(2);
        int player=g_store.spielerIndex,clay=g_store.taubeIndex,run=g_store.lauf;
        g_store.currentFireSent=true;store_wiederholen();
        assert(!g_store.currentFireSent&&g_store.ergebnisseCount==1&&g_store.spieler[0].punkte==2);
        assert(g_store.spielerIndex==player&&g_store.taubeIndex==clay&&g_store.lauf==run);
        store_wiederholen();
        assert(g_store.ergebnisseCount==0&&g_store.spielerIndex==0&&g_store.spieler[0].punkte==0);
        assert(g_store.ergebnisse[0].wiederholt);
    }
    setup(MODUS_CUSTOM_1,255,2);
    // First repeat also rearms an unscored pair before any launch flag exists.
    store_wiederholen();
    assert(g_store.repeatCurrentLaunch&&g_store.ergebnisseCount==0&&g_store.taubeIndex==0);
    store_wiederholen();
    assert(g_store.ergebnisseCount==0&&g_store.taubeIndex==0);
    g_store.currentFireSent=true;store_wiederholen();
    assert(!g_store.currentFireSent&&g_store.taubeIndex==0&&g_store.ergebnisseCount==0);
    g_store.currentFireSent=true;store_eintragen(2);
    store_wiederholen();assert(g_store.ergebnisseCount==1&&g_store.currentFireSent);
    store_eintragen(2);assert(g_store.ergebnisseCount==2&&g_store.spieler[0].punkte==4);
    store_wiederholen();
    assert(g_store.ergebnisseCount==2&&g_store.spielerIndex==1&&g_store.taubeIndex==0);
    store_wiederholen();
    assert(g_store.ergebnisseCount==0&&g_store.spielerIndex==0&&g_store.taubeIndex==0&&g_store.spieler[0].punkte==0);
    for(int players=2;players<=6;players++)for(int mode:{MODUS_HARAKIRI,MODUS_HARAKIRI_CUSTOM})
    for(int mask=1;mask<256;mask++) {
        if(mode==MODUS_HARAKIRI_CUSTOM&&((mask&33)!=33))continue;
        setup(mode,mask,players);verify_permutation();games++;
        if(memcmp(g_store.harakiriOrder[0],g_store.harakiriOrder[1],g_store.sequenzLen))different++;
        int count[6][2][9]={},expected[6][2][24]={};
        while(!finished) {
            int p=g_store.spielerIndex,clay=g_store.taubeIndex,run=g_store.lauf;
            auto entry=*store_sequence_entry(&g_store,p,clay);
            if(entry.isDoublette)assert(spiel_action_fire()==SPIEL_ACTION_SCORE_FIRST);
            else {
                assert(spiel_action_fire()==SPIEL_ACTION_OK);
                assert(fired_machine==entry.maschine);
                assert(fired_partner==(entry.isPair&&entry.maschine!=MASCHINE_H?(int)entry.partner:-1));
                assert(spiel_action_fire()==SPIEL_ACTION_SCORE_FIRST);
            }
            assert(++count[p][run-1][clay]==1);
            int score=entry.isPair ? (entry.isDoublette?0:2) : ((int)entry.maschine+p+run)%3;
            int key=(entry.isPair?(entry.isDoublette?16:8):0)+(int)entry.maschine;
            expected[p][run-1][key]=score;
            int oldCount=g_store.ergebnisseCount;
            if(entry.isPair){store_eintragen(1);assert(oldCount==g_store.ergebnisseCount);}
            store_eintragen(score);
            const auto &result=g_store.ergebnisse[oldCount];
            assert(result.spielerId==p+1&&result.taube==clay+1&&result.maschine==entry.maschine);
            if(entry.isPair&&!entry.isDoublette)
                assert(g_store.spielerIndex==p&&g_store.taubeIndex==clay+1);
        }
        assert(g_store.ergebnisseCount==2*players*g_store.sequenzLen);
        FinishedGame game{};game.base.modus=(Modus)mode;game.base.lauf=2;
        game.base.taubenProLauf=g_store.sequenzLen;
        game.base.ergebnisse_count=g_store.ergebnisseCount;
        memcpy(game.base.ergebnisse,g_store.ergebnisse,sizeof(g_store.ergebnisse));
        for(int p=0;p<players;p++)for(int run=1;run<=2;run++) {
            int total=0;
            for(int clay=1;clay<=g_store.sequenzLen;clay++) {
                auto *r=history_scorecard_result(&game,p+1,run,clay);assert(r);
                int key=history_scorecard_column_key(&game,run,clay);
                assert(r->punkte==expected[p][run-1][key]);total+=r->punkte;
                assert(history_scorecard_doublette(&game,run,clay)==(key>=8));
            }
            assert(total>=0);
        }
    }
    assert(different>games/2);
    for(int mode:{MODUS_NORMAL,MODUS_CUSTOM_1,MODUS_CUSTOM_2,MODUS_CUSTOM_3,MODUS_CUSTOM_4}) {
        setup(mode,255,6);assert(!g_store.harakiriOrdersReady);
        for(int p=0;p<6;p++)for(int c=0;c<g_store.sequenzLen;c++)
            assert(store_sequence_entry(&g_store,p,c)==&g_store.sequenz[c]);
    }
    setup(MODUS_HARAKIRI_CUSTOM,255,6);g_store.customLaeufe[4]=1;
    while(!finished)store_eintragen(2);
    assert(g_store.ergebnisseCount==6*g_store.sequenzLen);
    // Check rank distribution without demanding impossible uniqueness for one
    // unit or forcing different players' orders (which would bias randomness).
    int bins[8][8]={};
    for(int trial=0;trial<8000;trial++) {
        setup(MODUS_HARAKIRI,255,2);
        for(int unit=0;unit<8;unit++) {
            int clay=store_sequence_clay(&g_store,0,unit);
            bins[(int)store_sequence_entry(&g_store,0,clay)->maschine][unit]++;
        }
    }
    // Undo the final unit of a run restores all starting posts, then replaying
    // that unit rotates them exactly once again.
    setup(MODUS_HARAKIRI_CUSTOM,255,6);
    while(g_store.lauf==1)store_eintragen(2);
    int boundary=g_store.ergebnisseCount,posts[6];
    for(int i=0;i<6;i++)posts[i]=g_store.spieler[i].startPosten;
    store_wiederholen();assert(g_store.lauf==1&&g_store.spielerIndex==5);
    auto last=*store_sequence_entry(&g_store,5,g_store.taubeIndex);
    store_eintragen(2);if(last.isPair)store_eintragen(2);
    assert(g_store.lauf==2&&g_store.ergebnisseCount==boundary);
    for(int i=0;i<6;i++)assert(posts[i]==g_store.spieler[i].startPosten);
    for(auto &row:bins)for(int n:row)assert(n>800&&n<1200);
    // Undo follows the previous player's OWN raw offset; completed pairs undo
    // both results together. Repeat/skip never silently advance another unit.
    setup(MODUS_HARAKIRI_CUSTOM,255,6);
    for(int n=0;n<30;n++) {
        int player=g_store.spielerIndex,clay=g_store.taubeIndex;
        auto entry=*store_sequence_entry(&g_store,player,clay);
        int before=g_store.ergebnisseCount,points=g_store.spieler[player].punkte;
        g_store.currentFireSent=true;
        store_wiederholen();assert(!g_store.currentFireSent&&g_store.ergebnisseCount==before);
        store_eintragen(2);
        if(entry.isPair)store_eintragen(0);
        int nextPlayer=g_store.spielerIndex,nextClay=g_store.taubeIndex;
        int after=g_store.ergebnisseCount;
        const auto *next=store_sequence_entry(&g_store,nextPlayer,nextClay);
        bool rearmPair=next&&next->isPair&&!next->isDoublette&&!g_store.repeatCurrentLaunch;
        store_wiederholen();
        if(rearmPair) {
            // Stage one belongs to the newly current unscored pair. Only
            // stage two may undo the previous player's complete launch.
            assert(g_store.spielerIndex==nextPlayer&&g_store.taubeIndex==nextClay);
            assert(g_store.ergebnisseCount==after&&g_store.repeatCurrentLaunch);
            store_wiederholen();
        }
        assert(g_store.spielerIndex==player&&g_store.taubeIndex==clay);
        assert(g_store.ergebnisseCount==before&&g_store.spieler[player].punkte==points);
        store_skip_taube();if(entry.isPair)store_skip_taube();
    }
    // Persist a mixed first-run state, including permutations/results/credits,
    // then reconstruct RAM exactly as a reboot would.
    assert(active_game_restore());persist=true;
    setup(MODUS_HARAKIRI_CUSTOM,255,6);
    g_store.activeGameCreditCount=1;g_store.activeGameCreditPlayerIds[0]=1;
    snprintf(g_store.activeGameCreditUseIds[0],40,"original-use-event");
    for(int n=0;n<13;n++)store_eintragen(2);
    while(!store_sequence_entry(&g_store,g_store.spielerIndex,g_store.taubeIndex)->isPair)
        store_eintragen(2);
    if(!store_sequence_entry(&g_store,g_store.spielerIndex,g_store.taubeIndex)->isDoublette)
        store_eintragen(2);
    assert(store_sequence_entry(&g_store,g_store.spielerIndex,g_store.taubeIndex)->isDoublette);
    auto orders=g_store.harakiriOrder;
    uint8_t saved[6][9];memcpy(saved,orders,sizeof(saved));
    int count=g_store.ergebnisseCount,clay=g_store.taubeIndex,player=g_store.spielerIndex;
    auto machine=store_sequence_entry(&g_store,player,clay)->maschine;
    memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    assert(g_store.harakiriOrdersReady&&g_store.ergebnisseCount==count);
    assert(g_store.taubeIndex==clay&&g_store.spielerIndex==player);
    assert(!memcmp(saved,g_store.harakiriOrder,sizeof(saved)));
    assert(store_sequence_entry(&g_store,player,clay)->maschine==machine);
    assert(!g_store.currentFireSent&&!g_store.activeGameResumed);
    assert(!strcmp(g_store.activeGameCreditUseIds[0],"original-use-event"));
    // A failed replacement must retain the committed slot and permutations.
    auto newest=[]() {
        uint32_t gen[2]={};
        for(int slot=0;slot<2;slot++) {
            char name[160];snprintf(name,sizeof(name),ACTIVE_GAME_ROOT "/ACTIVE%d.BIN",slot);
            FILE *f=fopen(name,"rb");assert(f);uint32_t header[5];
            assert(fread(header,sizeof(header),1,f)==1);fclose(f);gen[slot]=header[4];
        }
        return gen[0]>gen[1]?0:1;
    };
    char name[160];int older=1-newest();
    snprintf(name,sizeof(name),ACTIVE_GAME_ROOT "/ACTIVE%d.BIN",older);
    assert(unlink(name)==0&&mkdir(name,0700)==0);
    // Identical state must perform no I/O, even when the alternate slot is
    // unwritable. Change a persisted field before injecting a write failure.
    assert(active_game_checkpoint());
    g_store.activeAcknowledgedClays++;
    assert(!active_game_checkpoint());assert(rmdir(name)==0);
    memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    assert(g_store.ergebnisseCount==count&&!memcmp(saved,g_store.harakiriOrder,sizeof(saved)));
    // Populate the repaired alternate slot with a genuinely new snapshot;
    // another identical request intentionally does not rotate generations.
    g_store.activeAcknowledgedClays++;
    assert(active_game_checkpoint());
    // Simulate an interrupted flash write; use the other validated checkpoint,
    // warn, and retain the same player permutations rather than regenerating.
    snprintf(name,sizeof(name),ACTIVE_GAME_ROOT "/ACTIVE%d.BIN",newest());
    FILE *partial=fopen(name,"wb");assert(partial);fputs("partial",partial);fclose(partial);
    memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    assert(!memcmp(saved,g_store.harakiriOrder,sizeof(saved))&&g_store.currentFireSent);
    assert(active_game_checkpoint());
    journal_status=-1;memset(&g_store,0,sizeof(g_store));assert(!active_game_restore());
    assert(g_store.activeGameStorageError); // uncertain archive is not absence
    journal_status=0;memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    remapped=true;assert(active_game_remap());
    // Concurrent/pre-publication snapshot still uses the old RAM IDs.
    assert(active_game_checkpoint());
    memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    assert(g_store.spieler[0].id==101&&g_store.activeGameCreditPlayerIds[0]==101);
    for(int i=0;i<g_store.ergebnisseCount;i++)assert(g_store.ergebnisse[i].spielerId!=1);
    // Durable cancellation wins even when power failed before the FAT
    // tombstone and the journal is currently unavailable.
    auto *before_cancel=new GameStore(g_store);
    credit_cancelled=true;journal_status=-1;memset(&g_store,0,sizeof(g_store));
    assert(active_game_restore());assert(!g_store.harakiriOrdersReady&&!g_store.activeGameResumed);
    credit_cancelled=false;journal_status=0;
    g_store=*before_cancel;delete before_cancel;assert(active_game_checkpoint());
    // Completion recognized by journal wins over an older active checkpoint.
    journal_status=1;memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    assert(!g_store.harakiriOrdersReady);
    journal_status=0;memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    assert(!g_store.harakiriOrdersReady); // tombstone persists
    // An NVS-fallback finished game is already in the boot RAM queue even when
    // no canonical FAT journal file exists. Never resurrect its active slot.
    setup(MODUS_HARAKIRI,255,2);persist=true;
    g_store.harakiriOrdersReady=true;assert(active_game_checkpoint());
    char pending_id[40];snprintf(pending_id,sizeof(pending_id),"%s",g_store.spielId);
    memset(&g_store,0,sizeof(g_store));
    g_store.pendingGamesCount=1;
    snprintf(g_store.pendingGames[0].externalId,40,"%s",pending_id);
    journal_status=0;assert(active_game_restore());assert(!g_store.harakiriOrdersReady);
    setup(MODUS_HARAKIRI,255,2);g_store.harakiriOrdersReady=true;
    assert(active_game_checkpoint());
    store_record_game_end(g_store.spielId);
    active_game_mark_ended();g_store.harakiriOrdersReady=false;g_store.screen=SCREEN_DASHBOARD;
    older=1-newest();snprintf(name,sizeof(name),ACTIVE_GAME_ROOT "/ACTIVE%d.BIN",older);
    assert(unlink(name)==0&&mkdir(name,0700)==0);
    assert(!active_game_checkpoint()&&!g_store.activeGameStorageError&&!active_game_is_open());
    assert(rmdir(name)==0);active_game_retry_finished_checkpoint();
    assert(!g_store.activeGameStorageError);
    ended_games.clear(); // subsequent independent trials reuse this fixture's UUID
    // Normal game: reboot between repeat stages preserves the explicit retry.
    for(int mode:{MODUS_NORMAL,MODUS_HARAKIRI}) {
    remapped=false;setup(mode,127,3);persist=true;int initial_player=g_store.spielerIndex;
    g_store.currentFireSent=true;assert(active_game_checkpoint());store_eintragen(2);
    int prior_player=g_store.spielerIndex,prior_clay=g_store.taubeIndex;
    g_store.currentFireSent=true;assert(active_game_checkpoint());store_wiederholen();
    memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    assert(g_store.harakiriOrdersReady==(mode==MODUS_HARAKIRI)&&!g_store.currentFireSent&&g_store.repeatCurrentLaunch);
    assert(g_store.spielerIndex==prior_player&&g_store.taubeIndex==prior_clay&&g_store.ergebnisseCount==1);
    store_wiederholen();assert(g_store.spielerIndex==initial_player&&g_store.ergebnisseCount==0&&g_store.spieler[initial_player].punkte==0);
    g_store.currentFireSent=true;assert(active_game_checkpoint());store_wiederholen();store_wiederholen();
    assert(g_store.repeatCurrentLaunch&&!g_store.currentFireSent&&g_store.ergebnisseCount==0&&g_store.spielerIndex==initial_player);
    }
    // Navigation must not silently end a Normal or Harakiri game.
    assert(g_store.activeGameOpen);g_store.screen=SCREEN_EINSTELLUNGEN;
    assert(active_game_checkpoint());
    memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    assert(g_store.activeGameOpen&&g_store.screen==SCREEN_SPIEL);
    // Failed ended tombstones clear only the RAM latch; durable end identity
    // still prevents resurrecting the old active slot after a power cycle.
    for(int slot=0;slot<2;slot++){
        char temp[160];snprintf(temp,sizeof(temp),ACTIVE_GAME_ROOT "/ACTIVE%d.TMP",slot);
        assert(mkdir(temp,0700)==0);
    }
    store_record_game_end(g_store.spielId);
    active_game_mark_ended();g_store.harakiriOrdersReady=false;g_store.screen=SCREEN_DASHBOARD;
    assert(!active_game_checkpoint()&&!g_store.activeGameStorageError&&!active_game_is_open());
    for(int slot=0;slot<2;slot++){
        char temp[160];snprintf(temp,sizeof(temp),ACTIVE_GAME_ROOT "/ACTIVE%d.TMP",slot);
        assert(rmdir(temp)==0);
    }
    memset(&g_store,0,sizeof(g_store));assert(active_game_restore());
    assert(!active_game_is_open()&&!g_store.activeGameStorageError);
    // Both unrecognizable files fail closed; never replace with shared order.
    for(int slot=0;slot<2;slot++){
        char name[160];snprintf(name,sizeof(name),ACTIVE_GAME_ROOT "/ACTIVE%d.BIN",slot);
        FILE *f=fopen(name,"wb");assert(f);fputs("corrupt",f);fclose(f);
    }
    memset(&g_store,0,sizeof(g_store));assert(!active_game_restore());
    assert(g_store.activeGameStorageError&&!active_game_checkpoint());
    assert(active_game_record_size()<=12288);
    printf("PASS %d games: all subsets, 2–6 players, independent whole-unit orders, flow/undo/skip, canonical scorecards\n",games);
    printf("PASS hardware-RNG shuffle distribution; active RAM map=%zu bytes, FAT record=%zu bytes x 2, unchanged FinishedGame ABI\n",
           sizeof(g_store.harakiriOrder),active_game_record_size());
    puts("PASS mid-game restart, failed/interrupted writes, credit identity, uncertain/final journal, durable tombstone, corrupt records fail closed");
}
'''
source=(ROOT/"main/store/game_store.cpp").read_text()
functions="\n".join(fixture.function(source,s) for s in [
    "static void shuffle","static bool append_single","static bool append_pair",
    "static int generate_sequenz","static void generate_player_orders","static int count_h2_before",
    "void store_eintragen","void store_wiederholen","void store_skip_taube"])
functions+=fixture.function((ROOT/"main/ui/screen_spiel.cpp").read_text(),"SpielActionResult spiel_action_fire()")
with tempfile.TemporaryDirectory(prefix="tm-harakiri-players-") as directory:
    path=Path(directory)
    (path/"ui_manager.h").write_text("#pragma once\nbool ui_manager_snapshot_lock(bool*);\nvoid ui_manager_unlock();\n")
    (path/"cJSON.h").write_text("#pragma once\nstruct cJSON {};\n")
    (path/"esp_timer.h").write_text("#pragma once\n#include <stdint.h>\ninline int64_t esp_timer_get_time(){return 100000000;}\n")
    # The log declaration's pointer type must match the real terminal header.
    STUBS=STUBS.replace('const void*){}','const cJSON*){}').replace('#include "active_game.h"','#include "active_game.h"\n#include "cJSON.h"')
    (path/"esp_heap_caps.h").write_text("#pragma once\n#include <cstdlib>\n#define MALLOC_CAP_SPIRAM 0\n#define MALLOC_CAP_8BIT 0\n#define heap_caps_malloc(n,c) malloc(n)\n#define heap_caps_calloc(n,s,c) calloc(n,s)\n#define heap_caps_free free\n")
    (path/"test.cpp").write_text(STUBS+functions+MAIN)
    subprocess.run(["g++","-std=c++17","-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer",
        "-Wall","-Wextra","-Werror",f'-DACTIVE_GAME_ROOT="{path}"',
        "-I",str(path),"-I",str(ROOT/"main/store"),"-I",str(ROOT/"main/ui"),
        str(path/"test.cpp"),str(ROOT/"main/store/active_game.cpp"),"-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)
