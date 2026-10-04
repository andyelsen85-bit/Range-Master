#!/usr/bin/env python3
"""Actual terminal nonce fetch/sign/retry helpers, using host HTTP/SDK mocks."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "gateway_fixture", ROOT.parent / "lora-gateway/tests/test_nonce.py")
gateway = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gateway)

STUBS = r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "trapmaster_auth.h"
constexpr size_t MAX_URL_LEN = 256, MAX_KEY_LEN = 256;
enum Maschine { MASCHINE_A, MASCHINE_B, MASCHINE_C, MASCHINE_D,
    MASCHINE_E, MASCHINE_F, MASCHINE_G, MASCHINE_H, MASCHINE_COUNT };
enum GatewayReachability { GATEWAY_NOT_CONFIGURED, GATEWAY_CHECKING,
    GATEWAY_REACHABLE, GATEWAY_UNREACHABLE, GATEWAY_AUTH_FAILED, GATEWAY_FAILED, GATEWAY_BUSY };
static std::string status;
void set_status(const char *text) { status = text; }
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_TIMEOUT = -1, HTTP_EVENT_ON_DATA = 1;
struct esp_http_client_event_t { int event_id, data_len; void *data, *user_data; };
struct esp_http_client_config_t {
    const char *url;
    int timeout_ms;
    bool disable_auto_redirect;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
};
struct Client { esp_http_client_config_t config; int code; std::string mac; };
using esp_http_client_handle_t = Client *;
struct Reply { int err, code; std::string body; };
static std::vector<Reply> replies;
static std::vector<std::string> urls, macs;
static size_t calls = 0;
static unsigned delays = 0, issued = 0;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    assert(config->disable_auto_redirect); return new Client{*config, 0, ""};
}
void esp_http_client_set_header(Client *client, const char *name, const char *value) {
    assert(std::string(name) == "X-TrapMaster-Auth"); client->mac = value;
}
int esp_http_client_perform(Client *client) {
    urls.emplace_back(client->config.url); macs.push_back(client->mac);
    Reply reply = replies.at(calls++); client->code = reply.code;
    if (client->config.event_handler) {
        ++issued; // every attempt here is a nonce GET, never a fire GET
        // Deliberately deliver a fragmented response like real TCP.
        for (size_t start = 0; start < reply.body.size(); start += 7) {
            size_t count = reply.body.size() - start; if (count > 7) count = 7;
            esp_http_client_event_t event{HTTP_EVENT_ON_DATA, (int)count,
                (void *)(reply.body.data() + start), client->config.user_data};
            client->config.event_handler(&event);
        }
    }
    return reply.err;
}
int esp_http_client_get_status_code(Client *client) { return client->code; }
void esp_http_client_close(Client *) {}
void esp_http_client_cleanup(Client *client) { delete client; }
#define pdMS_TO_TICKS(value) (value)
void vTaskDelay(int ms) { delays += ms; }
// The real cJSON parser belongs to the SDK. These mocks expose the requested
// JSON nonce field; overflow, hex and response-status checks remain real code.
struct cJSON { char *valuestring; std::string value; };
cJSON *cJSON_Parse(const char *body) {
    std::string json = body; auto start = json.find("\"nonce\":\"");
    if (start == std::string::npos) return nullptr;
    start += 9; auto end = json.find('"', start);
    if (end == std::string::npos) return nullptr;
    auto *node = new cJSON{}; node->value = json.substr(start, end - start);
    node->valuestring = node->value.data(); return node;
}
cJSON *cJSON_GetObjectItemCaseSensitive(cJSON *node, const char *name) {
    assert(std::string(name) == "nonce"); return node;
}
bool cJSON_IsString(cJSON *node) { return node != nullptr; }
void cJSON_Delete(cJSON *node) { delete node; }
"""

