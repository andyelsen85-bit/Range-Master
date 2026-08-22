#pragma once
// ============================================================
// Terminal-to-gateway machine firing interface.
// ============================================================
#include "game_store.h"

/** Create the persistent fire worker. Must be called once at boot. */
void lora_stub_init(void);

/**
 * Send a fire command for machine m.
 * Queues an HTTP request to the configured local gateway and returns
 * immediately; it never performs network I/O on the LVGL task.
 */
void lora_fire_machine(Maschine m);

/** Latest user-visible result, safe to read from the LVGL task. */
const char *lora_status_text(void);
