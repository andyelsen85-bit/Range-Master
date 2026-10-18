#!/usr/bin/env python3
"""Compile the REAL cancellation, credit snapshots, reconciliation and NVS codec."""
from pathlib import Path
import importlib.util,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",Path(__file__).with_name("test_machine_batch.py"))
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
SOURCE=(ROOT/"main/store/game_store.cpp").read_text()
def extract(signature):
    return fixture.function(SOURCE[SOURCE.rfind(signature):],signature)
STUBS=r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <ctime>
#include <sys/time.h>
#include "credit_outbox.h"
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
GameStore g_store{},durable{};
static char s_credit_cancelled_game_id[40]{};
static std::vector<unsigned char> disk;
static bool storage_ok=true;
static void kredit_events_lock(){}
static void kredit_events_unlock(){}
static bool save_kredit_state_unlocked(){
 if(!storage_ok)return false;
 CreditOutboxHeader h{};h.magic=CREDIT_OUTBOX_MAGIC;h.version=1;h.count=g_store.pendingKreditEventCount;
 memcpy(h.cancelledGameId,s_credit_cancelled_game_id,40);disk.resize(credit_outbox_size(h.count));
 memcpy(disk.data(),&h,sizeof(h));memcpy(disk.data()+sizeof(h),g_store.pendingKreditEvents,h.count*sizeof(KreditEvent));
 durable=g_store;return true;
}
void game_store_save(){assert(save_kredit_state_unlocked());}
void portal_live_changed(int){}
static bool credit_day_is_current(){return true;}
static bool reconcile_lineup_with_credits_unlocked(){return false;}
static bool bill_is_authoritatively_paid(int,const char*){return false;}
static bool has_pending_day_activity(int,const char*){return true;}
bool store_game_credit_price_valid(){return true;}
const Produkt *store_produkt(const char*){static Produkt p{};p.id=1;p.preisRevisionId=1;p.preisCent=500;return &p;}
static uint32_t esp_random(){static uint32_t n=100;return ++n;}
'''
FUNCTIONS=[
 "static int find_kredit_slot(", "static int find_pending_kredit_event(",
 "static void remove_pending_kredit_event(","static void clear_active_game_credit_tracking_unlocked(","static void clear_active_game_credit_tracking(",
 "static bool queue_kredit_event_unlocked(","bool store_cancel_spiel(",
 "int store_begin_kredit_event_sync(","void store_finish_kredit_event_sync_commit(",
 "static bool credit_id_in(","void store_capture_kredit_reconcile(",
 "void store_apply_portal_kredit(","bool store_credit_game_cancelled(",
]
MAIN=r'''
static void setup(int players=1) {
 g_store={};storage_ok=true;s_credit_cancelled_game_id[0]=0;
 time_t now=time(nullptr);tm local{};localtime_r(&now,&local);
 strftime(g_store.kreditDatum,11,"%Y-%m-%d",&local);
 g_store.screen=SCREEN_SPIEL;g_store.spielerCount=players;g_store.activeGameCreditCount=players;
 strcpy(g_store.spielId,"11111111-2222-3333-4444-555555555555");
 for(int i=0;i<players;i++) {
  g_store.kreditPlayerIds[i]=i+1;g_store.kredite[i].gewaehrt=10;g_store.kredite[i].verbraucht=1;
  assert(queue_kredit_event_unlocked(i+1,"USE",1));g_store.activeGameCreditPlayerIds[i]=i+1;
  strcpy(g_store.activeGameCreditUseIds[i],g_store.pendingKreditEvents[i].externalId);
 }
}
static void zero(int players=1) {
 for(int i=0;i<players;i++){assert(g_store.kredite[i].gewaehrt==10);assert(g_store.kredite[i].verbraucht==0);}
 for(int i=0;i<g_store.pendingKreditEventCount;i++)assert(strcmp(g_store.pendingKreditEvents[i].typ,"GRANT"));
}
static KreditReconcileProof proof(const KreditEvent&use,bool counted) {
 KreditReconcileProof p{};p.knownCount=1;strcpy(p.knownEventIds[0],use.externalId);
 if(counted){p.countedCount=1;strcpy(p.countedUseIds[0],use.externalId);}return p;
}
int main(){
 setenv("TZ","Europe/Luxembourg",1);tzset();KreditEvent snap[MAX_KREDIT_EVENTS]{};
 // a: never sent => remove USE, no refund event and no GRANT inflation.
 setup();assert(store_cancel_spiel());zero();assert(!g_store.pendingKreditEventCount);
 // b: successful sync => retain active receipt, then replace by VOID_USE.
 setup();assert(store_begin_kredit_event_sync(snap,50)==1);
 store_finish_kredit_event_sync_commit(snap,1,true);
 assert(g_store.pendingKreditEvents[0].acknowledged);
 auto p=proof(snap[0],true);store_apply_portal_kredit(1,10,1,&p);
 assert(store_cancel_spiel());zero();assert(g_store.pendingKreditEventCount==1);
 assert(!strcmp(g_store.pendingKreditEvents[0].typ,"VOID_USE"));
 assert(!strcmp(g_store.pendingKreditEvents[0].voidsExternalId,snap[0].externalId));
 store_apply_portal_kredit(1,10,1,&p);zero(); // baseline knows USE, not VOID
 p.voidedCount=1;strcpy(p.voidedUseIds[0],snap[0].externalId);
 store_apply_portal_kredit(1,10,0,&p);zero(); // VOID accepted, ACK lost
 // c: cancel an in-flight snapshot, before and after server accepts USE.
 setup();store_begin_kredit_event_sync(snap,50);assert(store_cancel_spiel());
 assert(g_store.pendingKreditEventCount==2);KreditReconcileProof unknown{};
 store_apply_portal_kredit(1,10,0,&unknown);zero();
 p=proof(snap[0],true);store_apply_portal_kredit(1,10,1,&p);zero();
 store_finish_kredit_event_sync_commit(snap,1,true);
 assert(g_store.pendingKreditEventCount==1);zero();
 // d: synced player, in-flight player, never-sent player, one cancellation.
 setup(3);assert(store_begin_kredit_event_sync(snap,2)==2);
 store_finish_kredit_event_sync_commit(snap,1,true);
 assert(store_cancel_spiel());zero(3);assert(g_store.pendingKreditEventCount==3);
 // e: decode committed outbox after loss of all RAM, ignore stale count key.
 auto saved=durable;g_store={};char cancelled[40]{};
 assert(credit_outbox_decode(disk.data(),disk.size(),49,g_store.pendingKreditEvents,&g_store.pendingKreditEventCount,cancelled));
 assert(g_store.pendingKreditEventCount==3);
 assert(!strcmp(cancelled,"11111111-2222-3333-4444-555555555555"));
 assert(store_credit_game_cancelled(cancelled));g_store=saved;zero(3);
 // f: failed HTTP ACK followed by reboot/offline cancellation still emits VOID.
 setup();store_begin_kredit_event_sync(snap,50);store_finish_kredit_event_sync_commit(snap,1,false);
 assert(!g_store.pendingKreditEvents[0].inFlight&&g_store.pendingKreditEvents[0].mayBeAccepted);
 assert(save_kredit_state_unlocked());g_store={};
 assert(credit_outbox_decode(disk.data(),disk.size(),0,g_store.pendingKreditEvents,&g_store.pendingKreditEventCount));
 assert(g_store.pendingKreditEvents[0].mayBeAccepted);g_store=durable;
 assert(store_cancel_spiel());assert(g_store.pendingKreditEventCount==2);zero();
 // Capacity refusal is atomic: not one of three players is partially refunded.
 setup(3);store_begin_kredit_event_sync(snap,3);
 while(g_store.pendingKreditEventCount<MAX_KREDIT_EVENTS)assert(queue_kredit_event_unlocked(99,"GRANT",1));
 auto count=g_store.pendingKreditEventCount;assert(!store_cancel_spiel());
 assert(g_store.pendingKreditEventCount==count);
 for(int i=0;i<3;i++)assert(g_store.kredite[i].verbraucht==1&&g_store.activeGameCreditUseIds[i][0]);
 // Legacy receipt and local-negative identities survive the binary upgrade.
 CreditOutboxReceipt old{};strcpy(old.id,"kre-legacy-100");old.player=-7;strcpy(old.day,"2026-10-18");
 strcpy(old.type,"USE");old.quantity=1;old.revision=12;old.cents=500;
 int n=0;assert(credit_outbox_decode(&old,sizeof(old),1,g_store.pendingKreditEvents,&n));
 assert(n==1&&g_store.pendingKreditEvents[0].spielerId==-7&&g_store.pendingKreditEvents[0].mayBeAccepted);
 assert(g_store.pendingKreditEvents[0].preisRevisionId==12);
 unsigned char invalid[sizeof(CreditOutboxHeader)]{};
 assert(!credit_outbox_decode(invalid,sizeof(invalid),0,g_store.pendingKreditEvents,&n));
 puts("PASS: six cancellation flows, exact 0 net credits/no grants, mixed states, capacity, ACK loss, binary restart and legacy migration.");
}
'''
with tempfile.TemporaryDirectory(prefix="credit-cancel-") as tmp:
    source=Path(tmp)/"test.cpp"
    source.write_text(STUBS+"\n".join(extract(s) for s in FUNCTIONS)+MAIN)
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-unused-function",
                    "-I",str(ROOT/"main/store"),str(source),"-o",str(Path(tmp)/"test")],check=True)
    subprocess.run([str(Path(tmp)/"test")],check=True)