MAIN = r"""
const char *nonce_body = "{\"nonce\":\"00112233445566778899aabbccddeeff\",\"expiresInMs\":10000}";
void reset() {
    replies.clear(); urls.clear(); macs.clear(); calls = delays = issued = 0; status.clear();
}
GatewayRequest request(bool pair = false) {
    GatewayRequest value = {};
    strcpy(value.gateway_url, "http://original-gateway/");
    strcpy(value.gateway_token, "synthetic-gateway-auth-key");
    value.kind = pair ? GATEWAY_REQUEST_FIRE_PAIR : GATEWAY_REQUEST_FIRE;
    value.machine = MASCHINE_D; value.second_machine = MASCHINE_F; value.delay_ms = 1000;
    return value;
}
int main() {
    assert(std::string(lora_gateway_state_label(GATEWAY_BUSY)) == "BELEGT / KONFLIKT");
    for (bool pair : {false, true}) {
        reset(); auto value = request(pair);
        replies = {{ESP_OK, 200, nonce_body}, {ESP_ERR_TIMEOUT, 0, ""},
                   {ESP_ERR_TIMEOUT, 0, ""}, {ESP_OK, 200, ""}};
        char url[MAX_URL_LEN + 96], mac[65]; int code = 0;
        assert(prepare_fire_request(&value, url, sizeof(url), mac, &code));
        assert(std::string(url).find("&nonce=00112233445566778899aabbccddeeff") != std::string::npos);
        assert(std::string(url).find("seq=") == std::string::npos);
        if (pair) assert(std::string(url).find("fire-pair?first=D&second=F&delayMs=1000") != std::string::npos);
        assert(perform_authenticated_get(url, mac, &code, pair ? 20000 : 7000, 3));
        assert(issued == 1 && calls == 4 && code == 200);
        assert(urls[0] == "http://original-gateway/nonce" && macs[0].empty());
        assert(urls[1] == urls[2] && urls[2] == urls[3]);
        assert(macs[1] == macs[2] && macs[2] == macs[3] && macs[1].size() == 64);
    }
    for (int rejection : {401, 409, 503}) {
        reset(); auto value = request(); char url[352], mac[65]; int code = 0;
        replies = {{ESP_OK, 200, nonce_body}, {ESP_OK, rejection, ""}, {ESP_OK, 200, ""}};
        assert(prepare_fire_request(&value, url, sizeof(url), mac, &code));
        assert(!perform_authenticated_get(url, mac, &code, 7000, 3));
        assert(calls == 2 && issued == 1 && code == rejection); // no retry, no new nonce
    }
    for (const std::string &body : {
        std::string("{\"nonce\":\"not-hex\"}"), std::string("{}"),
        std::string("{\"nonce\":\"00112233445566778899aabbccddeeff\"}") + std::string(160, ' ')}) {
        reset(); auto value = request(); char url[352], mac[65]; int code = 0;
        replies = {{ESP_OK, 200, body}};
        assert(!prepare_fire_request(&value, url, sizeof(url), mac, &code));
        assert(calls == 1 && issued == 1); // never sign/send a malformed nonce
    }
    reset(); auto value = request(); char url[352], mac[65]; int code = 0;
    replies = {{ESP_OK, 429, ""}, {ESP_OK, 200, nonce_body}};
    assert(prepare_fire_request(&value, url, sizeof(url), mac, &code));
    assert(calls == 2 && delays >= 20); // safe retry before any fire has been used
    reset(); value = request(); replies = {{ESP_OK, 404, ""}, {ESP_OK, 200, nonce_body}};
    assert(!prepare_fire_request(&value, url, sizeof(url), mac, &code));
    assert(calls == 1 && issued == 1); // old gateway: fail closed, no seq fallback
    reset(); value = request(); strcpy(value.gateway_url, "https://wrong-protocol/");
    assert(!prepare_fire_request(&value, url, sizeof(url), mac, &code) && calls == 0);
    std::puts("PASS: terminal fetches/signs fresh nonce for single/pair; fragmented responses and throttle retry");
    std::puts("PASS: fire timeout retries identical nonce/URL/HMAC; final 401/409/503 stop; no automatic new shot");
    std::puts("PASS: malformed/oversized nonce and old gateway rejected before fire; credentials snapshot retained");
}
"""

if __name__ == "__main__":
    source = (ROOT / "main/lora_stub/lora_stub.cpp").read_text()
    types = source[source.index("typedef enum : uint8_t"):source.index("static void set_status")]
    functions = "\n".join(gateway.function(source, name) for name in [
        "static bool build_fire_url", "static bool build_fire_pair_url",
        "static bool build_health_url", "static bool perform_authenticated_get",
        "const char *lora_gateway_state_label"])
    nonce_helpers = source[source.index("struct NonceResponse"):
                           source.index("struct HealthFailure")]
    nonce_helpers += "\n".join(gateway.function(source, name) for name in [
        "static bool fetch_gateway_nonce", "static bool prepare_fire_request"])
    assert "gatewaySequence" not in source and "game_store_save" not in source
    assert 'Gateway belegt oder Anfragekonflikt (HTTP 409).' in source
    for page in ("screen_spiel.cpp", "screen_dashboard.cpp"):
        ui = (ROOT / "main/ui" / page).read_text()
        assert "GATEWAY_BUSY) ? CLR_WARN" in ui and "lora_gateway_state_label" in ui
    with tempfile.TemporaryDirectory(prefix="tm-terminal-nonce-") as directory:
        path = Path(directory)
        (path / "mbedtls").mkdir()
        (path / "mbedtls/md.h").write_text(gateway.MD)
        (path / "test.cpp").write_text(STUBS + types + functions + nonce_helpers + MAIN)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", directory, "-I", str(ROOT.parent / "lora-common"),
                        str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)