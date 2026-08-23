// TrapMaster gateway — Heltec Wireless Stick V3 (ESP32-S3 + SX1262).
//
// Board support: install Heltec ESP32 Dev-Boards from HeltecAutomation and select
// Wireless Stick V3. The official library owns the SX1262 pin mapping; do not add
// manual radio SPI/DIO pin definitions here.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ctype.h>
#include <stdlib.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#if __has_include("TrapMasterGateway.local.h")
#include "TrapMasterGateway.local.h"
#endif
#if __has_include("../lora-common/trapmaster_protocol.h")
#include "../lora-common/trapmaster_protocol.h"
#include "../lora-common/trapmaster_auth.h"
#elif __has_include("../../lora-common/trapmaster_protocol.h")
#include "../../lora-common/trapmaster_protocol.h"
#include "../../lora-common/trapmaster_auth.h"
#else
#error "Could not locate the shared TrapMaster LoRa headers"
#endif

#ifndef TM_GATEWAY_AUTH_KEY
#error "Define TM_GATEWAY_AUTH_KEY to a private terminal-to-gateway HMAC key before compiling"
#endif
static constexpr char GATEWAY_AUTH_KEY[] = TM_GATEWAY_AUTH_KEY;
static_assert(sizeof(GATEWAY_AUTH_KEY) > 16,
              "TM_GATEWAY_AUTH_KEY must be at least 16 characters");

#define RF_FREQUENCY               433000000
#define TX_OUTPUT_POWER            14
#define LORA_BANDWIDTH             0
#define LORA_SPREADING_FACTOR      7
#define LORA_CODINGRATE            1
#define LORA_PREAMBLE_LENGTH       8
#define LORA_SYMBOL_TIMEOUT        0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON       false
#define ACK_TIMEOUT_MS             3000
#define TX_TIMEOUT_MS              500
#define PAIR_MAX_DELAY_MS          10000
#define PAIR_HTTP_TIMEOUT_MS       15000

#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
#ifndef TM_BENCH_INTERLOCK_GPIO
#error "Define TM_BENCH_INTERLOCK_GPIO to a verified physical test-enable input"
#endif
static bool bench_interlock_asserted()
{
    return digitalRead(TM_BENCH_INTERLOCK_GPIO) == LOW;
}
#endif

// The installed Heltec package defines its own internal `display` symbol but
// does not expose it in every board-header variant. Use a uniquely named
// gateway object so both the library and this sketch link cleanly.
SSD1306Wire gatewayDisplay(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_64_32, RST_OLED);
WebServer server(80);
WiFiManager wifiManager;
Preferences preferences;
RadioEvents_t radioEvents;

static uint32_t nextCounter = 0;
enum RequestState : uint8_t {
    REQUEST_NONE = 0,
    REQUEST_UNKNOWN = 1,
    REQUEST_COMPLETE = 2,
    REQUEST_PAIR_FIRST_COMPLETE = 3,
};
enum RequestKind : uint8_t { REQUEST_SINGLE = 0, REQUEST_PAIR = 1 };
static uint32_t lastSequence = 0;
static RequestKind lastRequestKind = REQUEST_SINGLE;
static char lastRequestMachine = 0;
static char lastRequestSecondMachine = 0;
static uint32_t lastRequestDelayMs = 0;
static RequestState lastRequestState = REQUEST_NONE;
static bool lastRequestAck = false;
static bool lastRequestSecondAck = false;
static uint32_t expectedAckCounter = 0;
static char expectedAckMachine = 0;
static volatile bool txDone = false;
static volatile bool txTimeout = false;
static volatile bool ackReceived = false;
static volatile bool radioBusy = false;
static String lastResult = "Starting";
static String lastMachine = "-";
static bool lastAck = false;

static bool parse_sequence(uint32_t *sequence)
{
    if (!sequence || !server.hasArg("seq")) return false;
    String text = server.arg("seq");
    if (text.length() != 8) return false;
    for (size_t i = 0; i < 8; ++i)
        if (!isxdigit((unsigned char)text[i])) return false;
    unsigned long value = strtoul(text.c_str(), nullptr, 16);
    if (value == 0) return false;
    *sequence = (uint32_t)value;
    return true;
}

static bool request_is_authentic(char machine, uint32_t sequence)
{
    if (!server.hasHeader("X-TrapMaster-Auth")) return false;
    uint8_t expected[tm_auth::MAC_LEN];
    if (!tm_auth::make_request_mac((const uint8_t *)GATEWAY_AUTH_KEY,
                                   sizeof(GATEWAY_AUTH_KEY) - 1,
                                   machine, sequence, expected)) {
        return false;
    }
    return tm_auth::mac_matches_hex(expected, server.header("X-TrapMaster-Auth").c_str());
}

