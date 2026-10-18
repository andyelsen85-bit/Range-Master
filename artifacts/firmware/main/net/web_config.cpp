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
#include "http_sync.h"
#include "storage_admin.h"
#include "cJSON.h"
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

// Escape dynamic HTML values without ever sending an empty HTTP chunk:
// esp_http_server interprets a zero-length chunk as the end of the response.
static esp_err_t send_html_value(httpd_req_t *req, const char *value)
{
    char buffer[128];
    size_t used = 0;
    for (const char *p = value; *p; ++p) {
        const char *entity = NULL;
        switch (*p) {
            case '&': entity = "&amp;"; break;
            case '\'': entity = "&#39;"; break;
            case '"': entity = "&quot;"; break;
            case '<': entity = "&lt;"; break;
            case '>': entity = "&gt;"; break;
            default: break;
        }
        size_t length = entity ? strlen(entity) : 1;
        if (used + length > sizeof(buffer)) {
            esp_err_t err = httpd_resp_send_chunk(req, buffer, used);
            if (err != ESP_OK) return err;
            used = 0;
        }
        if (entity) memcpy(buffer + used, entity, length);
        else buffer[used] = *p;
        used += length;
    }
    return used ? httpd_resp_send_chunk(req, buffer, used) : ESP_OK;
}

// ── HTML ──────────────────────────────────────────────────────
// Sent in chunks so CSS percentages don't need escaping.

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
    "hr{border:0;border-top:1px solid #3d4460;margin:1.5rem 0}"
    ".secondary{background:#334155;margin-top:.75rem}.secondary:hover{background:#475569}"
    ".status{color:#94a3b8;font-size:.82rem;line-height:1.5;margin-top:1rem}"
    "</style></head><body><div class=card>"
    "<h1>&#127919; TrapMaster</h1>"
    "<p class=sub>Terminalkonfiguration</p>";

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
    "<input name=gatewayUrl type=text value='";

static const char HTML_FORM_GATEWAY_KEY[] =
    "' placeholder='http://192.168.1.50'>"
    "<label>TrapMaster Gateway Key</label>"
    "<input name=gatewayKey type=password value='";

static const char HTML_FORM_END[] =
    "'>"
    "<button type=submit>&#128190;&nbsp; Speichern</button>"
    "</form>"
    "<hr>"
    "<form method=POST action=/backup><button class=secondary>Konfiguration jetzt sichern</button></form>"
    "<form method=POST action=/restore>"
    "<label style='margin-top:1.25rem'>Einmaliger Wiederherstellungscode</label>"
    "<input name=restoreCode type=text maxlength=12 pattern='[A-Fa-f0-9]{12}' autocomplete=off placeholder='ABCDEF123456' required>"
    "<button class=secondary>Backup wiederherstellen</button></form>"
    "<p class=status>";

static const char HTML_TAIL[] = "</p><p><a href='/pending'>Offene Spiele prüfen und als Datei sichern</a></p></div></body></html>";

// ── GET / — serve config form ─────────────────────────────────

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *banner = (const char *)req->user_ctx;  // NULL normally

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    httpd_resp_sendstr_chunk(req, HTML_HEAD);

    if (banner) httpd_resp_sendstr_chunk(req, banner);

    httpd_resp_sendstr_chunk(req, HTML_FORM_START);
    send_html_value(req, g_store.apiUrl);
    httpd_resp_sendstr_chunk(req, HTML_FORM_MID);
    send_html_value(req, g_store.apiKey);
    httpd_resp_sendstr_chunk(req, HTML_FORM_GATEWAY);
    send_html_value(req, g_store.gatewayUrl);
    httpd_resp_sendstr_chunk(req, HTML_FORM_GATEWAY_KEY);
    send_html_value(req, g_store.gatewayToken);
    httpd_resp_sendstr_chunk(req, HTML_FORM_END);
    send_html_value(req, g_store.configBackupStatus[0]
        ? g_store.configBackupStatus : "Noch kein Backup-Status.");
    if (g_store.lastConfigBackupAt[0]) {
        httpd_resp_sendstr_chunk(req, "<br>Letztes Backup: ");
        send_html_value(req, g_store.lastConfigBackupAt);
    }
    httpd_resp_sendstr_chunk(req, HTML_TAIL);

    httpd_resp_sendstr_chunk(req, NULL);  // end chunked response
    return ESP_OK;
}

