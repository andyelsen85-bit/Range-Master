// TrapMaster relay unit — Heltec Wireless Stick V3 (ESP32-S3 + SX1262).
//
// This sketch intentionally includes NO WiFi code. The official Heltec library
// owns all SX1262 wiring. Define the relay GPIO from the verified Wiring Stick V3
// pinout / actual wiring before building; an unknown GPIO must never be guessed.

#include <Arduino.h>
#include <Preferences.h>
#include "LoRaWan_APP.h"
#if __has_include("TrapMasterRelay.local.h")
#include "TrapMasterRelay.local.h"
#endif
#include "../lora-common/trapmaster_protocol.h"

#define RF_FREQUENCY               433000000
#define TX_OUTPUT_POWER            14
#define LORA_BANDWIDTH             0
#define LORA_SPREADING_FACTOR      7
#define LORA_CODINGRATE            1
#define LORA_PREAMBLE_LENGTH       8
#define LORA_SYMBOL_TIMEOUT        0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON       false

// Change this per flashed relay. Keep ASCII A through H. The same sketch is
// flashed eight times with IDs A, B, C, D, E, F, G, and H.
#ifndef TM_MACHINE_ID
#define TM_MACHINE_ID 'A'
#endif

// Verified installation mapping: every relay module currently uses GPIO4.
// Recheck the wiring before connecting a real trap; change this local setting
// if the physical relay input is moved.
#ifndef TM_RELAY_GPIO
#define TM_RELAY_GPIO 4
#endif

#ifndef TM_RELAY_ACTIVE_LEVEL
#define TM_RELAY_ACTIVE_LEVEL LOW // verified active-low relay modules
#endif

#ifndef TM_RELAY_PULSE_MS
#define TM_RELAY_PULSE_MS 300
#endif

#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
#ifndef TM_BENCH_INTERLOCK_GPIO
#error "Define TM_BENCH_INTERLOCK_GPIO to a verified physical test-enable input"
#endif
#endif

static_assert(TM_MACHINE_ID >= 'A' && TM_MACHINE_ID <= 'H',
              "TM_MACHINE_ID must be an ASCII letter A through H");

RadioEvents_t radioEvents;
Preferences preferences;
static uint32_t lastAcceptedCounter = 0;
static volatile bool firePending = false;
static uint32_t pendingCounter = 0;

static void onTxDone()
{
    Radio.Rx(0);
}

static void onTxTimeout()
{
    Radio.Rx(0);
}

static void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
    // Radio-link bench mode only. It is excluded from normal builds and must
    // never be flashed onto a relay connected to a physical trap.
    if (digitalRead(TM_BENCH_INTERLOCK_GPIO) == LOW &&
        size == 3 && payload[0] == 'T' && payload[1] == 'B' &&
        payload[2] == TM_MACHINE_ID) {
        pendingCounter = 0;
        firePending = true;
        Radio.Sleep();
        return;
    }
#endif
    tm_protocol::DecodedPacket decoded = {};
    if (!tm_protocol::decrypt(payload, size, &decoded)) {
        Serial.println("Rejected unauthenticated packet");
        Radio.Rx(0);
        return;
    }
    if (decoded.command != tm_protocol::CMD_FIRE ||
        decoded.machine != TM_MACHINE_ID ||
        decoded.counter <= lastAcceptedCounter) {
        Serial.println("Rejected wrong-address or replayed packet");
        Radio.Rx(0);
        return;
    }

    // Radio is put to sleep until the pending command is handled, so no second
    // packet can overwrite this command before its counter is saved.
    pendingCounter = decoded.counter;
    firePending = true;
    Serial.printf("Authenticated FIRE %c counter=%lu RSSI=%d SNR=%d\n",
                  decoded.machine, (unsigned long)decoded.counter, rssi, snr);
    Radio.Sleep();
}

static void initRadio()
{
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    radioEvents.TxDone = onTxDone;
    radioEvents.TxTimeout = onTxTimeout;
    radioEvents.RxDone = onRxDone;
    Radio.Init(&radioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
    Radio.Rx(0);
}

static void fireRelayAndAck(uint32_t counter)
{
    // Persist before touching the output. A restart must never make an accepted
    // command replayable. If NVS cannot save the counter, fail closed.
    if (counter != 0) {
        if (preferences.putULong("last_counter", counter) != sizeof(uint32_t)) {
            Serial.println("Rejected FIRE: replay counter persistence failed");
            Radio.Rx(0);
            return;
        }
        lastAcceptedCounter = counter;
    }

    digitalWrite(TM_RELAY_GPIO, TM_RELAY_ACTIVE_LEVEL);
    delay(TM_RELAY_PULSE_MS);
    digitalWrite(TM_RELAY_GPIO, !TM_RELAY_ACTIVE_LEVEL);

    uint8_t frame[tm_protocol::FRAME_LEN];
    if (tm_protocol::encrypt(TM_MACHINE_ID, tm_protocol::CMD_ACK, counter, frame)) {
        Radio.Send(frame, sizeof(frame));
    } else {
        Radio.Rx(0);
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(TM_RELAY_GPIO, OUTPUT);
#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
    pinMode(TM_BENCH_INTERLOCK_GPIO, INPUT_PULLUP);
#endif
    // Keep the dry contact inactive during every boot and radio initialization step.
    digitalWrite(TM_RELAY_GPIO, !TM_RELAY_ACTIVE_LEVEL);
    if (!preferences.begin("trapmaster", false)) {
        Serial.println("Relay NVS unavailable; refusing to start");
        while (true) delay(1000);
    }
    lastAcceptedCounter = preferences.getULong("last_counter", 0);
    initRadio();
    Serial.printf("Relay %c listening on 433 MHz (counter=%lu)\n",
                  TM_MACHINE_ID, (unsigned long)lastAcceptedCounter);
}

void loop()
{
    Radio.IrqProcess();
    if (firePending) {
        firePending = false;
        fireRelayAndAck(pendingCounter);
    }
}