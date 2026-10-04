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
#define MAX_URL_LEN 256
#define MAX_KEY_LEN 256
enum Maschine { MASCHINE_A, MASCHINE_B, MASCHINE_C, MASCHINE_D,
    MASCHINE_E, MASCHINE_F, MASCHINE_G, MASCHINE_H, MASCHINE_COUNT };
enum GatewayReachability { GATEWAY_REACHABLE, GATEWAY_AUTH_FAILED, GATEWAY_FAILED };
struct {
    bool maschinenAktiv[8];
    uint32_t gatewaySequence;
    char gatewayUrl[256], gatewayToken[256];
} g_store = {};
static bool s_request_busy = false;
static void *s_state_mutex = nullptr;
static void *s_gateway_queue = (void *)1;
static std::string status;
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
struct Call { char machine; uint32_t sequence; std::string key, url; };
static std::vector<Call> calls;
static std::vector<int> replies;
static Call auth;
namespace tm_auth {
    constexpr size_t MAC_LEN = 32, MAC_HEX_LEN = 64;
    bool make_request_mac(const uint8_t *key, size_t size, uint8_t machine,
                          uint32_t sequence, uint8_t *) {
        auth = {(char)machine, sequence, std::string((const char *)key, size), ""};
        return true;
    }
    void mac_to_hex(uint8_t *, char *hex) { strcpy(hex, "fake-mac"); }
}
bool perform_authenticated_get(const char *url, const char *, int *http, int, int) {
    auth.url = url; calls.push_back(auth);
    *http = calls.size() <= replies.size() ? replies[calls.size()-1] : 200;
    return *http >= 200 && *http < 300;
}
"""

UI_STUBS = r"""
struct lv_obj_t {};
struct lv_event_t { void *user_data; };
static lv_obj_t object, label;
static lv_obj_t *s_machine_test_confirm = nullptr;
static lv_obj_t *s_lbl_machine_test_status = &label;
static bool s_machine_test_pending = false;
static std::string dialog_text;
static int opened = 0, closed = 0;
constexpr int LV_EVENT_CLICKED = 1, CLR_WARN = 2, CLR_DANGER = 3;
void *lv_event_get_user_data(lv_event_t *event) { return event->user_data; }
void lv_msgbox_close(lv_obj_t *) { ++closed; }
lv_obj_t *lv_msgbox_create(void *) { ++opened; return &object; }
void lv_msgbox_add_title(lv_obj_t *, const char *) {}
void lv_msgbox_add_text(lv_obj_t *, const char *text) { dialog_text = text; }
lv_obj_t *lv_msgbox_add_footer_button(lv_obj_t *, const char *) { return &object; }
void lv_obj_add_event_cb(lv_obj_t *, void (*)(lv_event_t *), int, void *) {}
void lv_label_set_text(lv_obj_t *, const char *) {}
int lv_color_hex(int color) { return color; }
void lv_obj_set_style_text_color(lv_obj_t *, int, int) {}
"""

MAIN = r"""
void reset() {
    g_store = {}; g_store.maschinenAktiv[0] = g_store.maschinenAktiv[7] = true;
    g_store.gatewaySequence = 10;
    strcpy(g_store.gatewayUrl, "http://original-gateway");
    strcpy(g_store.gatewayToken, "synthetic-auth-key");
    wifi = queueOK = true; s_request_busy = false; saves = queues = 0;
    calls.clear(); replies.clear(); order.clear(); status.clear();
    s_machine_test_confirm = nullptr; s_machine_test_pending = false;
}
int main() {
    reset();
    assert(lora_test_enabled_machines(0x81));
    assert(s_request_busy && queues == 1 && saves == 1);
    assert(g_store.gatewaySequence == 12 && queued.sequence == 11);
    assert((order == std::vector<std::string>{"save", "queue"}));
    assert(queued.machine_mask == 0x81 && !queued.gameLaunch);
    strcpy(g_store.gatewayUrl, "http://changed-gateway");
    strcpy(g_store.gatewayToken, "changed-auth-key");
    perform_machine_test_batch(&queued);
    assert(calls.size() == 2 && calls[0].machine == 'A' && calls[1].machine == 'H');
    assert(calls[0].sequence == 11 && calls[1].sequence == 12);
    for (const auto &call : calls) {
        assert(call.key == "synthetic-auth-key");
        assert(call.url.find("http://original-gateway/") == 0);
    }
    assert(status.find("A:OK H:OK") != std::string::npos);
    assert(saves == 1); // worker never records games, credits, or further sequences

    reset(); assert(!lora_test_enabled_machines(0xff) && queues == 0 && saves == 0);
    reset(); assert(!lora_test_enabled_machines(0) && queues == 0);
    reset(); wifi = false; assert(!lora_test_enabled_machines(0x81) && queues == 0);
    reset(); s_request_busy = true; assert(!lora_test_enabled_machines(0x81) && saves == 0);
    reset(); g_store.gatewaySequence = UINT32_MAX - 1;
    assert(!lora_test_enabled_machines(0x81) && queues == 0 && saves == 0 && !s_request_busy);
    reset(); g_store.gatewayToken[0] = '\0';
    assert(!lora_test_enabled_machines(0x81) && queues == 0);
    reset(); queueOK = false;
    assert(!lora_test_enabled_machines(0x81) && !s_request_busy && g_store.gatewaySequence == 12);

    reset(); assert(lora_test_enabled_machines(0x81)); replies = {403};
    perform_machine_test_batch(&queued);
    assert(calls.size() == 1 && status.find("A:FEHLER H:NICHT GESENDET") != std::string::npos);
    assert(state == GATEWAY_AUTH_FAILED);
    reset(); assert(lora_test_enabled_machines(0x81)); replies = {202, 200};
    perform_machine_test_batch(&queued);
    assert(calls.size() == 2 && status.find("A:OHNE ACK H:OK") != std::string::npos);
    assert(status.find("TEST MIT WARNUNG") == 0);

    reset(); lv_event_t event = {};
    machine_test_all_cb(&event);
    assert(s_machine_test_confirm && queues == 0 && saves == 0);
    assert(dialog_text.find("A H ") != std::string::npos);
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
    std::puts("PASS: enabled-only A-H selection; H once; unique persisted sequences; immutable credentials");
    std::puts("PASS: rejection/no-machine/offline/busy/overflow/queue failure; stop on failure; per-machine ACK results");
    std::puts("PASS: confirmation never fires; cancel never fires; changed selection rejected");
}
"""


if __name__ == "__main__":
    transport = (ROOT / "main/lora_stub/lora_stub.cpp").read_text()
    ui = (ROOT / "main/ui/screen_einstellungen.cpp").read_text()
    types = transport[transport.index("typedef enum : uint8_t"):
                      transport.index("static void set_status")]
    queue = r"""
static GatewayRequest queued;
int xQueueSend(void *, const GatewayRequest *request, int) {
    if (!queueOK) return 0;
    queued = *request; ++queues; order.push_back("queue"); return pdTRUE;
}
"""
    transport_functions = "\n".join(function(transport, signature) for signature in [
        "static bool begin_request", "static bool copy_gateway_config",
        "static bool build_fire_url", "static void perform_machine_test_batch",
        "bool lora_test_enabled_machines"])
    ui_functions = "\n".join(function(ui, signature) for signature in [
        "static void machine_test_all_confirm_cb",
        "static void machine_test_all_cancel_cb", "static void machine_test_all_cb"])
    with tempfile.TemporaryDirectory(prefix="tm-machine-batch-") as directory:
        cpp = Path(directory) / "test.cpp"
        cpp.write_text(STUBS + types + queue + transport_functions +
                       UI_STUBS + ui_functions + MAIN)
        executable = Path(directory) / "test"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)