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
#include "esp_random.h"
#include "nonce_registry.h"
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
class GatewayWebServer : public WebServer {
public:
    using WebServer::WebServer;
    WiFiClient detachClient() {
        WiFiClient client = _currentClient;
        _currentClient = WiFiClient();
        return client;
    }
};
GatewayWebServer server(80);
WiFiManager wifiManager;
Preferences preferences;
RadioEvents_t radioEvents;

static uint32_t nextCounter = 0;
static tm_gateway::NonceRegistry nonces;
static tm_gateway::Entry *pendingFire = nullptr;
// Copies share ownership of the socket; WebServer can accept other requests
// after the handler returns while this original awaits its radio result.
static WiFiClient pendingClient;
static uint32_t expectedAckCounter = 0;
static char expectedAckMachine = 0;
static volatile bool txDone = false;
static volatile bool txTimeout = false;
static volatile bool ackReceived = false;
static volatile bool radioBusy = false;
static String lastResult = "Starting";
static String lastMachine = "-";
static bool lastAck = false;
static bool networkSettingsSaved = false;

static void renderStatus();

// ── WiFi addressing ────────────────────────────────────────────
// The gateway uses DHCP unless the operator enables static addressing in the
// WiFiManager portal. Static mode deliberately uses a conventional /24 subnet
// and the configured router as DNS; the operator only needs to enter the two
// addresses that identify the venue network.
static const IPAddress STATIC_SUBNET(255, 255, 255, 0);
static bool staticAddressEnabled = false;
static char staticAddressText[16] = {};
static char staticGatewayText[16] = {};

static char staticModeAttrs[80] = {};
static const char IPV4_INPUT_ATTRS[] =
    "inputmode='decimal' pattern='[0-9]{1,3}(\\.[0-9]{1,3}){3}'";
static WiFiManagerParameter staticModeParam(
    "tm_static", "Use fixed IPv4 address (DHCP off)", "", 2,
    staticModeAttrs, WFM_LABEL_AFTER);
static WiFiManagerParameter staticAddressParam(
    "tm_static_ip", "Fixed IPv4 address", "", 15, IPV4_INPUT_ATTRS);
static WiFiManagerParameter staticGatewayParam(
    "tm_static_gateway", "Router / gateway IPv4", "", 15, IPV4_INPUT_ATTRS);

static bool parseStaticNetwork(const char *addressText, const char *gatewayText,
                               IPAddress *address, IPAddress *gateway)
{
    if (!addressText || !gatewayText || !address || !gateway ||
        !address->fromString(addressText) || !gateway->fromString(gatewayText)) {
        return false;
    }
    // The configured /24 requires the device and router on the same subnet.
    if ((*address)[0] != (*gateway)[0] || (*address)[1] != (*gateway)[1] ||
        (*address)[2] != (*gateway)[2] || *address == *gateway ||
        (*address)[3] == 0 || (*address)[3] == 255 ||
        (*gateway)[3] == 0 || (*gateway)[3] == 255) {
        return false;
    }
    return true;
}

static void loadNetworkSettings()
{
    staticAddressEnabled = preferences.getBool("net_static", false);
    preferences.getString("net_ip", "").toCharArray(staticAddressText,
                                                     sizeof(staticAddressText));
    preferences.getString("net_gw", "").toCharArray(staticGatewayText,
                                                     sizeof(staticGatewayText));
}

static void configureNetworkPortal()
{
    snprintf(staticModeAttrs, sizeof(staticModeAttrs),
             "type='checkbox' value='1'%s",
             staticAddressEnabled ? " checked" : "");
    staticModeParam.setValue(staticAddressEnabled ? "1" : "", 2);
    staticAddressParam.setValue(staticAddressText, sizeof(staticAddressText) - 1);
    staticGatewayParam.setValue(staticGatewayText, sizeof(staticGatewayText) - 1);
    wifiManager.addParameter(&staticModeParam);
    wifiManager.addParameter(&staticAddressParam);
    wifiManager.addParameter(&staticGatewayParam);
}

