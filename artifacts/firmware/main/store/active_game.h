#pragma once
#include <stddef.h>
// Two bounded, checksummed FAT slots. Not rebuildable cache; never cleanup.
bool active_game_restore();
bool active_game_checkpoint();
bool active_game_remap(); // rewrite both slots before acknowledging player-ID changes
size_t active_game_record_size();
bool active_game_repair_safe_locked(); // read-only CRC proof; caller holds journal lock
