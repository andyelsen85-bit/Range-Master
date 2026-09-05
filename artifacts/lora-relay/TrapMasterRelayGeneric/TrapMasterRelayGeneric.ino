// Generic TrapMaster relay — Heltec Wireless Stick V3 (ESP32-S3 + SX1262).
//
// Unlike the fixed A-H projects, this build keeps its machine assignment in
// NVS. Connect to its own WiFi hotspot and select A-H in the captive portal.

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"

#if __has_include("../TrapMasterRelay.local.h")
#include "../TrapMasterRelay.local.h"
#elif __has_include("../TrapMasterRelay/TrapMasterRelay.local.h")
#include "../TrapMasterRelay/TrapMasterRelay.local.h"
#endif

#if __has_include("../../lora-common/trapmaster_protocol.h")
#include "../../lora-common/trapmaster_protocol.h"
#include "../../lora-common/trapmaster_auth.h"
#else
#error "Could not locate the shared TrapMaster LoRa headers"
#endif

#define RF_FREQUENCY               433000000
#define TX_OUTPUT_POWER            14
#define LORA_BANDWIDTH             0
#define LORA_SPREADING_FACTOR      7
#define LORA_CODINGRATE            1
#define LORA_PREAMBLE_LENGTH       8
#define LORA_SYMBOL_TIMEOUT        0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON       false

#ifndef TM_RELAY_GPIO
#define TM_RELAY_GPIO 4
#endif

#ifndef TM_RELAY_ACTIVE_LEVEL
#define TM_RELAY_ACTIVE_LEVEL LOW
#endif

#ifndef TM_RELAY_PULSE_MS
#define TM_RELAY_PULSE_MS 300
#endif

#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
#ifndef TM_BENCH_INTERLOCK_GPIO
#error "Define TM_BENCH_INTERLOCK_GPIO to a verified physical test-enable input"
#endif
#endif

RadioEvents_t radioEvents;
Preferences preferences;
DNSServer dnsServer;
WebServer configServer(80);
SSD1306Wire relayDisplay(0x3c, 500000, SDA_OLED, SCL_OLED,
                         GEOMETRY_64_32, RST_OLED);

static char machineId = 0;
static char configApSsid[32] = {};
static uint32_t lastAcceptedCounter = 0;
static volatile bool firePending = false;
static uint32_t pendingCounter = 0;

static bool validMachineId(char value)
{
    return value >= 'A' && value <= 'H';
}

static void renderMachineId()
{
    relayDisplay.clear();
    relayDisplay.setTextAlignment(TEXT_ALIGN_CENTER);
    relayDisplay.setFont(ArialMT_Plain_24);
    relayDisplay.drawString(32, 3, validMachineId(machineId)
                                      ? String(machineId)
                                      : String("?"));
    relayDisplay.display();
}

static String portalPage(const String &message = "")
{
    String options;
    for (char id = 'A'; id <= 'H'; ++id) {
        options += "<option value='";
        options += id;
        options += "'";
        if (id == machineId) options += " selected";
        options += ">Maschine ";
        options += id;
        options += "</option>";
    }

    String page =
        "<!doctype html><html lang='de'><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>TrapMaster Empfänger</title><style>"
        "body{font-family:Arial,sans-serif;background:#111827;color:#f9fafb;"
        "margin:0;padding:24px}main{max-width:420px;margin:40px auto;"
        "background:#1f2937;padding:24px;border-radius:14px}"
        "h1{font-size:24px;margin-top:0;color:#38bdf8}"
        "label{display:block;margin:20px 0 8px;font-weight:bold}"
        "select,button{box-sizing:border-box;width:100%;min-height:48px;"
        "font-size:18px;border-radius:8px}select{padding:8px;background:#fff}"
        "button{margin-top:18px;border:0;background:#2563eb;color:#fff;"
        "font-weight:bold}.ok{padding:12px;background:#14532d;border-radius:8px}"
        ".warn{color:#fbbf24;font-size:14px}</style></head><body><main>"
        "<h1>TrapMaster Empfänger</h1>";
    if (message.length()) page += "<p class='ok'>" + message + "</p>";
    page += "<p>Aktuelle Zuordnung: <strong>";
    page += validMachineId(machineId) ? String("Maschine ") + machineId
                                     : String("nicht konfiguriert");
    page +=
        "</strong></p><form method='post' action='/save'>"
        "<label for='machine'>Maschinen-ID auswählen</label>"
        "<select id='machine' name='machine'>" +
        options +
        "</select><button type='submit'>Zuordnung speichern</button></form>"
        "<p class='warn'>Eine Änderung gilt sofort. Der Wiederholungszähler "
        "wird aus Sicherheitsgründen nicht zurückgesetzt.</p>"
        "</main></body></html>";
    return page;
}