static void saveNetworkSettings()
{
    // An unchecked HTML checkbox is intentionally absent from the form, making
    // this test reliable when the operator switches back from static to DHCP.
    bool useStatic = wifiManager.server &&
                     wifiManager.server->hasArg("tm_static");
    String addressValue = staticAddressParam.getValue();
    String gatewayValue = staticGatewayParam.getValue();
    IPAddress address;
    IPAddress gateway;

    if (useStatic && !parseStaticNetwork(addressValue.c_str(), gatewayValue.c_str(),
                                         &address, &gateway)) {
        lastResult = "Static IP invalid";
        Serial.println("Static IPv4 rejected; DHCP setting unchanged");
        renderStatus();
        return;
    }

    size_t savedAddressLength = preferences.putString("net_ip", addressValue);
    size_t savedGatewayLength = preferences.putString("net_gw", gatewayValue);
    if (preferences.putBool("net_static", useStatic) != sizeof(bool) ||
        (addressValue.length() > 0 && savedAddressLength == 0) ||
        (gatewayValue.length() > 0 && savedGatewayLength == 0)) {
        lastResult = "Network save failed";
        Serial.println("Could not save network address settings");
        renderStatus();
        return;
    }

    staticAddressEnabled = useStatic;
    addressValue.toCharArray(staticAddressText, sizeof(staticAddressText));
    gatewayValue.toCharArray(staticGatewayText, sizeof(staticGatewayText));
    networkSettingsSaved = true;
    Serial.printf("Saved WiFi mode: %s\n", useStatic ? "static" : "DHCP");
}

static void applyNetworkSettings()
{
    if (!staticAddressEnabled) {
        Serial.println("WiFi address mode: DHCP");
        return;
    }

    IPAddress address;
    IPAddress gateway;
    if (!parseStaticNetwork(staticAddressText, staticGatewayText, &address, &gateway)) {
        // Do not let an incomplete persisted value block access to the setup portal.
        staticAddressEnabled = false;
        preferences.putBool("net_static", false);
        Serial.println("Saved static IPv4 invalid; falling back to DHCP");
        return;
    }

    wifiManager.setSTAStaticIPConfig(address, gateway, STATIC_SUBNET, gateway);
    Serial.printf("WiFi address mode: static %s via %s\n",
                  staticAddressText, staticGatewayText);
}

static bool request_is_authentic(char machine, const uint8_t *nonce)
{
    if (!server.hasHeader("X-TrapMaster-Auth")) return false;
    uint8_t expected[tm_auth::MAC_LEN];
    if (!tm_auth::make_request_mac((const uint8_t *)GATEWAY_AUTH_KEY,
                                   sizeof(GATEWAY_AUTH_KEY) - 1,
                                   machine, nonce, expected)) {
        return false;
    }
    return tm_auth::mac_matches_hex(expected, server.header("X-TrapMaster-Auth").c_str());
}

