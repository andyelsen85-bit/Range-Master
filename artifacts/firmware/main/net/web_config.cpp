// ============================================================
// web_config — tiny HTTP config server (esp_http_server)
//
// Serves http://<terminal-ip>/ — a small dark-themed form
// that lets an operator set the API URL/key and gateway URL/key from any
// browser on the local network.  No keyboard on the terminal
// required.
//
// Lifecycle:
//   web_config_start() — called from coprocessor IP_EVENT handler
//   web_config_stop()  — called on WiFi disconnect
// ============================================================
#include "web_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "game_store.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "web_config";
static httpd_handle_t s_server = NULL;

// ── Helpers ───────────────────────────────────────────────────

// Decode a single hex nibble; returns -1 on bad input.
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// URL-decode src into dst (max_len including NUL).
static void url_decode(char *dst, const char *src, size_t max_len)
{
    size_t out = 0;
    while (*src && out + 1 < max_len) {
        if (*src == '+') {
            dst[out++] = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            int hi = hex_val(src[1]);
            int lo = hex_val(src[2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                src += 3;
            } else {
                dst[out++] = *src++;
            }
        } else {
            dst[out++] = *src++;
        }
    }
    dst[out] = '\0';
}

// Extract value for key= from a URL-encoded body into dst.
// Returns true if the key was found.
static bool extract_field(const char *body, const char *key,
                           char *dst, size_t dst_len)
{
    const char *p = strstr(body, key);
    if (!p) { dst[0] = '\0'; return false; }
    p += strlen(key);  // points to value (URL-encoded)
    // value ends at '&' or end of string
    const char *end = strchr(p, '&');
    size_t val_len = end ? (size_t)(end - p) : strlen(p);
    // copy raw into temp buffer
    char tmp[512] = {};
    if (val_len >= sizeof(tmp)) val_len = sizeof(tmp) - 1;
    memcpy(tmp, p, val_len);
    tmp[val_len] = '\0';
    url_decode(dst, tmp, dst_len);
    return true;
}

// ── HTML ──────────────────────────────────────────────────────
// Sent in three chunks so CSS percentages don't need escaping.

static const char HTML_HEAD[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>TrapMaster Config</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:#0f1117;color:#e2e8f0;font-family:system-ui,sans-serif;"
    "min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1rem}"
    ".card{background:#1e2130;border-radius:12px;padding:2rem;width:100%;max-width:480px;"
    "box-shadow:0 4px 32px #0006}"
    "h1{color:#3b82f6;font-size:1.35rem;margin-bottom:.25rem}"
    ".sub{color:#64748b;font-size:.85rem;margin-bottom:1.5rem}"
    "label{display:block;color:#94a3b8;font-size:.75rem;text-transform:uppercase;"
    "letter-spacing:.06em;margin-bottom:.35rem}"
    "input{width:100%;background:#2d3347;border:1px solid #3d4460;border-radius:8px;"
    "color:#e2e8f0;padding:.72rem 1rem;font-size:.95rem;margin-bottom:1.25rem;"
    "outline:none;transition:border .15s}"
    "input:focus{border-color:#3b82f6}"
    "button{width:100%;background:#3b82f6;color:#fff;border:none;border-radius:8px;"
    "padding:.88rem;font-size:1rem;font-weight:600;cursor:pointer;transition:background .15s}"
    "button:hover{background:#2563eb}"
    ".ok{background:#022c22;border:1px solid #10b981;color:#10b981;border-radius:8px;"
    "padding:.7rem 1rem;margin-bottom:1rem;font-size:.9rem}"
    ".err{background:#2c0a0a;border:1px solid #ef4444;color:#ef4444;border-radius:8px;"
    "padding:.7rem 1rem;margin-bottom:1rem;font-size:.9rem}"
    "</style></head><body><div class=card>"
    "<h1>&#127919; TrapMaster</h1>"
    "<p class=sub>Terminal-Konfiguration</p>";

static const char HTML_FORM_START[] =
    "<form method=POST action=/save>"
    "<label>API URL</label>"
    "<input name=apiUrl type=text value='";

static const char HTML_FORM_MID[] =
    "' placeholder='https://...' required>"
    "<label>API Key</label>"
    "<input name=apiKey type=password value='";

static const char HTML_FORM_GATEWAY[] =
    "'>"
    "<label>TrapMaster Gateway URL</label>"
    "<input name=gatewayUrl type=text value='"
    "' placeholder='http://192.168.1.50'>"
    "<label>TrapMaster Gateway Key</label>"
    "<input name=gatewayKey type=password value='";

static const char HTML_FORM_END[] =
    "'>"
    "<button>&#128190;&nbsp; Speichern</button>"
    "</form>"
    "</div></body></html>";

// ── GET / — serve config form ─────────────────────────────────

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *banner = (const char *)req->user_ctx;  // NULL normally

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    httpd_resp_sendstr_chunk(req, HTML_HEAD);

    if (banner) httpd_resp_sendstr_chunk(req, banner);

    httpd_resp_sendstr_chunk(req, HTML_FORM_START);
    httpd_resp_sendstr_chunk(req, g_store.apiUrl);
    httpd_resp_sendstr_chunk(req, HTML_FORM_MID);
    httpd_resp_sendstr_chunk(req, g_store.apiKey);
    httpd_resp_sendstr_chunk(req, HTML_FORM_GATEWAY);
    httpd_resp_sendstr_chunk(req, g_store.gatewayUrl);
    httpd_resp_sendstr_chunk(req, HTML_FORM_END);

    httpd_resp_sendstr_chunk(req, NULL);  // end chunked response
    return ESP_OK;
}

// ── POST /save — apply new config ────────────────────────────

static esp_err_t save_post_handler(httpd_req_t *req)
{
    // Read body (URL-encoded form data)
    int body_len = req->content_len;
    if (body_len <= 0 || body_len > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }

    char body[512 + 1] = {};
    int received = httpd_req_recv(req, body, body_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_FAIL;
    }
    body[received] = '\0';
    char new_url[MAX_URL_LEN] = {};
    char new_key[MAX_KEY_LEN] = {};
    char new_gateway_url[MAX_URL_LEN] = {};
    char new_gateway_key[MAX_KEY_LEN] = {};
    extract_field(body, "apiUrl=", new_url, sizeof(new_url));
    extract_field(body, "apiKey=", new_key, sizeof(new_key));
    extract_field(body, "gatewayUrl=", new_gateway_url, sizeof(new_gateway_url));
    extract_field(body, "gatewayKey=", new_gateway_key, sizeof(new_gateway_key));

    bool changed = false;
    if (new_url[0] != '\0' &&
        strncmp(new_url, g_store.apiUrl, MAX_URL_LEN) != 0) {
        strncpy(g_store.apiUrl, new_url, MAX_URL_LEN - 1);
        g_store.apiUrl[MAX_URL_LEN - 1] = '\0';
        changed = true;
        ESP_LOGI(TAG, "API URL updated: %s", g_store.apiUrl);
    }
    // Always accept key (even if unchanged — password fields may send the
    // same value and we want to allow explicit clears too).
    if (strncmp(new_key, g_store.apiKey, MAX_KEY_LEN) != 0) {
        strncpy(g_store.apiKey, new_key, MAX_KEY_LEN - 1);
        g_store.apiKey[MAX_KEY_LEN - 1] = '\0';
        changed = true;
        ESP_LOGI(TAG, "API Key updated (length %d)", (int)strlen(g_store.apiKey));
    }
    if (strncmp(new_gateway_url, g_store.gatewayUrl, MAX_URL_LEN) != 0) {
        strncpy(g_store.gatewayUrl, new_gateway_url, MAX_URL_LEN - 1);
        g_store.gatewayUrl[MAX_URL_LEN - 1] = '\0';
        changed = true;
        ESP_LOGI(TAG, "Gateway URL updated: %s", g_store.gatewayUrl);
    }
    if (strncmp(new_gateway_key, g_store.gatewayToken, MAX_KEY_LEN) != 0) {
        strncpy(g_store.gatewayToken, new_gateway_key, MAX_KEY_LEN - 1);
        g_store.gatewayToken[MAX_KEY_LEN - 1] = '\0';
        changed = true;
        ESP_LOGI(TAG, "Gateway key updated (length %d)",
                 (int)strlen(g_store.gatewayToken));
    }
    if (changed) game_store_save();

    // Redirect back to GET / with a success banner injected via query param.
    // We use a meta-refresh trick so the banner is visible before the page
    // auto-reloads — simpler than a JS solution.
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    const char *ok_page =
        "<!DOCTYPE html><html><head>"
        "<meta charset=utf-8>"
        "<meta http-equiv=refresh content='2;url=/'>"
        "<style>"
        "body{background:#0f1117;color:#e2e8f0;font-family:system-ui,sans-serif;"
        "min-height:100vh;display:flex;align-items:center;justify-content:center}"
        ".box{background:#022c22;border:1px solid #10b981;color:#10b981;"
        "border-radius:12px;padding:2rem 3rem;font-size:1.1rem;text-align:center}"
        "p{color:#64748b;margin-top:.5rem;font-size:.85rem}"
        "</style></head><body>"
        "<div class=box>"
        "&#10003;&nbsp; Gespichert!<br>"
        "<p>Leitung zreeck an 2 Sekonnen...</p>"
        "</div></body></html>";

    httpd_resp_send(req, ok_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── URI table ─────────────────────────────────────────────────

static const httpd_uri_t uri_root = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = root_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t uri_save = {
    .uri      = "/save",
    .method   = HTTP_POST,
    .handler  = save_post_handler,
    .user_ctx = NULL,
};

// ── Public API ────────────────────────────────────────────────

void web_config_start(void)
{
    if (s_server) return;  // already running

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.stack_size       = 6144;
    cfg.max_uri_handlers = 4;
    cfg.max_open_sockets  = 1;     // config page: one client at a time is enough
    // max_req_hdr_len is CONFIG_HTTPD_MAX_REQ_HDR_LEN in sdkconfig.defaults (not a runtime field)

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return;
    }

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_save);

    ESP_LOGI(TAG, "Config web server started — http://%s/", g_store.wifiIp);
}

void web_config_stop(void)
{
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "Config web server stopped");
}
