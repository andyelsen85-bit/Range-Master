#include "game_store.h"
#include "active_game.h"
#include "game_journal.h"
#include "terminal_logs.h"
#include "ui_manager.h"
#include "storage_write.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <atomic>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#ifndef ACTIVE_GAME_ROOT
#define ACTIVE_GAME_ROOT "/fatfs"
#endif
struct ActiveData {
    Modus modus;
    int lauf,taubeIndex,spielerIndex,sequenzLen,spielerCount,ergebnisseCount;
    int activeAcknowledgedClays,activeGameCreditCount;
    int activeGameCreditPlayerIds[MAX_SPIELER];
    char activeGameCreditUseIds[MAX_SPIELER][40];
    char spielId[40];
    Spieler spieler[MAX_SPIELER];
    SequenzEintrag sequenz[MAX_SEQUENZ];
    uint8_t harakiriOrder[MAX_SPIELER][HARAKIRI_MAX_CLAYS];
    Ergebnis ergebnisse[MAX_ERGEBNISSE];
    bool currentFireSent;
    int customRuns;
};
struct ActiveRecord {
    uint32_t magic,version,bytes,checksum,generation;
    bool active;
    ActiveData data;
};
static_assert(sizeof(ActiveRecord)<=8192,"Two active-game slots plus scratch must fit in 24 KiB");
static bool initialized=false,blocked=false;
static std::atomic<bool> had_active{false};
static std::atomic<uint32_t> next_generation{0};
static uint32_t committed_generation=0;
static int committed_slot=-1;
static uint32_t hash(const ActiveRecord &r) {
    uint32_t h=2166136261u;const auto *b=(const uint8_t*)&r;
    for(size_t i=0;i<sizeof(r);i++)
        if(i<offsetof(ActiveRecord,checksum)||i>=offsetof(ActiveRecord,checksum)+sizeof(r.checksum))
            h=(h^b[i])*16777619u;
    return h;
}
static void path(int slot,char *out,size_t n) {
    snprintf(out,n,ACTIVE_GAME_ROOT "/active%d.bin",slot);
}
#define ACTIVE_FIELDS(X) \
    X(modus) X(lauf) X(taubeIndex) X(spielerIndex) X(sequenzLen) X(spielerCount) \
    X(ergebnisseCount) X(activeAcknowledgedClays) X(activeGameCreditCount) \
    X(activeGameCreditPlayerIds) X(activeGameCreditUseIds) X(spielId) X(spieler) \
    X(sequenz) X(harakiriOrder) X(ergebnisse) X(currentFireSent)

