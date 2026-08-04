#pragma once
// ============================================================
// LoRa UART stub — placeholder for Phase 2 machine control
// (Task #49: fire clay machines A–H via SX1276/RFM95)
// ============================================================
#include "game_store.h"

/** Initialise the LoRa UART port (no-op until phase 2). */
void lora_stub_init(void);

/**
 * Send a fire command for machine m.
 * Phase 1: logs the command and returns immediately.
 * Phase 2: will transmit a LoRa packet to the machine node.
 */
void lora_fire_machine(Maschine m);