static bool pair_request_is_authentic(char first, char second,
                                      uint32_t delay_ms, const uint8_t *nonce)
{
    if (!server.hasHeader("X-TrapMaster-Auth")) return false;
    uint8_t expected[tm_auth::MAC_LEN];
    if (!tm_auth::make_pair_request_mac((const uint8_t *)GATEWAY_AUTH_KEY,
                                        sizeof(GATEWAY_AUTH_KEY) - 1,
                                        first, second, delay_ms, nonce, expected)) {
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

static void handleNonce()
{
    uint8_t bytes[tm_auth::NONCE_LEN];
    char hex[tm_auth::NONCE_HEX_LEN + 1];
    esp_fill_random(bytes, sizeof(bytes)); // hardware entropy with WiFi active
    tm_auth::nonce_to_hex(bytes, hex);
    server.sendHeader("Cache-Control", "no-store");
    if (!nonces.issue(hex, millis())) {
        server.sendHeader("Retry-After", "1");
        server.send(429, "application/json",
                    "{\"ok\":false,\"error\":\"nonce rate or capacity limit\"}");
        return;
    }
    char body[96];
    snprintf(body, sizeof(body), "{\"nonce\":\"%s\",\"expiresInMs\":10000}", hex);
    server.send(200, "application/json", body);
}

static void admitFire(const uint8_t *nonce, bool pair, char first, char second,
                      uint32_t delayMs)
{
    char hex[tm_auth::NONCE_HEX_LEN + 1];
    tm_auth::nonce_to_hex(nonce, hex);
    tm_gateway::Entry *entry = nullptr;
    auto admission = nonces.admit(hex, pair, first, second, delayMs,
                                 pendingFire != nullptr || radioBusy, millis(), &entry);
    server.sendHeader("Cache-Control", "no-store");
    switch (admission) {
    case tm_gateway::Admission::Unauthorized:
        server.send(401, "application/json", "{\"ok\":false,\"error\":\"unknown or expired nonce\"}");
        return;
    case tm_gateway::Admission::Conflict:
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"conflicting request\"}");
        return;
    case tm_gateway::Admission::Busy:
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
        return;
    case tm_gateway::Admission::Cached:
        server.send(entry->status, "application/json", entry->response);
        return;
    case tm_gateway::Admission::Start:
        pendingFire = entry;
        pendingClient = server.detachClient();
        return; // execution is outside the handler, never nested inside it
    }
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
        if (pendingFire) server.handleClient();
        Radio.IrqProcess();
        delay(1);
    }
    return flag;
}