static esp_err_t action_result(httpd_req_t *req, bool ok, const char *message)
{
    char page[640];
    snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta http-equiv=refresh content='3;url=/'>"
        "<style>body{background:#0f1117;color:#e2e8f0;font-family:system-ui;"
        "min-height:100vh;display:flex;align-items:center;justify-content:center}"
        ".box{background:%s;border:1px solid %s;color:%s;border-radius:12px;"
        "padding:2rem 3rem;text-align:center}p{color:#94a3b8}</style></head>"
        "<body><div class=box><strong>%s</strong><p>Zurück in 3 Sekunden...</p></div></body></html>",
        ok ? "#022c22" : "#2c0a0a", ok ? "#10b981" : "#ef4444",
        ok ? "#10b981" : "#ef4444", message);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t backup_post_handler(httpd_req_t *req)
{
    esp_err_t err = http_backup_config();
    return action_result(req, err == ESP_OK,
                          err == ESP_OK ? "Backup erfolgreich." : "Backup fehlgeschlagen.");
}

static esp_err_t restore_post_handler(httpd_req_t *req)
{
    int body_len = req->content_len;
    if (body_len <= 0 || body_len > 64) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }
    char body[65] = {}, code[16] = {};
    int received = httpd_req_recv(req, body, body_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_FAIL;
    }
    body[received] = '\0';
    extract_field(body, "restoreCode=", code, sizeof(code));
    esp_err_t err = http_restore_config(code);
    return action_result(req, err == ESP_OK,
                          err == ESP_OK ? "Wiederherstellung erfolgreich. Terminal neu starten."
                                        : "Wiederherstellung fehlgeschlagen. Code und Verbindung prüfen.");
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
        "&#10003;&nbsp; Gespeichert!<br>"
        "<p>Weiterleitung in 2 Sekunden...</p>"
        "</div></body></html>";

    httpd_resp_send(req, ok_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── URI table ─────────────────────────────────────────────────

static const char PENDING_PAGE[]=R"HTML(<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Offene Spiele sichern</title><style>body{background:#18211d;color:white;font:16px sans-serif;max-width:960px;margin:30px auto;padding:16px}button,input{padding:12px;margin:5px}article{border:1px solid #667;padding:16px;margin:16px 0;overflow-wrap:anywhere}pre{white-space:pre-wrap}.danger{background:#a22;color:white}</style>
<h1>Offene Spiele verwalten</h1><p>Speicher-Admin-PIN zuerst am Terminal unter Einstellungen → System festlegen. Dies ist nicht die Catering-PIN.</p>
<label>Speicher-Admin-PIN <input id="pin" type="password" inputmode="numeric" maxlength="16" autocomplete="off"></label>
<button id="refresh">Liste laden</button><button id="check">Mit Portal abgleichen</button><p id="status" role="status"></p><main id="games"></main>
<p>Erst wenn alle offenen Spiele geklärt sind: am Terminal „Speicher reparieren“ erneut öffnen. Journal, aktive Spiele und finanzielle Ereignisse werden niemals pauschal gelöscht.</p>
<script>
const status=document.querySelector('#status'),games=document.querySelector('#games');
async function request(path,body){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json','X-Storage-Pin':document.querySelector('#pin').value},body:JSON.stringify(body||{})});if(!r.ok)throw Error(await r.text());return r}
async function refresh(){try{const data=await(await request('/pending/list')).json();games.replaceChildren();for(const g of data.games){const article=document.createElement('article'),text=document.createElement('pre');text.textContent=(g.players.join(', ')||'Spielernamen nicht verfügbar')+'\n'+g.date+'\n'+g.externalId+'\n'+g.state+'\nPortal: '+g.portal;article.append(text);
const save=document.createElement('button');save.textContent='Als Datei sichern';save.onclick=async()=>{try{const r=await request('/pending/export',{id:g.externalId});const blob=await r.blob();const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download='Spiel-'+g.externalId+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),60000);status.textContent='Vollständige JSON-Sicherung übertragen. Prüfen Sie, dass die Datei wirklich gespeichert ist.'}catch(e){status.textContent=e.message}};
const discard=document.createElement('button');discard.textContent='Verwerfen (Datenverlust)';discard.className='danger';discard.onclick=async()=>{try{if(!confirm('ERSTE BESTÄTIGUNG: Spiel '+g.externalId+' nur verwerfen, wenn die vollständige JSON-Datei sicher gespeichert wurde. Dies löscht die lokale Wiederherstellung. Fortfahren?'))return;const prepare=await(await request('/pending/action',{action:'prepare',id:g.externalId})).json();const expected='VERWERFEN '+g.externalId;const entered=prompt('ZWEITE BESTÄTIGUNG: Datenverlust! Vollständige Sicherung geprüft? Geben Sie exakt ein: '+expected);if(entered!==expected)return;await request('/pending/action',{action:'discard',id:g.externalId,token:prepare.token,confirmation:entered});await refresh()}catch(e){status.textContent=e.message}};
article.append(save,discard);games.append(article)}status.textContent=data.games.length+' offene Spiele. Bei unbekanntem Portal-Status zuerst abgleichen. Ein leerer Spielbestand allein erlaubt keine Reparatur, wenn andere dauerhafte Ereignisse offen sind.'}catch(e){status.textContent=e.message}}
document.querySelector('#refresh').onclick=refresh;document.querySelector('#check').onclick=async()=>{try{status.textContent='Portal wird geprüft …';await request('/pending/action',{action:'reconcile'});await refresh()}catch(e){status.textContent=e.message}};
</script></html>)HTML";
static esp_err_t pending_page_handler(httpd_req_t *req) {
    httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,PENDING_PAGE,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t pending_api_handler(httpd_req_t *req) {
    char pin[17]{},body[1025]{};
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    if(httpd_req_get_hdr_value_str(req,"X-Storage-Pin",pin,sizeof(pin))!=ESP_OK)
        return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Speicher-Admin-PIN fehlt.");
    if(req->content_len<=0||req->content_len>1024)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültige Anfrage.");
    int received=0;
    while(received<req->content_len) {
        int n=httpd_req_recv(req,body+received,req->content_len-received);
        if(n<=0){memset(pin,0,sizeof(pin));return ESP_FAIL;}received+=n;
    }
    cJSON *input=cJSON_Parse(body);
    if(!input){memset(pin,0,sizeof(pin));return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültiges JSON.");}
    cJSON *id_item=cJSON_GetObjectItem(input,"id");const char *id=cJSON_IsString(id_item)?id_item->valuestring:"";
    bool valid_id=id[0]&&strlen(id)<40&&strspn(id,"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-")==strlen(id);
    esp_err_t result=ESP_FAIL;
    if(!strcmp(req->uri,"/pending/list")) {
        if(storage_admin_verify(pin)) {
            char *json=http_pending_games_json();
            if(json){httpd_resp_set_type(req,"application/json");result=httpd_resp_send(req,json,HTTPD_RESP_USE_STRLEN);cJSON_free(json);}
        }
    } else if(!strcmp(req->uri,"/pending/export")&&valid_id) {
        char *json=http_pending_game_export(id,pin);
        if(json) {
            cJSON *exported=cJSON_Parse(json),*hash=exported?cJSON_GetObjectItem(exported,"recordChecksum"):nullptr;
            char disposition[100];snprintf(disposition,sizeof(disposition),"attachment; filename=\"Spiel-%.39s.json\"",id);
            httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Content-Disposition",disposition);
            result=httpd_resp_send(req,json,HTTPD_RESP_USE_STRLEN);
            if(result==ESP_OK&&cJSON_IsNumber(hash))http_pending_game_export_delivered(id,(uint32_t)hash->valuedouble);
            cJSON_Delete(exported);cJSON_free(json);
        }
    } else if(!strcmp(req->uri,"/pending/action")) {
        cJSON *action=cJSON_GetObjectItem(input,"action");
        if(cJSON_IsString(action)&&!strcmp(action->valuestring,"reconcile")) {
            if(http_pending_games_reconcile(pin)==ESP_OK)result=httpd_resp_send(req,"{}",2);
        } else if(valid_id&&cJSON_IsString(action)&&!strcmp(action->valuestring,"prepare")) {
            char token[24]{};
            if(http_pending_game_prepare_discard(id,pin,token,sizeof(token))) {
                char reply[64];snprintf(reply,sizeof(reply),"{\"token\":\"%s\"}",token);
                result=httpd_resp_send(req,reply,HTTPD_RESP_USE_STRLEN);
            }
        } else if(valid_id&&cJSON_IsString(action)&&!strcmp(action->valuestring,"discard")) {
            cJSON *token=cJSON_GetObjectItem(input,"token"),*confirmation=cJSON_GetObjectItem(input,"confirmation");
            if(cJSON_IsString(token)&&cJSON_IsString(confirmation)&&
               http_pending_game_discard(id,pin,token->valuestring,confirmation->valuestring))
                result=httpd_resp_send(req,"{}",2);
        }
    }
    cJSON_Delete(input);memset(pin,0,sizeof(pin));
    if(result!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
        "Nicht freigegeben: PIN falsch/gesperrt, Sync läuft, Portal nicht erreichbar oder Spiel nicht exportiert/geändert. Erst vollständige Datei sichern; Bestätigung innerhalb 60 Sekunden.");
    return result;
}
static const httpd_uri_t uri_pending_page={.uri="/pending",.method=HTTP_GET,.handler=pending_page_handler,.user_ctx=nullptr};
static const httpd_uri_t uri_pending_list={.uri="/pending/list",.method=HTTP_POST,.handler=pending_api_handler,.user_ctx=nullptr};
static const httpd_uri_t uri_pending_export={.uri="/pending/export",.method=HTTP_POST,.handler=pending_api_handler,.user_ctx=nullptr};
static const httpd_uri_t uri_pending_action={.uri="/pending/action",.method=HTTP_POST,.handler=pending_api_handler,.user_ctx=nullptr};
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

static const httpd_uri_t uri_backup = {
    .uri = "/backup",
    .method = HTTP_POST,
    .handler = backup_post_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t uri_restore = {
    .uri = "/restore",
    .method = HTTP_POST,
    .handler = restore_post_handler,
    .user_ctx = NULL,
};

// ── Public API ────────────────────────────────────────────────

void web_config_start(void)
{
    if (s_server) return;  // already running

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.stack_size       = 6144;
    cfg.max_uri_handlers = 8;
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
    httpd_register_uri_handler(s_server, &uri_backup);
    httpd_register_uri_handler(s_server, &uri_restore);
    httpd_register_uri_handler(s_server,&uri_pending_page);
    httpd_register_uri_handler(s_server,&uri_pending_list);
    httpd_register_uri_handler(s_server,&uri_pending_export);
    httpd_register_uri_handler(s_server,&uri_pending_action);

    ESP_LOGI(TAG, "Config web server started — http://%s/", g_store.wifiIp);
}

void web_config_stop(void)
{
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "Config web server stopped");
}