static bool pair_request_is_authentic(char first, char second,
                                      uint32_t delay_ms, uint32_t sequence)
{
    if (!server.hasHeader("X-TrapMaster-Auth")) return false;
    uint8_t expected[tm_auth::MAC_LEN];
    if (!tm_auth::make_pair_request_mac((const uint8_t *)GATEWAY_AUTH_KEY,
                                        sizeof(GATEWAY_AUTH_KEY) - 1,
                                        first, second, delay_ms, sequence, expected)) {
        return false;
    }
    return tm_auth::mac_matches_hex(expected, server.header("X-TrapMaster-Auth").c_str());
}

static bool health_request_is_authentic()
{
    if (!server.hasHeader("X-TrapMaster-Auth")) return false;
    uint8_t expected[tm_auth::MAC_LEN];
    if (!tm_auth::make_health_mac((const uint8_t *)GATEWAY_AUTH_KEY,
                                  sizeof(GATEWAY_AUTH_KEY) - 1, expected)) {
        return false;
    }
    return tm_auth::mac_matches_hex(expected, server.header("X-TrapMaster-Auth").c_str());
}

static bool persist_request(uint32_t sequence, RequestKind kind, char machine,
                            char second_machine, uint32_t delay_ms,
                            RequestState state, bool ack, bool second_ack)
{
    // Store the sequence before radio TX. A reset part-way through the
    // following writes still has a nonzero sequence and therefore fails closed.
    if (preferences.putULong("req_seq", sequence) != sizeof(uint32_t) ||
        preferences.putUChar("req_kind", (uint8_t)kind) != sizeof(uint8_t) ||
        preferences.putChar("req_machine", machine) != sizeof(char) ||
        preferences.putChar("req_second", second_machine) != sizeof(char) ||
        preferences.putULong("req_delay", delay_ms) != sizeof(uint32_t) ||
        preferences.putUChar("req_state", (uint8_t)state) != sizeof(uint8_t) ||
        preferences.putBool("req_ack", ack) != sizeof(bool) ||
        preferences.putBool("req_ack2", second_ack) != sizeof(bool)) {
        lastResult = "NVS request save failed";
        return false;
    }
    lastSequence = sequence;
    lastRequestKind = kind;
    lastRequestMachine = machine;
    lastRequestSecondMachine = second_machine;
    lastRequestDelayMs = delay_ms;
    lastRequestState = state;
    lastRequestAck = ack;
    lastRequestSecondAck = second_ack;
    return true;
}

static void renderStatus()
{
    gatewayDisplay.clear();
    gatewayDisplay.setTextAlignment(TEXT_ALIGN_LEFT);
    gatewayDisplay.setFont(ArialMT_Plain_10);
    gatewayDisplay.drawString(0, 0, WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "WiFi setup");
    gatewayDisplay.drawString(0, 10, "FIRE " + lastMachine);
    gatewayDisplay.drawString(0, 20, lastResult);
    gatewayDisplay.display();
}

static void onTxDone()
{
    txDone = true;
}

static void onTxTimeout()
{
    txTimeout = true;
    Radio.Sleep();
}

static void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    tm_protocol::DecodedPacket decoded = {};
    if (tm_protocol::decrypt(payload, size, &decoded) &&
        decoded.command == tm_protocol::CMD_ACK &&
        decoded.counter == expectedAckCounter &&
        decoded.machine == (uint8_t)expectedAckMachine) {
        ackReceived = true;
        Serial.printf("Authenticated ACK from %c RSSI=%d SNR=%d\n",
                      decoded.machine, rssi, snr);
    }
    Radio.Sleep();
}

static void onRxTimeout()
{
    Radio.Sleep();
}

static void initRadio()
{
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    radioEvents.TxDone = onTxDone;
    radioEvents.TxTimeout = onTxTimeout;
    radioEvents.RxDone = onRxDone;
    radioEvents.RxTimeout = onRxTimeout;
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
}

static bool waitForFlag(volatile bool &flag, uint32_t timeoutMs)
{
    uint32_t started = millis();
    while (!flag && millis() - started < timeoutMs) {
        Radio.IrqProcess();
        delay(1);
    }
    return flag;
}

