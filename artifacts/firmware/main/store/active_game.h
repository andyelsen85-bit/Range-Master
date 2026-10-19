#pragma once
#include <stddef.h>
#include "storage_repair.h"
// Two bounded, checksummed FAT slots. Not rebuildable cache; never cleanup.
bool active_game_restore();
bool active_game_checkpoint();
void active_game_retry_finished_checkpoint(); // worker-only retry of a completed tombstone
bool active_game_remap(); // rewrite both slots before acknowledging player-ID changes
size_t active_game_record_size();
bool active_game_repair_safe_locked(); // read-only CRC proof; caller holds journal lock
bool active_game_inspect_repair_locked(ActiveGameRepairInspection *out);
bool active_game_is_open();
void active_game_mark_ended(); // RAM only; caller first persists journal/cancel intent
#ifdef ESP_PLATFORM
void active_game_writer_init();
void active_game_schedule();
bool active_game_flush(); // worker only; never wait while holding the LVGL lock
bool active_game_checkpoint_pending();
bool active_game_writer_idle_for(unsigned milliseconds);
void active_game_request_flush(); // request immediate checkpoint without waiting
#else
inline void active_game_schedule(){(void)active_game_checkpoint();}
inline bool active_game_checkpoint_pending(){return false;}
#endif