static void redirectToPortal()
{
    configServer.sendHeader("Location", String("http://") +
                                            WiFi.softAPIP().toString() + "/",
                            true);
    configServer.send(302, "text/plain", "");
}

static void handleSave()
{
    if (!configServer.hasArg("machine") ||
        configServer.arg("machine").length() != 1) {
        configServer.send(400, "text/html; charset=utf-8",
                          portalPage("Ungültige Maschinen-ID."));
        return;
    }

    char selected = configServer.arg("machine")[0];
    if (!validMachineId(selected)) {
        configServer.send(400, "text/html; charset=utf-8",
                          portalPage("Nur A bis H sind erlaubt."));
        return;
    }
    if (preferences.putChar("machine_id", selected) != sizeof(char)) {
        configServer.send(500, "text/html; charset=utf-8",
                          portalPage("Speichern fehlgeschlagen."));
        return;
    }

    machineId = selected;
    renderMachineId();
    Serial.printf("Generic relay assigned to machine %c\n", machineId);
    configServer.send(200, "text/html; charset=utf-8",
                      portalPage(String("Maschine ") + machineId +
                                 " wurde gespeichert."));
}

static void startConfigPortal()
{
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(configApSsid, sizeof(configApSsid), "TrapMaster-Relay-%06llX",
             (unsigned long long)(chipId & 0xFFFFFFULL));

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(configApSsid)) {
        Serial.println("Configuration hotspot could not be started");
        return;
    }

    configServer.on("/", HTTP_GET, []() {
        configServer.send(200, "text/html; charset=utf-8", portalPage());
    });
    configServer.on("/save", HTTP_POST, handleSave);
    configServer.on("/generate_204", HTTP_ANY, redirectToPortal);
    configServer.on("/hotspot-detect.html", HTTP_ANY, redirectToPortal);
    configServer.on("/connecttest.txt", HTTP_ANY, redirectToPortal);
    configServer.on("/ncsi.txt", HTTP_ANY, redirectToPortal);
    configServer.onNotFound(redirectToPortal);
    configServer.begin();
    dnsServer.start(53, "*", WiFi.softAPIP());

    Serial.printf("Configuration hotspot: %s at %s\n", configApSsid,
                  WiFi.softAPIP().toString().c_str());
}

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
    if (!validMachineId(machineId)) {
        Serial.println("Rejected packet: machine ID is not configured");
        Radio.Rx(0);
        return;
    }
#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
    if (digitalRead(TM_BENCH_INTERLOCK_GPIO) == LOW &&
        size == 3 && payload[0] == 'T' && payload[1] == 'B' &&
        payload[2] == machineId) {
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
        decoded.machine != machineId ||
        decoded.counter <= lastAcceptedCounter) {
        Serial.println("Rejected wrong-address or replayed packet");
        Radio.Rx(0);
        return;
    }

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
    if (!validMachineId(machineId)) {
        Radio.Rx(0);
        return;
    }
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
    if (tm_protocol::encrypt(machineId, tm_protocol::CMD_ACK, counter, frame)) {
        Radio.Send(frame, sizeof(frame));
    } else {
        Radio.Rx(0);
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);
    delay(100);
    relayDisplay.init();

    pinMode(TM_RELAY_GPIO, OUTPUT);
#if defined(TM_ENABLE_UNENCRYPTED_BENCH_TEST)
    pinMode(TM_BENCH_INTERLOCK_GPIO, INPUT_PULLUP);
#endif
    digitalWrite(TM_RELAY_GPIO, !TM_RELAY_ACTIVE_LEVEL);

    if (!preferences.begin("trapmaster", false)) {
        Serial.println("Relay NVS unavailable; refusing to start");
        renderMachineId();
        while (true) delay(1000);
    }
    machineId = preferences.getChar("machine_id", 0);
    if (!validMachineId(machineId)) machineId = 0;
    lastAcceptedCounter = preferences.getULong("last_counter", 0);
    renderMachineId();

    startConfigPortal();
    initRadio();
    Serial.printf("Generic relay %c listening on 433 MHz (counter=%lu)\n",
                  validMachineId(machineId) ? machineId : '?',
                  (unsigned long)lastAcceptedCounter);
}

void loop()
{
    dnsServer.processNextRequest();
    configServer.handleClient();
    Radio.IrqProcess();
    if (firePending) {
        firePending = false;
        fireRelayAndAck(pendingCounter);
    }
}