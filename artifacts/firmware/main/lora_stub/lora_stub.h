#pragma once
// ============================================================
// Terminal-to-gateway machine firing interface.
// ============================================================
#include <stddef.h>
#include "game_store.h"

/** Create the persistent fire worker. Must be called once at boot. */
void lora_stub_init(void);

/**
 * Send a fire command for machine m.
 * Queues an HTTP request to the configured local gateway and returns
 * immediately; it never performs network I/O on the LVGL task.
 */
bool lora_fire_machine(Maschine m);

/**
 * Send one authenticated, ordered A-G custom doublette to the gateway.
 * The gateway persists and coordinates both radio commands; this function
 * queues only one HTTP request and never blocks the LVGL task.
 */
bool lora_fire_doublette(Maschine first, Maschine second, uint16_t delay_ms);

/**
 * Queue a non-actuating authenticated request to verify that the configured
 * gateway is reachable and accepts the configured HMAC key.
 */
bool lora_gateway_check(void);

/** True while the gateway worker is processing a FIRE or health request. */
bool lora_request_busy(void);

/** Copy the latest user-visible result safely into caller-owned storage. */
void lora_copy_status_text(char *out, size_t out_len);
