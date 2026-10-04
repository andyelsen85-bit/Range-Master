#!/usr/bin/env python3
"""Exercise actual batch admission/worker/UI callbacks with host mocks, not traps."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


STUBS = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <functional>
#define MAX_URL_LEN 256
#define MAX_KEY_LEN 256
enum Maschine { MASCHINE_A, MASCHINE_B, MASCHINE_C, MASCHINE_D,
    MASCHINE_E, MASCHINE_F, MASCHINE_G, MASCHINE_H, MASCHINE_COUNT };
enum GatewayReachability { GATEWAY_REACHABLE, GATEWAY_AUTH_FAILED, GATEWAY_FAILED, GATEWAY_BUSY };
struct {
    bool maschinenAktiv[8];
    char gatewayUrl[256], gatewayToken[256];
} g_store = {};
static bool s_request_busy = false;
static void *s_state_mutex = nullptr;
static void *s_gateway_queue = (void *)1;
static std::string status;
static char s_machine_test_status[256];
static bool s_machine_test_running;
static bool wifi = true, queueOK = true;
static int saves = 0, queues = 0, state = 0;
static std::vector<std::string> order;
#define portMAX_DELAY 0
#define pdTRUE 1
void xSemaphoreTake(void *, int) {}
void xSemaphoreGive(void *) {}
void set_status(const char *text) { status = text; }
void set_request_busy(bool value) { s_request_busy = value; }
void set_gateway_state(GatewayReachability value) { state = value; }
bool cop_wifi_is_connected() { return wifi; }
void game_store_save() { ++saves; order.push_back("save"); }
void lora_copy_status_text(char *out, size_t size) { snprintf(out, size, "%s", status.c_str()); }
bool lora_request_busy() { return s_request_busy; }
struct Call { char machine; std::string nonce, key, url; };
static std::vector<Call> calls;
static unsigned elapsed_ms = 0;
static std::vector<unsigned> start_times, finish_times, pauses;
static std::function<void()> during_pause;
#define pdMS_TO_TICKS(value) (value)
void vTaskDelay(unsigned ms) {
    pauses.push_back(ms); elapsed_ms += ms;
    if (during_pause) during_pause();
}
static std::vector<int> replies;
static Call auth;
namespace tm_auth {
    constexpr size_t MAC_HEX_LEN = 64, NONCE_LEN = 16, NONCE_HEX_LEN = 32;
}
bool perform_authenticated_get(const char *url, const char *, int *http, int, int) {
    start_times.push_back(elapsed_ms);
    auth.url = url; calls.push_back(auth);
    elapsed_ms += 375; // represents waiting for the complete HTTP/radio/ACK cycle
    finish_times.push_back(elapsed_ms);
    *http = calls.size() <= replies.size() ? replies[calls.size()-1] : 200;
    return *http >= 200 && *http < 300;
}
static unsigned nonce_counter = 0;
struct { void success() {} } s_health_history;
"""

UI_STUBS = r"""
struct lv_obj_t {};
struct lv_event_t { void *user_data; };
static lv_obj_t object, label;
static lv_obj_t *s_machine_test_confirm = nullptr;
static lv_obj_t *s_lbl_machine_test_status = &label;
static bool s_machine_test_pending = false;
static bool s_machine_test_bulk = false;
static std::string displayed_text;
static int displayed_color;
static std::string dialog_text;
static int opened = 0, closed = 0, font_assignments = 0;
constexpr int UI_FONT_16 = 16, UI_FONT_20 = 20;
constexpr int LV_EVENT_CLICKED = 1, CLR_WARN = 2, CLR_DANGER = 3, CLR_SUCCESS = 4;
void *lv_event_get_user_data(lv_event_t *event) { return event->user_data; }
void lv_msgbox_close(lv_obj_t *) { ++closed; }
lv_obj_t *lv_msgbox_create(void *) { ++opened; return &object; }
lv_obj_t *lv_msgbox_add_title(lv_obj_t *, const char *) { return &label; }
lv_obj_t *lv_msgbox_add_text(lv_obj_t *, const char *text) { dialog_text = text; return &label; }
lv_obj_t *lv_msgbox_add_footer_button(lv_obj_t *, const char *) { return &object; }
lv_obj_t *lv_obj_get_child(lv_obj_t *, int) { return &label; }
void lv_obj_set_style_text_font(lv_obj_t *, int font, int) {
    assert(font == UI_FONT_16 || font == UI_FONT_20); ++font_assignments;
}
void lv_obj_add_event_cb(lv_obj_t *, void (*)(lv_event_t *), int, void *) {}
void lv_label_set_text(lv_obj_t *, const char *text) { displayed_text = text; }
const char *lv_label_get_text(lv_obj_t *) { return displayed_text.c_str(); }
int lv_color_hex(int color) { return color; }
void lv_obj_set_style_text_color(lv_obj_t *, int color, int) { displayed_color = color; }
"""

