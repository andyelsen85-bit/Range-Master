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
