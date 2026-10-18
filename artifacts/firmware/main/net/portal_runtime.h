#pragma once
bool portal_queue_repair_safe_locked(); // CRC-verified files, no state-lock inversion
#include "portal_model.h"
void portal_runtime_init();
void portal_queue_refresh();
bool portal_queue_snapshot(PortalQueueSnapshot *out);
bool portal_queue_take(const PortalQueueGame *game,bool merge);
bool portal_queue_release();
bool portal_queue_game_started();
void portal_queue_status(char *out,size_t size);
bool portal_queue_reserved();
bool portal_queue_busy();
bool portal_queue_current(PortalQueueGame *out);
unsigned portal_queue_generation();
/** O(1), RAM only. Called after start/result/undo/finish/cancel, NEVER on FIRE. */
void portal_live_changed(int state); // 1 RUNNING, 2 FINISHED, 3 ABORTED