MAIN = r"""
void reset() {
    g_store = {}; g_store.maschinenAktiv[0] = g_store.maschinenAktiv[7] = true;
    strcpy(g_store.gatewayUrl, "http://original-gateway");
    strcpy(g_store.gatewayToken, "synthetic-auth-key");
    wifi = queueOK = true; s_request_busy = false; saves = queues = 0;
    calls.clear(); replies.clear(); order.clear(); status.clear(); nonce_counter = 0;
    elapsed_ms = font_assignments = 0; start_times.clear(); finish_times.clear(); pauses.clear();
    s_machine_test_status[0] = 0; s_machine_test_running = false;
    s_machine_test_bulk = false; during_pause = {}; displayed_text.clear();
    s_machine_test_confirm = nullptr; s_machine_test_pending = false;
}
int main() {
    reset();
    assert(lora_test_enabled_machines(0x81));
    assert(s_request_busy && queues == 1 && saves == 0);
    assert((order == std::vector<std::string>{"queue"}));
    assert(queued.machine_mask == 0x81 && !queued.gameLaunch);
    strcpy(g_store.gatewayUrl, "http://changed-gateway");
    strcpy(g_store.gatewayToken, "changed-auth-key");
    perform_machine_test_batch(&queued);
    assert(calls.size() == 2 && calls[0].machine == 'A' && calls[1].machine == 'H');
    assert(calls[0].nonce != calls[1].nonce);
    assert(start_times[0] == 0 && start_times[1] - finish_times[0] == 2000);
    assert((pauses == std::vector<unsigned>{2000})); // no trailing pause
    for (const auto &call : calls) {
        assert(call.key == "synthetic-auth-key");
        assert(call.url.find("http://original-gateway/") == 0);
    }
    assert(status.find("A:OK H:OK") != std::string::npos);
    assert(saves == 0); // no fire counter, games, or credits persisted by tests

    reset(); assert(!lora_test_enabled_machines(0xff) && queues == 0 && saves == 0);
    reset(); assert(!lora_test_enabled_machines(0) && queues == 0);
    reset(); wifi = false; assert(!lora_test_enabled_machines(0x81) && queues == 0);
    reset(); s_request_busy = true; assert(!lora_test_enabled_machines(0x81) && saves == 0);
    reset(); g_store.gatewayToken[0] = '\0';
    assert(!lora_test_enabled_machines(0x81) && queues == 0);
    reset(); queueOK = false;
    assert(!lora_test_enabled_machines(0x81) && !s_request_busy && saves == 0);

    reset(); assert(lora_test_enabled_machines(0x81)); replies = {403};
    perform_machine_test_batch(&queued);
    assert(calls.size() == 1 && status.find("A:FEHLER H:NICHT GESENDET") != std::string::npos);
    assert(state == GATEWAY_AUTH_FAILED);
    assert(pauses.empty()); // stop on rejection, without a delayed subsequent shot
    reset(); assert(lora_test_enabled_machines(0x81)); replies = {409};
    perform_machine_test_batch(&queued);
    assert(calls.size() == 1 && status.find("A:BELEGT/KONFLIKT H:NICHT GESENDET") != std::string::npos);
    assert(state == GATEWAY_BUSY);
    assert(pauses.empty());
    reset(); assert(lora_test_enabled_machines(0x81)); replies = {202, 200};
    perform_machine_test_batch(&queued);
    assert(calls.size() == 2 && status.find("A:OHNE ACK H:OK") != std::string::npos);
    assert(status.find("TEST MIT WARNUNG") == 0);
    assert(start_times[1] - finish_times[0] == 2000); // missing ACK still gets a full pause

    reset(); for (int m = 0; m < 8; ++m) g_store.maschinenAktiv[m] = true;
    assert(lora_test_enabled_machines(0xff));
    perform_machine_test_batch(&queued);
    assert(calls.size() == 8 && pauses.size() == 7 && start_times[0] == 0);
    for (unsigned i = 1; i < calls.size(); ++i) {
        assert(calls[i].machine == (char)('A' + i));
        assert(start_times[i] - finish_times[i - 1] == 2000);
    }
    reset(); g_store.maschinenAktiv[7] = false;
    assert(lora_test_enabled_machines(0x01)); perform_machine_test_batch(&queued);
    assert(calls.size() == 1 && pauses.empty());

    reset(); lv_event_t event = {};
    machine_test_all_cb(&event);
    assert(s_machine_test_confirm && queues == 0 && saves == 0);
    assert(dialog_text.find("A H ") != std::string::npos);
    assert(dialog_text.find("2 Sekunden Pause nach der Gateway-Antwort") != std::string::npos);
    assert(dialog_text.find("ausgelöst") != std::string::npos && font_assignments == 4);
    machine_test_all_cancel_cb(&event);
    assert(!s_machine_test_confirm && queues == 0 && saves == 0);
    reset(); machine_test_all_cb(&event);
    event.user_data = (void *)(uintptr_t)0x81;
    machine_test_all_confirm_cb(&event);
    assert(!s_machine_test_confirm && s_machine_test_pending && queues == 1);
    reset(); machine_test_all_cb(&event);
    g_store.maschinenAktiv[7] = false; // selection changed while confirmation was open
    machine_test_all_confirm_cb(&event);
    assert(!s_machine_test_pending && queues == 0);
    reset(); machine_test_all_cb(&event); machine_test_all_confirm_cb(&event);
    assert(s_machine_test_pending && s_machine_test_bulk);
    refresh_machine_test_status();
    assert(displayed_text.find("TEST STARTET") == 0);
    during_pause = [] {
        set_status("Gateway erreichbar - Schlüssel akzeptiert."); // unrelated periodic status
        refresh_machine_test_status(); // used by sync-triggered Settings refresh
        assert(s_machine_test_pending && displayed_text == "TEST: A:OK ");
    };
    perform_machine_test_batch(&queued);
    set_status("Gateway erreichbar - Schlüssel akzeptiert."); // next health poll beats UI tick
    refresh_machine_test_status();
    assert(!s_machine_test_pending && displayed_text.find("TEST BEENDET: TEST: A:OK H:OK") == 0);
    assert(displayed_color == CLR_SUCCESS);
    const auto retained = displayed_text;
    refresh_machine_test_status(); // later sync / return to Settings must not reset it
    assert(displayed_text == retained);
    reset(); machine_test_all_cb(&event); machine_test_all_confirm_cb(&event);
    replies = {202, 200}; perform_machine_test_batch(&queued);
    set_status("Checking gateway...");
    refresh_machine_test_status();
    assert(!s_machine_test_pending && displayed_text.find("A:OHNE ACK H:OK") != std::string::npos);
    assert(displayed_color == CLR_DANGER);
    std::puts("PASS: enabled-only A-H selection; H once; fresh per-machine nonces; immutable credentials; no NVS writes");
    std::puts("PASS: rejection/no-machine/offline/busy/queue failure; stop on failure; per-machine ACK results");
    std::puts("PASS: confirmation never fires; cancel never fires; changed selection rejected");
    std::puts("PASS: full 2-second pause after each completed request, before next machine; no leading/trailing or post-error pause");
    std::puts("PASS: confirmation title, body and both button labels use accent-capable UI fonts");
    std::puts("PASS: sync refresh retains in-progress ACKs; health cannot overwrite final success/warnings; completion and report are atomic");
}
"""