static bool sendFire(char machine, bool *ack)
{
    if (radioBusy || !tm_protocol::valid_machine(machine)) return false;
    radioBusy = true;
    *ack = false;
    uint32_t counter = ++nextCounter;
    if (counter == 0) { // counter wrap would reuse a nonce under the same key
        lastResult = "Counter exhausted";
        radioBusy = false;
        return false;
    }
    // Persist before radio TX: a power loss can skip values, never reuse one.
    if (preferences.putULong("counter", counter) != sizeof(uint32_t)) {
        lastResult = "Counter save failed";
        radioBusy = false;
        return false;
    }

    uint8_t frame[tm_protocol::FRAME_LEN];
    if (!tm_protocol::encrypt(machine, tm_protocol::CMD_FIRE, counter, frame)) {
        lastResult = "Encrypt failed";
        radioBusy = false;
        return false;
    }

    expectedAckCounter = counter;
    expectedAckMachine = machine;
    ackReceived = false;
    txDone = false;
    txTimeout = false;
    Radio.Send(frame, sizeof(frame));
    if (!waitForFlag(txDone, TX_TIMEOUT_MS) || txTimeout) {
        lastResult = "LoRa TX failed";
        radioBusy = false;
        return false;
    }

    Radio.Rx(ACK_TIMEOUT_MS);
    *ack = waitForFlag(ackReceived, ACK_TIMEOUT_MS);
    lastAck = *ack;
    lastMachine = String(machine);
    lastResult = *ack ? "sent + ACK" : "sent, no ACK";
    radioBusy = false;
    renderStatus();
    return true;
}

#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
// Radio bring-up only. This endpoint is compiled out by default and must never
// be enabled after a relay is connected to a real trap.
static void handleBenchFire()
{
    // A compiled bench endpoint still needs a locally asserted hardware
    // interlock. It must never be wired/enabled on an installed trap.
    if (!bench_interlock_asserted()) {
        server.send(403, "application/json",
                    "{\"ok\":false,\"error\":\"physical bench interlock is open\"}");
        return;
    }
    if (!server.hasArg("machine") || server.arg("machine").length() != 1) {
        server.send(400, "application/json", "{\"error\":\"machine must be A-H\"}");
        return;
    }
    char machine = (char)toupper(server.arg("machine")[0]);
    if (!tm_protocol::valid_machine(machine) || radioBusy) {
        server.send(400, "application/json", "{\"error\":\"invalid request\"}");
        return;
    }
    uint8_t packet[3] = { 'T', 'B', (uint8_t)machine };
    radioBusy = true;
    txDone = false;
    txTimeout = false;
    Radio.Send(packet, sizeof(packet));
    bool sent = waitForFlag(txDone, TX_TIMEOUT_MS) && !txTimeout;
    radioBusy = false;
    lastMachine = String(machine);
    lastResult = sent ? "BENCH sent" : "BENCH TX failed";
    renderStatus();
    server.send(sent ? 200 : 502, "application/json",
                sent ? "{\"ok\":true,\"bench\":true}" : "{\"ok\":false}");
}
#endif

static void handleFire()
{
    if (!server.hasArg("machine") || server.arg("machine").length() != 1) {
        server.send(400, "application/json", "{\"error\":\"machine must be A-H\"}");
        return;
    }
    char machine = (char)toupper(server.arg("machine")[0]);
    if (!tm_protocol::valid_machine(machine)) {
        server.send(400, "application/json", "{\"error\":\"machine must be A-H\"}");
        return;
    }
    uint32_t sequence = 0;
    if (!parse_sequence(&sequence)) {
        server.send(400, "application/json", "{\"error\":\"seq must be 8 hex characters\"}");
        return;
    }
    if (!request_is_authentic(machine, sequence)) {
        server.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    if (sequence < lastSequence) {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"stale sequence\"}");
        return;
    }
    if (sequence == lastSequence) {
        if (lastRequestKind != REQUEST_SINGLE || machine != lastRequestMachine) {
            server.send(409, "application/json", "{\"ok\":false,\"error\":\"request conflicts with another machine\"}");
        } else if (lastRequestState == REQUEST_COMPLETE) {
            String body = "{\"ok\":true,\"machine\":\"" + String(machine) +
                          "\",\"ack\":" + String(lastRequestAck ? "true" : "false") +
                          ",\"cached\":true}";
            server.send(lastRequestAck ? 200 : 202, "application/json", body);
        } else {
            server.send(409, "application/json",
                        "{\"ok\":false,\"error\":\"previous request outcome is unknown\"}");
        }
        return;
    }
    if (!persist_request(sequence, REQUEST_SINGLE, machine, 0, 0,
                         REQUEST_UNKNOWN, false, false)) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"gateway storage unavailable\"}");
        return;
    }
    bool ack = false;
    if (!sendFire(machine, &ack)) {
        server.send(radioBusy ? 503 : 502, "application/json",
                    "{\"ok\":false,\"error\":\"LoRa send failed\"}");
        return;
    }
    if (!persist_request(sequence, REQUEST_SINGLE, machine, 0, 0,
                         REQUEST_COMPLETE, ack, false)) {
        server.send(503, "application/json",
                    "{\"ok\":false,\"error\":\"fire result could not be saved; do not retry\"}");
        return;
    }
    String body = "{\"ok\":true,\"machine\":\"" + String(machine) +
                  "\",\"ack\":" + String(ack ? "true" : "false") + "}";
    server.send(ack ? 200 : 202, "application/json", body);
}