static bool sendFire(char machine, bool *ack, bool wait_for_ack)
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

    if (!wait_for_ack) {
        lastAck = false;
        lastMachine = String(machine);
        lastResult = "sent, ACK skipped";
        radioBusy = false;
        renderStatus();
        return true;
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
    if (!tm_protocol::valid_machine(machine) || radioBusy || pendingFire) {
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
    uint8_t nonce[tm_auth::NONCE_LEN];
    if (!tm_auth::nonce_from_hex(server.arg("nonce").c_str(), nonce)) {
        server.send(401, "application/json", "{\"ok\":false,\"error\":\"invalid nonce\"}");
        return;
    }
    if (!request_is_authentic(machine, nonce)) {
        server.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    admitFire(nonce, false, machine, 0, 0);
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
    uint8_t nonce[tm_auth::NONCE_LEN];
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
    if (!tm_auth::nonce_from_hex(server.arg("nonce").c_str(), nonce)) {
        server.send(401, "application/json", "{\"ok\":false,\"error\":\"invalid nonce\"}");
        return;
    }
    if (!pair_request_is_authentic(first, second, delay_ms, nonce)) {
        server.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    admitFire(nonce, true, first, second, delay_ms);
}

static void finishPendingFire(int status, const char *body)
{
    // Cache even failures before writing the socket. A lost HTTP response must
    // never permit a second transmission; these results use preallocated RAM.
    nonces.finish(pendingFire, status, body, millis());
    pendingClient.printf("HTTP/1.1 %d Result\r\nContent-Type: application/json\r\n"
                         "Content-Length: %u\r\nConnection: close\r\n"
                         "Cache-Control: no-store\r\n\r\n%s",
                         status, (unsigned)strlen(pendingFire->response),
                         pendingFire->response);
    pendingClient.stop();
    pendingClient = WiFiClient();
    pendingFire = nullptr;
}

static void runPendingFire()
{
    if (!pendingFire) return;
    const char first = pendingFire->first, second = pendingFire->second;
    const uint32_t delay_ms = pendingFire->delay;
    if (!pendingFire->pair) {
        bool ack = false;
        if (!sendFire(first, &ack, true)) {
            finishPendingFire(lastResult == "Counter save failed" ? 503 : 502,
                              "{\"ok\":false,\"error\":\"LoRa send failed; do not retry as new fire\"}");
            return;
        }
        char body[128];
        snprintf(body, sizeof(body), "{\"ok\":true,\"machine\":\"%c\",\"ack\":%s}",
                 first, ack ? "true" : "false");
        finishPendingFire(ack ? 200 : 202, body);
        return;
    }
    bool first_ack = false;
    // Zero means launch both machines back-to-back. We wait only for the
    // first radio transmission to complete, not its ACK, so a valid 0-second
    // doublette cannot be delayed by the ACK timeout.
    bool wait_for_first_ack = delay_ms > 0;
    if (!sendFire(first, &first_ack, wait_for_first_ack)) {
        finishPendingFire(lastResult == "Counter save failed" ? 503 : 502,
                    "{\"ok\":false,\"error\":\"first machine send failed; do not retry\"}");
        return;
    }
    if (wait_for_first_ack && !first_ack) {
        finishPendingFire(202,
                    "{\"ok\":false,\"partial\":true,\"firstAck\":false,\"secondSent\":false}");
        return;
    }
    uint32_t started = millis();
    while (millis() - started < delay_ms) {
        server.handleClient(); // answer busy throughout the pair delay
        Radio.IrqProcess();
        delay(1);
    }
    bool second_ack = false;
    char body[tm_gateway::RESPONSE_BYTES];
    if (!sendFire(second, &second_ack, true)) {
        snprintf(body, sizeof(body), "{\"ok\":false,\"partial\":true,\"firstAck\":%s,"
                 "\"firstAckWaited\":%s,\"secondSent\":false}",
                 first_ack ? "true" : "false", wait_for_first_ack ? "true" : "false");
        finishPendingFire(lastResult == "Counter save failed" ? 503 : 502, body);
        return;
    }
    snprintf(body, sizeof(body), "{\"ok\":true,\"first\":\"%c\",\"second\":\"%c\","
             "\"firstAck\":%s,\"firstAckWaited\":%s,\"secondAck\":%s}", first, second,
             first_ack ? "true" : "false", wait_for_first_ack ? "true" : "false",
             second_ack ? "true" : "false");
    finishPendingFire(second_ack ? 200 : 202, body);
}

static void handleStatus()
{
    String body = "{\"ok\":true,\"uptimeMs\":" + String(millis()) +
                  ",\"ip\":\"" + WiFi.localIP().toString() +
                  "\",\"networkMode\":\"" +
                  String(staticAddressEnabled ? "static" : "dhcp") +
                  "\",\"gateway\":\"" +
                  String(staticAddressEnabled ? staticGatewayText : "") +
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
                  "\",\"uptimeMs\":" + String(millis()) +
                  ",\"fireAuth\":\"nonce-v1\",\"nonceTtlMs\":10000,\"resultTtlMs\":30000,"
                  "\"busy\":" + String(pendingFire ? "true" : "false") + "}";
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

    if (!preferences.begin("trapmaster", false)) {
        lastResult = "NVS unavailable";
        renderStatus();
        while (true) delay(1000); // never run without persistent security state
    }
    loadNetworkSettings();
    applyNetworkSettings();
    configureNetworkPortal();
    wifiManager.setSaveParamsCallback(saveNetworkSettings);

    if (shouldResetWifi()) wifiManager.resetSettings();
    WiFi.mode(WIFI_STA);
    wifiManager.setConfigPortalTimeout(180);
    if (!wifiManager.autoConnect("TrapMaster-Gateway-Setup")) {
        lastResult = "WiFi setup failed";
        renderStatus();
        delay(3000);
        ESP.restart();
    }
    // WiFiManager connects immediately after portal save. Restart once so a
    // changed DHCP/static selection is applied before the gateway starts HTTP.
    if (networkSettingsSaved) {
        lastResult = "Network saved - reboot";
        renderStatus();
        delay(1200);
        ESP.restart();
    }

    nextCounter = preferences.getULong("counter", 0);
    initRadio();
    const char *headerKeys[] = { "X-TrapMaster-Auth" };
    server.collectHeaders(headerKeys, 1);
    server.on("/nonce", HTTP_GET, handleNonce);
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
    runPendingFire();
    Radio.IrqProcess();
}