if __name__ == "__main__":
    transport = (ROOT / "main/lora_stub/lora_stub.cpp").read_text()
    ui = (ROOT / "main/ui/screen_einstellungen.cpp").read_text()
    types = transport[transport.index("typedef enum : uint8_t"):
                      transport.index("static void set_status")]
    queue = r"""
static GatewayRequest queued;
bool prepare_fire_request(GatewayRequest *request, char *url, size_t size, char *mac, int *) {
    snprintf(request->nonce_hex, sizeof(request->nonce_hex), "%032x", ++nonce_counter);
    auth = {(char)('A' + request->machine), request->nonce_hex, request->gateway_token, ""};
    strcpy(mac, "fake-mac");
    return build_fire_url(url, size, request);
}
int xQueueSend(void *, const GatewayRequest *request, int) {
    if (!queueOK) return 0;
    queued = *request; ++queues; order.push_back("queue"); return pdTRUE;
}
"""
    transport_functions = "\n".join(function(transport, signature) for signature in [
        "static void publish_machine_test_status", "void lora_copy_machine_test_status",
        "static bool begin_request", "static bool copy_gateway_config",
        "static bool build_fire_url", "static void perform_machine_test_batch",
        "bool lora_test_enabled_machines"])
    ui_functions = "\n".join(function(ui, signature) for signature in [
        "static void refresh_machine_test_status",
        "static void machine_test_all_confirm_cb",
        "static void machine_test_all_cancel_cb", "static void machine_test_all_cb"])
    refresh = function(ui, "void screen_einstellungen_refresh")
    assert "s_machine_test_pending = false" not in refresh
    assert "EIN AUSLÖSEBEFEHL" not in refresh and "refresh_machine_test_status();" in refresh
    with tempfile.TemporaryDirectory(prefix="tm-machine-batch-") as directory:
        cpp = Path(directory) / "test.cpp"
        queue = queue.replace("static GatewayRequest queued;",
                              "static bool build_fire_url(char *, size_t, const GatewayRequest *);\nstatic GatewayRequest queued;")
        cpp.write_text(STUBS + types + queue + transport_functions +
                       UI_STUBS + ui_functions + MAIN)
        executable = Path(directory) / "test"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)