static bool valid(const ActiveRecord &r) {
    if(r.magic!=0x48414b49 || r.version!=1 || r.bytes!=sizeof(r) ||
       r.checksum!=hash(r))return false;
    if(!r.active)return true;
    const auto &d=r.data;
    if((d.modus!=MODUS_HARAKIRI && d.modus!=MODUS_HARAKIRI_CUSTOM) ||
       d.spielerCount<1 || d.spielerCount>MAX_SPIELER || d.sequenzLen<1 ||
       d.sequenzLen>HARAKIRI_MAX_CLAYS || d.spielerIndex<0 ||
       d.spielerIndex>=d.spielerCount || d.taubeIndex<0 || d.taubeIndex>d.sequenzLen ||
       d.lauf<1 || d.lauf>2 || d.customRuns<1 || d.customRuns>2 ||
       d.ergebnisseCount<0 || d.ergebnisseCount>MAX_ERGEBNISSE ||
       d.activeGameCreditCount<0 || d.activeGameCreditCount>MAX_SPIELER ||
       !d.spielId[0] || d.spielId[39])return false;
    for(int p=0;p<d.spielerCount;p++) {
        if(d.spieler[p].name[MAX_NAME_LEN-1])return false;
        bool seen[HARAKIRI_MAX_CLAYS]={};
        for(int c=0;c<d.sequenzLen;c++) {
            int index=d.harakiriOrder[p][c];
            if(index>=d.sequenzLen || seen[index])return false;
            seen[index]=true;
            const auto &entry=d.sequenz[index];
            if(entry.maschine<MASCHINE_A || entry.maschine>=MASCHINE_COUNT)return false;
            if(entry.isPair && !entry.isDoublette &&
               (c+1>=d.sequenzLen || d.harakiriOrder[p][c+1]!=index+1 ||
                !d.sequenz[index+1].isDoublette))return false;
            if(entry.isPair && entry.isDoublette &&
               (c==0 || d.harakiriOrder[p][c-1]!=index-1))return false;
        }
    }
    return true;
}
static bool read_file(const char *name,ActiveRecord *out,bool *present) {
    errno=0;
    FILE *f=fopen(name,"rb");
    if(!f){*present=errno!=ENOENT;return false;}
    *present=true;
    bool ok=fread(out,1,sizeof(*out),f)==sizeof(*out) && fgetc(f)==EOF && !ferror(f);
    if(fclose(f)!=0)ok=false;
    return ok&&valid(*out);
}
static bool read(int slot,ActiveRecord *out,bool *present,bool strict=false) {
    char name[96],temp[104];path(slot,name,sizeof(name));
    snprintf(temp,sizeof(temp),"%s.tmp",name);
    bool bin_present=false,tmp_present=false;
    bool ok=read_file(name,out,&bin_present);
    struct stat st{};errno=0;
    if(stat(temp,&st)!=0&&errno==ENOENT){*present=bin_present;return ok;}
    auto *candidate=(ActiveRecord*)heap_caps_malloc(sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!candidate){*present=true;return false;}
    bool tmp_ok=read_file(temp,candidate,&tmp_present);
    if(tmp_ok&&(!ok||candidate->generation>=out->generation))*out=*candidate;
    heap_caps_free(candidate);*present=bin_present||tmp_present;
    if(strict&&((bin_present&&!ok)||(tmp_present&&!tmp_ok)))return false;
    return ok||tmp_ok;
}
size_t active_game_record_size(){return sizeof(ActiveRecord);}
bool active_game_repair_safe_locked() {
    ActiveGameRepairInspection result{};
    return active_game_inspect_repair_locked(&result);
}
bool active_game_inspect_repair_locked(ActiveGameRepairInspection *out) {
    *out={};out->had_active=had_active.load();
    out->ram_active=g_store.harakiriOrdersReady||g_store.screen==SCREEN_SPIEL;
    DIR *dir=opendir(ACTIVE_GAME_ROOT);
    if(!dir){out->unreadable=1;out->error=errno;return false;}
    ActiveRecord *r=nullptr;bool found=false;uint32_t generation=0;
    for(;;) {
        errno=0;auto *entry=readdir(dir);
        if(!entry){if(errno){++out->unreadable;out->error=errno;}break;}
        const char *n=entry->d_name;
        if(strcasecmp(n,"active0.bin")&&strcasecmp(n,"active1.bin")&&
           strcasecmp(n,"active0.bin.tmp")&&strcasecmp(n,"active1.bin.tmp"))continue;
        ++out->present;
        // Empty roots need no PSRAM at all. Allocation failure is uncertainty
        // only when a real record exists, never synthetic "present=true".
        if(!r)r=(ActiveRecord*)heap_caps_malloc(sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!r){++out->unreadable;out->error=ENOMEM;continue;}
        char file[384];snprintf(file,sizeof(file),ACTIVE_GAME_ROOT "/%.255s",n);
        bool present=false;
        if(read_file(file,r,&present)) {
            if(!found||r->generation>generation){found=true;generation=r->generation;out->active=r->active;}
        } else {++out->unreadable;out->error=errno?errno:EBADMSG;}
    }
    if(closedir(dir)!=0){++out->unreadable;out->error=errno;}
    heap_caps_free(r);
    out->stale_latch=out->had_active&&!out->present&&!out->unreadable&&!out->ram_active;
    // This is only a volatile cache of the last successful checkpoint. It is
    // not evidence of pending work when BOTH disk and live RAM are empty.
    if(out->stale_latch) {
        had_active=false;
        terminal_log_add("sync","INFO","Aktives Spiel gemerkt (had_active), aber Dateien und RAM leer: veraltete Markierung zurückgesetzt");
    }
    return !out->unreadable&&!out->active&&!out->ram_active&&(!out->had_active||out->stale_latch);
}
static void remap_record(ActiveRecord *r) {
    if(!r->active)return;
    for(int i=0;i<r->data.spielerCount;i++)
        r->data.spieler[i].id=game_journal_resolve_player_locked(r->data.spieler[i].id);
    for(int i=0;i<r->data.ergebnisseCount;i++)
        r->data.ergebnisse[i].spielerId=game_journal_resolve_player_locked(r->data.ergebnisse[i].spielerId);
    for(int i=0;i<r->data.activeGameCreditCount;i++)
        r->data.activeGameCreditPlayerIds[i]=game_journal_resolve_player_locked(r->data.activeGameCreditPlayerIds[i]);
}
static bool write_slot(int slot,ActiveRecord *r) {
    r->checksum=hash(*r);
    char name[96],temp[104];path(slot,name,sizeof(name));
    snprintf(temp,sizeof(temp),"%s.tmp",name);
    FILE *f=fopen(temp,"wb");bool ok=f!=nullptr,complete=false;
    if(f) {
        ok=storage_write_committed(f,r,sizeof(*r),&complete);
        if(fclose(f)!=0)ok=false;
    }
    if(!ok){if(!complete)(void)unlink(temp);return false;}
    if(unlink(name)!=0&&errno!=ENOENT)return false;
    ok=rename(temp,name)==0; // Complete temp remains recoverable if rename fails.
    return ok;
}
bool active_game_remap() {
    if(!initialized)return true;
    if(blocked)return false;
    game_journal_storage_lock();
    if(committed_slot<0){game_journal_storage_unlock();return true;}
    auto *r=(ActiveRecord*)heap_caps_malloc(sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool present=false,ok=r&&read(committed_slot,r,&present);
    if(ok) {
        remap_record(r);
        // Identity-only rewrite: keep the gameplay revision. A newer score
        // already captured by LVGL must not be superseded by this disk copy.
        int other=1-committed_slot;
        ok=write_slot(other,r);
        if(ok){committed_slot=other;ok=write_slot(1-other,r);}
    }
    heap_caps_free(r);game_journal_storage_unlock();return ok;
}

bool active_game_checkpoint() {
    if(!initialized)return true; // NVS normalization precedes the FAT mount.
    if(blocked)return false; // Never overwrite an unrecognized active record.
    bool acquired=false;
    if(!ui_manager_snapshot_lock(&acquired))return false;
    auto *r=(ActiveRecord*)heap_caps_calloc(1,sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!r){g_store.activeGameStorageError=true;if(acquired)ui_manager_unlock();return false;}
    r->active=g_store.harakiriOrdersReady;
    if(!r->active&&!had_active){
        heap_caps_free(r);if(acquired)ui_manager_unlock();return true;
    }
    r->magic=0x48414b49;r->version=1;r->bytes=sizeof(*r);r->generation=++next_generation;
    if(r->active) {
#define PACK(field) memcpy(&r->data.field,&g_store.field,sizeof(r->data.field));
        ACTIVE_FIELDS(PACK)
#undef PACK
        r->data.customRuns=g_store.modus==MODUS_HARAKIRI_CUSTOM ? g_store.customLaeufe[4] : 2;
    }
    if(acquired)ui_manager_unlock();
    game_journal_storage_lock();
    bool ok=true;
    if(r->generation>committed_generation) {
        // Rewrite only the older slot: the last committed file always survives.
        remap_record(r); // includes snapshots captured before RAM publication
        int slot=committed_slot==0?1:0;
        ok=write_slot(slot,r);
        if(ok) {
            committed_generation=r->generation;committed_slot=slot;had_active=r->active;
        }
    }
    uint32_t generation=r->generation;
    game_journal_storage_unlock();heap_caps_free(r);
    // A delayed ACK/settings save must never clear the error from a newer,
    // failed score checkpoint. Recheck its generation under the UI lock.
    if(ui_manager_snapshot_lock(&acquired)) {
        if(generation==next_generation.load())g_store.activeGameStorageError=!ok;
        if(acquired)ui_manager_unlock();
    }
    if(!ok)terminal_log_add("data_loss_risk","ERROR","Harakiri-Spielstand nicht gespeichert; bitte Speicher prüfen");
    return ok;
}

void active_game_retry_finished_checkpoint() {
    bool acquired=false;
    if(!ui_manager_snapshot_lock(&acquired))return;
    bool retry=initialized && !blocked && had_active.load() && !g_store.harakiriOrdersReady;
    if(acquired)ui_manager_unlock();
    if(retry)(void)active_game_checkpoint();
}

bool active_game_restore() {
    ActiveGameRepairInspection initial{};
    (void)active_game_inspect_repair_locked(&initial);
    if(!initial.present&&!initial.unreadable&&!initial.ram_active) {
        // No checkpoint files is a normal boot, not a reason to allocate two
        // large records and latch an OOM storage error on an empty filesystem.
        initialized=true;blocked=false;had_active=false;committed_slot=-1;
        committed_generation=next_generation=0;g_store.activeGameStorageError=false;
        return true;
    }
    auto *r=(ActiveRecord*)heap_caps_malloc(sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    auto *best=(ActiveRecord*)heap_caps_calloc(1,sizeof(ActiveRecord),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!r||!best){
        heap_caps_free(r);heap_caps_free(best);blocked=true;initialized=true;
        g_store.activeGameStorageError=true;
        snprintf(g_store.lineupWarning,sizeof(g_store.lineupWarning),"Zu wenig RAM zum Prüfen des offenen Harakiri-Spiels.");
        return false;
    }
    bool any=false,invalid=false;int best_slot=-1;
    for(int slot=0;slot<2;slot++) {
        bool present=false;
        bool ok=read(slot,r,&present);
        if(ok && (best_slot<0 || r->generation>best->generation)){*best=*r;best_slot=slot;}
        if(present&&!ok)invalid=true;
        any|=present;
    }
    blocked=any&&best_slot<0;initialized=true;
    if(!any&&!g_store.harakiriOrdersReady&&g_store.screen!=SCREEN_SPIEL)had_active=false;
    if(blocked) {
        g_store.activeGameStorageError=true;
        snprintf(g_store.lineupWarning,sizeof(g_store.lineupWarning),
                 "Harakiri-Spielstand beschädigt. Nicht überschreiben; Diagnose erforderlich.");
        terminal_log_add("data_loss_risk","ERROR",g_store.lineupWarning);
    }
    if(best_slot>=0) {
        // A complete temporary can be the newest durable checkpoint after a
        // reset between unlink and rename. Canonicalize it before the next save.
        char name[96],temp[104];path(best_slot,name,sizeof(name));
        snprintf(temp,sizeof(temp),"%s.tmp",name);
        bool present=false;
        if(read_file(temp,r,&present)&&r->generation==best->generation) {
            if((unlink(name)!=0&&errno!=ENOENT)||rename(temp,name)!=0)invalid=true;
        }
        committed_generation=next_generation=best->generation;committed_slot=best_slot;had_active=best->active;
        // A crash between final-journal commit and active tombstone must not
        // resurrect an already finished (possibly already accepted) game.
        bool cancelled=best->active&&store_credit_game_cancelled(best->data.spielId);
        bool pending_finish=false;
        for(int i=0;i<g_store.pendingGamesCount;i++)
            if(!strcmp(g_store.pendingGames[i].externalId,best->data.spielId))pending_finish=true;
        int finished=best->active&&!cancelled?
            (pending_finish?1:game_journal_contains(best->data.spielId)):0;
        if(best->active&&!cancelled && (finished<0 || g_store.operatingMode==TERMINAL_MODE_CATERING)) {
            blocked=true;g_store.activeGameStorageError=true;
            terminal_log_add("data_loss_risk","ERROR","Offenes Harakiri-Spiel gesperrt: Archiv prüfen oder Catering-Modus verlassen");
        } else if(best->active&&!finished&&!cancelled) {
#define UNPACK(field) memcpy(&g_store.field,&best->data.field,sizeof(g_store.field));
            ACTIVE_FIELDS(UNPACK)
#undef UNPACK
            g_store.harakiriOrdersReady=true;
            if(g_store.modus==MODUS_HARAKIRI_CUSTOM)g_store.customLaeufe[4]=best->data.customRuns;
            g_store.screen=SCREEN_SPIEL;
            // A physical shot may have happened just before power loss.
            // Never authorize a duplicate automatically after restart.
            g_store.currentFireSent=true;
            g_store.activeGameResumed=true;
            terminal_log_add("game","WARN","Harakiri-Spiel fortgesetzt; letzte Einheit vor erneutem Auslösen prüfen");
            if(invalid)terminal_log_add("data_loss_risk","WARN",
                "Unvollständiger Spielstand: letzte gültige Sicherung geladen; letzte Wertung prüfen");
        } else if(best->active) {
            // Finished journal OR durable credit cancellation wins.
            g_store.harakiriOrdersReady=false;
            if(cancelled)terminal_log_add("game","WARN","Storniertes Spiel nach Neustart nicht fortgesetzt; Kredit-Storno bleibt erhalten");
        }
    }
    heap_caps_free(r);heap_caps_free(best);
    if(best_slot>=0 && had_active && !g_store.harakiriOrdersReady)return active_game_checkpoint();
    return !blocked;
}