static bool parse_pair_machine(const char *name, char *machine)
{
    if (!name || !machine || !server.hasArg(name)) return false;
    String text = server.arg(name);
    if (text.length() != 1) return false;
    char value = (char)toupper((unsigned char)text[0]);
    if (value < 'A' || value > 'G') return false; // H is its own local doublette.
    *machine = value;
    return true;
}

static bool parse_delay_ms(uint32_t *delay_ms)
{
    if (!delay_ms || !server.hasArg("delayMs")) return false;
    String text = server.arg("delayMs");
    if (text.length() == 0 || text.length() > 5) return false;
    for (size_t i = 0; i < text.length(); ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    unsigned long value = strtoul(text.c_str(), nullptr, 10);
    if (value > PAIR_MAX_DELAY_MS) return false;
    *delay_ms = (uint32_t)value;
    return true;
}

static void handleFirePair()
{
    char first = 0;
    char second = 0;
    uint32_t delay_ms = 0;
    uint32_t sequence = 0;
    if (!parse_pair_machine("first", &first) ||
        !parse_pair_machine("second", &second) || first == second) {
        server.send(400, "application/json",
                    "{\"error\":\"first and second must be different A-G machines\"}");
        return;
    }
    if (!parse_delay_ms(&delay_ms)) {
        server.send(400, "application/json",
                    "{\"error\":\"delayMs must be 0-10000 milliseconds\"}");
        return;
    }
    if (!parse_sequence(&sequence)) {
        server.send(400, "application/json", "{\"error\":\"seq must be 8 hex characters\"}");
        return;
    }
    if (!pair_request_is_authentic(first, second, delay_ms, sequence)) {
        server.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    if (sequence < lastSequence) {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"stale sequence\"}");
        return;
    }
    if (sequence == lastSequence) {
        bool same_pair = lastRequestKind == REQUEST_PAIR &&
                         first == lastRequestMachine &&
                         second == lastRequestSecondMachine &&
                         delay_ms == lastRequestDelayMs;
        if (!same_pair) {
            server.send(409, "application/json",
                        "{\"ok\":false,\"error\":\"request conflicts with another pair\"}");
        } else if (lastRequestState == REQUEST_COMPLETE) {
            String body = "{\"ok\":true,\"first\":\"" + String(first) +
                          "\",\"second\":\"" + String(second) +
                          "\",\"firstAck\":" + String(lastRequestAck ? "true" : "false") +
                          ",\"secondAck\":" + String(lastRequestSecondAck ? "true" : "false") +
                          ",\"cached\":true}";
            server.send(lastRequestAck && lastRequestSecondAck ? 200 : 202,
                        "application/json", body);
        } else {
            // The gateway records before each physical action. A reboot or
            // storage error between the two commands is deliberately fail-closed.
            server.send(409, "application/json",
                        "{\"ok\":false,\"error\":\"pair outcome is unknown; do not retry\"}");
        }
        return;
    }
    if (!persist_request(sequence, REQUEST_PAIR, first, second, delay_ms,
                         REQUEST_UNKNOWN, false, false)) {
        server.send(503, "application/json",
                    "{\"ok\":false,\"error\":\"gateway storage unavailable\"}");
        return;
    }

    bool first_ack = false;
    if (!sendFire(first, &first_ack)) {
        server.send(502, "application/json",
                    "{\"ok\":false,\"error\":\"first machine send failed; do not retry\"}");
        return;
    }
    if (!first_ack) {
        persist_request(sequence, REQUEST_PAIR, first, second, delay_ms,
                        REQUEST_COMPLETE, false, false);
        server.send(202, "application/json",
                    "{\"ok\":false,\"partial\":true,\"firstAck\":false,\"secondSent\":false}");
        return;
    }
    if (!persist_request(sequence, REQUEST_PAIR, first, second, delay_ms,
                         REQUEST_PAIR_FIRST_COMPLETE, true, false)) {
        server.send(503, "application/json",
                    "{\"ok\":false,\"error\":\"pair progress could not be saved; do not retry\"}");
        return;
    }

    if (delay_ms > 0) delay(delay_ms);

    bool second_ack = false;
    if (!sendFire(second, &second_ack)) {
        persist_request(sequence, REQUEST_PAIR, first, second, delay_ms,
                        REQUEST_COMPLETE, true, false);
        server.send(502, "application/json",
                    "{\"ok\":false,\"partial\":true,\"firstAck\":true,\"secondSent\":false}");
        return;
    }
    if (!persist_request(sequence, REQUEST_PAIR, first, second, delay_ms,
                         REQUEST_COMPLETE, true, second_ack)) {
        server.send(503, "application/json",
                    "{\"ok\":false,\"error\":\"pair result could not be saved; do not retry\"}");
        return;
    }
    String body = "{\"ok\":true,\"first\":\"" + String(first) +
                  "\",\"second\":\"" + String(second) +
                  "\",\"firstAck\":true,\"secondAck\":" +
                  String(second_ack ? "true" : "false") + "}";
    server.send(second_ack ? 200 : 202, "application/json", body);
}

static void handleStatus()
{
    String body = "{\"ok\":true,\"uptimeMs\":" + String(millis()) +
                  ",\"ip\":\"" + WiFi.localIP().toString() +
                  "\",\"rssi\":" + String(WiFi.RSSI()) +
                  ",\"lastMachine\":\"" + lastMachine +
                  "\",\"lastResult\":\"" + lastResult +
                  "\",\"lastAck\":" + String(lastAck ? "true" : "false") + "}";
    server.send(200, "application/json", body);
}

// This endpoint never transmits LoRa. It lets the terminal prove that the
// configured local URL is reachable and that both devices have the same HMAC
// key before an operator can request a machine launch.
static void handleHealth()
{
    if (!health_request_is_authentic()) {
        server.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    String body = "{\"ok\":true,\"auth\":true,\"ip\":\"" + WiFi.localIP().toString() +
                  "\",\"uptimeMs\":" + String(millis()) + "}";
    server.send(200, "application/json", body);
}

static bool shouldResetWifi()
{
    pinMode(0, INPUT_PULLUP); // USER button in the official Wireless Stick V3 example
    if (digitalRead(0) != LOW) return false;
    delay(1500);
    return digitalRead(0) == LOW;
}

void setup()
{
    Serial.begin(115200);
#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
    pinMode(TM_BENCH_INTERLOCK_GPIO, INPUT_PULLUP);
#endif
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW); // OLED rail on (official Heltec behavior)
    delay(100);
    gatewayDisplay.init();
    renderStatus();

    if (shouldResetWifi()) wifiManager.resetSettings();
    WiFi.mode(WIFI_STA);
    wifiManager.setConfigPortalTimeout(180);
    if (!wifiManager.autoConnect("TrapMaster-Gateway-Setup")) {
        lastResult = "WiFi setup failed";
        renderStatus();
        delay(3000);
        ESP.restart();
    }

    if (!preferences.begin("trapmaster", false)) {
        lastResult = "NVS unavailable";
        renderStatus();
        while (true) delay(1000); // never transmit if monotonic state cannot persist
    }
    nextCounter = preferences.getULong("counter", 0);
    lastSequence = preferences.getULong("req_seq", 0);
    lastRequestKind = (RequestKind)preferences.getUChar("req_kind", REQUEST_SINGLE);
    lastRequestMachine = preferences.getChar("req_machine", 0);
    lastRequestSecondMachine = preferences.getChar("req_second", 0);
    lastRequestDelayMs = preferences.getULong("req_delay", 0);
    lastRequestState = (RequestState)preferences.getUChar("req_state", REQUEST_NONE);
    lastRequestAck = preferences.getBool("req_ack", false);
    lastRequestSecondAck = preferences.getBool("req_ack2", false);
    initRadio();
    const char *headerKeys[] = { "X-TrapMaster-Auth" };
    server.collectHeaders(headerKeys, 1);
    server.on("/fire", HTTP_GET, handleFire);
    server.on("/fire-pair", HTTP_GET, handleFirePair);
    server.on("/health", HTTP_GET, handleHealth);
    server.on("/status", HTTP_GET, handleStatus);
#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
    server.on("/bench-fire", HTTP_GET, handleBenchFire);
#endif
    server.begin();
    lastResult = "Ready";
    renderStatus();
}

void loop()
{
    server.handleClient();
    Radio.IrqProcess();
}