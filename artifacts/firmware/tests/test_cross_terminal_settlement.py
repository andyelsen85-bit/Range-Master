#!/usr/bin/env python3
"""Real store/HTTP reconciliation helpers, with host-only SDK/NVS/JSON mocks."""
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
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>
#include <map>
#include <vector>
#include <string>
#include "payment_receipts.h"
#define EXT_RAM_BSS_ATTR
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
const char *TAG = "test";
constexpr int MAX_PORTAL_SPIELER=200, MAX_SPIELER=6, MAX_DAY_BILLS=24;
constexpr int MAX_BILL_LINES=76, MAX_BILL_CATEGORIES=12, MAX_DAY_PRODUCTS=76;
constexpr int BILL_OPEN=0, BILL_PAID=1, MAX_PENDING_PAYMENTS=64;
constexpr int ESP_OK=0, ESP_ERR_NO_MEM=-1, ESP_ERR_TIMEOUT=-2, ESP_ERR_INVALID_RESPONSE=-3;
constexpr int OFFLINE_CACHE_CREDITS=0;
using esp_err_t=int; using TickType_t=int;
struct KreditStand { int gewaehrt, verbraucht; };
struct MunitionStand { int spielerId, cal12, cal20; };
struct KreditEvent { int spielerId; char datum[11], typ[8]; int anzahl; };
struct SaleEvent { int spielerId; char datum[11]; };
struct PaymentEvent { int spielerId; char datum[11], externalId[64], lastError[96]; bool inFlight; };
struct PlayerBill { int spielerId, state, lineCount, categoryCount; char paymentExternalId[64]; };
struct BillDaySummary {
    int playerCount, categoryCount, productCount;
    bool authoritative; char datum[11]; PlayerBill players[MAX_DAY_BILLS];
};
struct GameStore {
    int kreditPlayerIds[MAX_PORTAL_SPIELER], lineupIds[MAX_SPIELER];
    KreditStand kredite[MAX_PORTAL_SPIELER]; MunitionStand munition[MAX_PORTAL_SPIELER];
    BillDaySummary billDayBaseline, billDay;
    KreditEvent pendingKreditEvents[50]; int pendingKreditEventCount;
    SaleEvent pendingVerkaufEvents[64]; int pendingVerkaufEventCount;
    PaymentEvent pendingPaymentEvents[MAX_PENDING_PAYMENTS]; int pendingPaymentEventCount;
    char kreditDatum[11];
} g_store;
static PaymentReceipt s_payment_receipts[MAX_PORTAL_SPIELER];
static int s_payment_receipt_count;
static char s_payment_receipt_date[11];
void payment_state_lock() {} void payment_state_unlock() {}
void kredit_events_lock() {} void kredit_events_unlock() {}
void reconcile_lineup_with_credits_unlocked() {
    for (int &id : g_store.lineupIds) {
        bool funded=false;
        for(int k=0;k<MAX_PORTAL_SPIELER;++k)
            if(g_store.kreditPlayerIds[k]==id && g_store.kredite[k].gewaehrt>g_store.kredite[k].verbraucht) funded=true;
        if(!funded) id=0;
    }
}
int find_kredit_slot(const GameStore *s,int id) {
    for(int k=0;k<MAX_PORTAL_SPIELER;++k) if(s->kreditPlayerIds[k]==id)return k;
    return -1;
}
void store_rebuild_bill_projection() { g_store.billDay=g_store.billDayBaseline; }
static bool persistence_ok=true;
bool save_payment_state_unlocked() { return persistence_ok; }
static int saves=0, commits=0;
void game_store_save() { ++saves; }
void cache_pulled_section(int) {}
bool sync_commit_begin(const char *,int *started) { *started=0; ++commits; return true; }
void sync_commit_end(const char *,int) {}
struct cJSON {
    int type=0, valueint=0; double valuedouble=0;
    std::string text; char *valuestring=nullptr;
    std::map<std::string,cJSON*> fields; std::vector<cJSON*> elements;
};
cJSON *json_root=nullptr;
bool cJSON_IsArray(cJSON *v) { return v && v->type==1; }
bool cJSON_IsString(cJSON *v) { return v && v->type==2; }
bool cJSON_IsNumber(cJSON *v) { return v && v->type==3; }
int cJSON_GetArraySize(cJSON *v) { return (int)v->elements.size(); }
cJSON *cJSON_GetObjectItem(cJSON *v,const char *key) {
    auto i=v->fields.find(key); return i==v->fields.end()?nullptr:i->second;
}
cJSON *cJSON_GetObjectItemCaseSensitive(cJSON *v,const char *k) { return cJSON_GetObjectItem(v,k); }
cJSON *cJSON_Parse(const char *) { return json_root; }
void cJSON_Delete(cJSON *v) {
    for(auto &p:v->fields)cJSON_Delete(p.second);
    for(auto *p:v->elements)cJSON_Delete(p);
    delete v; json_root=nullptr;
}
#define cJSON_ArrayForEach(item,arr) for(size_t j=0;j<(arr)->elements.size() && ((item)=(arr)->elements[j],true);++j)
int http_get_json(const char *,char *buffer,size_t capacity) {
    assert(capacity>8192); strcpy(buffer,"{}"); return ESP_OK;
}
cJSON *string(const char *s) { auto *v=new cJSON;v->type=2;v->text=s;v->valuestring=v->text.data();return v; }
cJSON *number(int n) { auto *v=new cJSON;v->type=3;v->valueint=n;v->valuedouble=n;return v; }
void credit_response(const char *date,int granted=-999,int used=0) {
    json_root=new cJSON;json_root->fields["datum"]=string(date);
    auto *arr=new cJSON;arr->type=1;json_root->fields["kredite"]=arr;
    if(granted!=-999) {
        auto *row=new cJSON;
        row->fields={{"spielerId",number(1)},{"gewaehrt",number(granted)},{"verbraucht",number(used)}};
        arr->elements.push_back(row);
    }
}
"""

MAIN = r"""
char today[11];
void reset() {
    g_store={}; s_payment_receipt_count=0; s_payment_receipt_date[0]=0;
    g_store.kreditPlayerIds[0]=1; g_store.kredite[0]={2,1};g_store.lineupIds[0]=1;
    g_store.munition[0]={1,3,4}; strcpy(g_store.kreditDatum,today);
    persistence_ok=true; saves=commits=0;
}
BillDaySummary paid(const char *id) {
    BillDaySummary bill={};bill.authoritative=true;strcpy(bill.datum,today);bill.playerCount=1;
    bill.players[0].spielerId=1;bill.players[0].state=BILL_PAID;strcpy(bill.players[0].paymentExternalId,id);
    return bill;
}
int main() {
    time_t now=time(nullptr);struct tm t;localtime_r(&now,&t);strftime(today,sizeof(today),"%Y-%m-%d",&t);
    reset(); auto bill=paid("remote-payment");
    // Even a cached PAID snapshot from the old firmware must not suppress first reconciliation.
    g_store.billDayBaseline=bill;assert(store_has_unapplied_paid_sessions());
    store_cache_bill_day(&bill);
    assert(g_store.kreditPlayerIds[0]==0 && g_store.lineupIds[0]==0);
    assert(g_store.kredite[0].gewaehrt==0 && g_store.munition[0].cal12==0);
    assert(g_store.billDayBaseline.playerCount==1); // historical bill remains intact
    g_store.kreditPlayerIds[0]=1;g_store.kredite[0]={0,0}; // explicit later re-add
    store_cache_bill_day(&bill);assert(g_store.kreditPlayerIds[0]==1);
    assert(!store_has_unapplied_paid_sessions());
    auto saved=s_payment_receipts[0];int count=s_payment_receipt_count;
    memset(s_payment_receipts,0,sizeof(s_payment_receipts));s_payment_receipt_count=0;
    s_payment_receipts[0]=saved;s_payment_receipt_count=count; // restored durable journal
    store_cache_bill_day(&bill);assert(g_store.kreditPlayerIds[0]==1);
    g_store.kredite[0]={2,1};auto next=paid("next-payment");store_cache_bill_day(&next);
    assert(g_store.kreditPlayerIds[0]==0);
    reset();g_store.pendingKreditEventCount=1;
    auto &event=g_store.pendingKreditEvents[0];event.spielerId=1;strcpy(event.datum,today);strcpy(event.typ,"GRANT");event.anzahl=-1;
    store_cache_bill_day(&bill);assert(g_store.kreditPlayerIds[0]==1 && s_payment_receipt_count==0);
    credit_response(today,2,1);assert(http_pull_kredite()==ESP_OK);
    assert(g_store.kredite[0].gewaehrt==1 && g_store.kredite[0].verbraucht==1);
    g_store.pendingKreditEventCount=0;store_cache_bill_day(&bill);assert(g_store.kreditPlayerIds[0]==0);
    reset();credit_response(today,1,1);assert(http_pull_kredite()==ESP_OK);
    assert(g_store.kredite[0].gewaehrt==1 && g_store.kredite[0].verbraucht==1 && g_store.lineupIds[0]==0);
    reset();credit_response(today,2,1);assert(http_pull_kredite()==ESP_OK);
    assert(g_store.lineupIds[0]==1); // funded players never see a transient zero baseline
    reset();credit_response(today);assert(http_pull_kredite()==ESP_OK);
    assert(g_store.kredite[0].gewaehrt==0 && g_store.kredite[0].verbraucht==0);
    assert(g_store.kreditPlayerIds[0]==1); // unpaid zero-credit Catering members are not removed
    reset();g_store.pendingKreditEventCount=1;g_store.pendingKreditEvents[0]={1,{}, {},3};
    strcpy(g_store.pendingKreditEvents[0].datum,today);strcpy(g_store.pendingKreditEvents[0].typ,"GRANT");
    credit_response(today);assert(http_pull_kredite()==ESP_OK);assert(g_store.kredite[0].gewaehrt==3);
    reset();credit_response("1999-01-01");assert(http_pull_kredite()==ESP_ERR_INVALID_RESPONSE);
    assert(g_store.kredite[0].gewaehrt==2 && commits==0);
    reset();credit_response(today,1,1);
    json_root->fields["kredite"]->elements[0]->fields["gewaehrt"]->valuedouble=1.5;
    assert(http_pull_kredite()==ESP_ERR_INVALID_RESPONSE && g_store.kredite[0].gewaehrt==2 && commits==0);
    reset();bill.authoritative=false;store_cache_bill_day(&bill);assert(g_store.kreditPlayerIds[0]==1);
    reset();bill.authoritative=true;strcpy(bill.datum,"1999-01-01");store_cache_bill_day(&bill);
    assert(g_store.kreditPlayerIds[0]==1);
    reset();g_store.pendingPaymentEventCount=1;
    auto &payment=g_store.pendingPaymentEvents[0];payment.spielerId=1;
    strcpy(payment.datum,today);strcpy(payment.externalId,"own-payment");
    PaymentEvent snapshot=payment;const char *accepted[]={"own-payment"};
    persistence_ok=false;
    assert(!finish_payment_sync_internal(&snapshot,1,accepted,1,"",true));
    assert(g_store.kreditPlayerIds[0]==1 && g_store.pendingPaymentEventCount==1 && s_payment_receipt_count==0);
    persistence_ok=true;
    assert(finish_payment_sync_internal(&snapshot,1,accepted,1,"",true));
    assert(g_store.kreditPlayerIds[0]==0 && s_payment_receipt_count==1);
    g_store.kreditPlayerIds[0]=1;g_store.kredite[0]={0,0};auto own=paid("own-payment");
    store_cache_bill_day(&own);assert(g_store.kreditPlayerIds[0]==1);
    // A real post-payment grant starts a fresh funded session, despite the old paid audit bill.
    store_apply_portal_kredit(1,2,0);assert(g_store.kredite[0].gewaehrt==2);
    puts("PASS: remote/local payment removes daily/Catering member, lineup and operational counters; audit history retained");
    puts("PASS: receipt replay/reboot and later re-add; pending corrections protected; failure restores payment and roster");
    puts("PASS: full/empty credit baseline clears stale balances and reapplies pending grants; wrong dates/invalid numbers do not mutate data");
}
"""

if __name__ == "__main__":
    source = (ROOT / "main/store/game_store.cpp").read_text()
    network = (ROOT / "main/net/http_sync.cpp").read_text()
    functions = "\n".join(fixture.function(source, name) for name in [
        "static void prepare_payment_receipts", "static int payment_receipt_slot",
        "static bool has_pending_day_activity", "static void reset_munition_for_player",
        "static bool bill_is_authoritatively_paid", "static bool finish_payment_sync_internal",
        "bool store_has_unapplied_paid_sessions", "void store_cache_bill_day",
        "void store_apply_portal_kredit"])
    save = fixture.function(source, "void game_store_save")
    init = fixture.function(source, "void game_store_init")
    assert '"paid_seen"' in save and '"paid_seen_day"' in save and '"paid_seen"' in init
    assert "store_has_unapplied_paid_sessions()" in fixture.function(network, "static esp_err_t http_sync_all_impl")
    with tempfile.TemporaryDirectory(prefix="tm-cross-terminal-") as directory:
        path = Path(directory)
        (path / "test.cpp").write_text(STUBS + functions +
            fixture.function(network, "esp_err_t http_pull_kredite") + MAIN)
        subprocess.run(["g++", "-std=c++17", "-Os", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "main/store"), str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)