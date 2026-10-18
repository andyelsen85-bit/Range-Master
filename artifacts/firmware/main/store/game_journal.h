#pragma once
int game_journal_retry_acks(void); // bounded maintenance, sync worker only
#include "game_store.h"
bool game_journal_init();
bool game_journal_save(const FinishedGame *game);
bool game_journal_ack(const char *external_id);
int game_journal_recover(unsigned days);
bool game_journal_remap(int old_id,int new_id);
bool game_journal_ready();
int game_journal_contains(const char *external_id); // 1 valid, 0 absent, -1 uncertain
int game_journal_resolve_player_locked(int id); // caller holds storage lock
int game_journal_manifest(const char *day,char (*ids)[40],int capacity);
int game_journal_reload_pending();
// Maintenance holds the journal lock across proof, cache deletion and remount.
// Inspection is read-only; malformed/unreadable records make proof fail closed.
struct GameJournalInspection { int pending, accepted, uncertain; uint64_t bytes; };
void game_journal_storage_lock();
void game_journal_storage_unlock();
bool game_journal_inspect_locked(GameJournalInspection *out);
size_t game_journal_record_size();
bool game_journal_resume_after_cleanup(); // read-only journal validation, no pruning/rewrites
bool game_journal_hold_pending(const FinishedGame *game); // bounded emergency NVS slot
bool game_journal_recheck_ready(); // clears transient failure only after a fresh proof
void game_journal_maintenance(); // worker context, 1s/5s/30s retry; never LVGL timer
bool game_journal_is_file(const char *name);
bool game_journal_self_test();
bool game_journal_read(const char *id,FinishedGame *out);
// Only the authenticated, exported and twice-confirmed recovery path may call.
bool game_journal_discard(const char *id);
int game_journal_pending_ids(char (*ids)[40],int capacity);
