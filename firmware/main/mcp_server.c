#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcp_server.h"
#include "tools.h"
#include "animus_config.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "cJSON.h"
#include "sdkconfig.h"

/*
 * MCP over Streamable HTTP, running directly on the microcontroller.
 *
 * Spec notes (Streamable HTTP transport, revision 2025-11-25):
 *  - single endpoint (/mcp) accepting POST; GET without SSE support -> 405
 *  - JSON-RPC 2.0 payloads; notifications are answered with HTTP 202
 *  - the server assigns an Mcp-Session-Id on initialize (we generate one
 *    per boot; the device is otherwise stateless, which the upcoming
 *    2026-07-28 revision makes the default model)
 *  - Origin header is validated to block DNS-rebinding from browsers
 */

static const char *TAG = "animus.mcp";

#define MCP_LATEST "2025-11-25"
static const char *SUPPORTED_VERSIONS[] = {
    "2025-11-25",
    "2025-06-18",
    "2025-03-26",
};

#define BODY_MAX 16384

static char s_session[33];

/* ---- request guards -------------------------------------------------------- */

static bool auth_ok(httpd_req_t *req)
{
    if (CONFIG_ANIMUS_AUTH_TOKEN[0] == '\0') {
        return true; /* auth disabled */
    }

    char hdr[192] = { 0 };
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) !=
        ESP_OK) {
        return false;
    }

    char want[192];
    snprintf(want, sizeof(want), "Bearer %s", CONFIG_ANIMUS_AUTH_TOKEN);
    return strcmp(hdr, want) == 0;
}

static bool origin_ok(httpd_req_t *req)
{
    size_t len = httpd_req_get_hdr_value_len(req, "Origin");
    if (len == 0) {
        return true; /* CLI clients (Claude Code, curl) send no Origin */
    }

    char origin[128] = { 0 };
    if (len >= sizeof(origin) ||
        httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) !=
            ESP_OK) {
        return false;
    }

    /* Browser-based tools running locally (e.g. MCP Inspector) are fine;
     * anything else is a potential DNS-rebinding attack. */
    if (strncmp(origin, "http://localhost", 16) == 0 ||
        strncmp(origin, "http://127.", 11) == 0) {
        return true;
    }

    ESP_LOGW(TAG, "rejected Origin '%s' (DNS-rebinding protection)", origin);
    return false;
}

/* ---- JSON-RPC plumbing ------------------------------------------------------ */

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out == NULL) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_sendstr(req, out);
    cJSON_free(out);
    return r;
}

static cJSON *rpc_base(const cJSON *id)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    if (id != NULL) {
        cJSON_AddItemToObject(r, "id", cJSON_Duplicate(id, true));
    } else {
        cJSON_AddNullToObject(r, "id");
    }
    return r;
}

static esp_err_t send_rpc_error(httpd_req_t *req, const cJSON *id, int code,
                                const char *message)
{
    cJSON *r = rpc_base(id);
    cJSON *e = cJSON_AddObjectToObject(r, "error");
    cJSON_AddNumberToObject(e, "code", code);
    cJSON_AddStringToObject(e, "message", message);
    return send_json(req, r);
}

/* ---- MCP methods ------------------------------------------------------------ */

static cJSON *do_initialize(const cJSON *params)
{
    const char *ver = MCP_LATEST;
    const cJSON *pv = cJSON_GetObjectItemCaseSensitive(params,
                                                       "protocolVersion");
    if (cJSON_IsString(pv)) {
        for (size_t i = 0;
             i < sizeof(SUPPORTED_VERSIONS) / sizeof(SUPPORTED_VERSIONS[0]);
             i++) {
            if (strcmp(pv->valuestring, SUPPORTED_VERSIONS[i]) == 0) {
                ver = SUPPORTED_VERSIONS[i];
                break;
            }
        }
    }

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "protocolVersion", ver);

    cJSON *caps  = cJSON_AddObjectToObject(res, "capabilities");
    cJSON *tools = cJSON_AddObjectToObject(caps, "tools");
    cJSON_AddBoolToObject(tools, "listChanged", false);

    cJSON *si = cJSON_AddObjectToObject(res, "serverInfo");
    cJSON_AddStringToObject(si, "name", CONFIG_ANIMUS_DEVICE_NAME);
    cJSON_AddStringToObject(si, "title", "Animus");
    cJSON_AddStringToObject(si, "version", ANIMUS_VERSION);

    cJSON_AddStringToObject(
        res, "instructions",
        "This server is a physical microcontroller (Animus). Its firmware "
        "enforces a hard allow-list: only the pins named in the tool "
        "descriptions can be touched, outputs may auto-switch-off after a "
        "firmware time limit, and every actuation is logged. Before "
        "switching any output ON, briefly tell the user what you are about "
        "to actuate in the physical world.");

    return res;
}

static cJSON *do_tools_list(void)
{
    size_t count = 0;
    const animus_tool_t *ts = tools_all(&count);

    cJSON *res = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(res, "tools");
    for (size_t i = 0; i < count; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", ts[i].name);
        cJSON_AddStringToObject(t, "description", ts[i].description);
        cJSON_AddItemToObject(t, "inputSchema",
                              cJSON_CreateRaw(ts[i].input_schema));
        cJSON_AddItemToArray(arr, t);
    }
    return res;
}

static esp_err_t do_tools_call(httpd_req_t *req, const cJSON *id,
                               const cJSON *params)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
    if (!cJSON_IsString(name)) {
        return send_rpc_error(req, id, -32602, "Missing tool name");
    }

    const animus_tool_t *t = tools_find(name->valuestring);
    if (t == NULL) {
        return send_rpc_error(req, id, -32602, "Unknown tool");
    }

    const cJSON *args = cJSON_GetObjectItemCaseSensitive(params, "arguments");

    bool is_error = false;
    char *text = t->fn(args, &is_error);
    if (text == NULL) {
        return send_rpc_error(req, id, -32603, "Tool ran out of memory");
    }

    cJSON *r       = rpc_base(id);
    cJSON *res     = cJSON_AddObjectToObject(r, "result");
    cJSON *content = cJSON_AddArrayToObject(res, "content");
    cJSON *item    = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text);
    cJSON_AddItemToArray(content, item);
    cJSON_AddBoolToObject(res, "isError", is_error);

    free(text);
    return send_json(req, r);
}

/* ---- HTTP handlers ----------------------------------------------------------- */

static esp_err_t mcp_post(httpd_req_t *req)
{
    if (!origin_ok(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "Origin not allowed");
    }
    if (!auth_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        return httpd_resp_sendstr(req, "Unauthorized");
    }

    int total = req->content_len;
    if (total <= 0 || total > BODY_MAX) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_sendstr(req, "Body missing or too large");
    }

    char *buf = malloc((size_t)total + 1);
    if (buf == NULL) {
        return httpd_resp_send_500(req);
    }

    int got = 0, timeouts = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, (size_t)(total - got));
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) {
            continue;
        }
        if (r <= 0) {
            free(buf);
            return ESP_FAIL;
        }
        got += r;
        timeouts = 0;
    }
    buf[total] = '\0';

    cJSON *msg = cJSON_Parse(buf);
    free(buf);
    if (msg == NULL) {
        return send_rpc_error(req, NULL, -32700, "Parse error");
    }
    if (cJSON_IsArray(msg)) {
        esp_err_t e = send_rpc_error(req, NULL, -32600,
                                     "Batch requests are not supported");
        cJSON_Delete(msg);
        return e;
    }

    const cJSON *id     = cJSON_GetObjectItemCaseSensitive(msg, "id");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(msg, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(msg, "params");

    if (!cJSON_IsString(method)) {
        esp_err_t e = send_rpc_error(req, id, -32600, "Invalid Request");
        cJSON_Delete(msg);
        return e;
    }

    const char *m = method->valuestring;
    ESP_LOGI(TAG, "<- %s", m);

    if (id == NULL) {
        /* JSON-RPC notification (e.g. notifications/initialized):
         * accept and return no body, per Streamable HTTP transport. */
        httpd_resp_set_status(req, "202 Accepted");
        esp_err_t e = httpd_resp_send(req, NULL, 0);
        cJSON_Delete(msg);
        return e;
    }

    esp_err_t e;
    if (strcmp(m, "initialize") == 0) {
        httpd_resp_set_hdr(req, "Mcp-Session-Id", s_session);
        cJSON *r = rpc_base(id);
        cJSON_AddItemToObject(r, "result", do_initialize(params));
        e = send_json(req, r);
    } else if (strcmp(m, "ping") == 0) {
        cJSON *r = rpc_base(id);
        cJSON_AddItemToObject(r, "result", cJSON_CreateObject());
        e = send_json(req, r);
    } else if (strcmp(m, "tools/list") == 0) {
        cJSON *r = rpc_base(id);
        cJSON_AddItemToObject(r, "result", do_tools_list());
        e = send_json(req, r);
    } else if (strcmp(m, "tools/call") == 0) {
        e = do_tools_call(req, id, params);
    } else {
        e = send_rpc_error(req, id, -32601, "Method not found");
    }

    cJSON_Delete(msg);
    return e;
}

static esp_err_t mcp_get(httpd_req_t *req)
{
    /* We do not offer a server-initiated SSE stream. */
    httpd_resp_set_status(req, "405 Method Not Allowed");
    httpd_resp_set_hdr(req, "Allow", "POST, DELETE");
    return httpd_resp_sendstr(req, "Use POST for MCP messages");
}

static esp_err_t mcp_delete(httpd_req_t *req)
{
    /* Clients may terminate their session with DELETE. We are stateless,
     * so simply acknowledge. */
    return httpd_resp_sendstr(req, "session closed");
}

/* ---- startup ------------------------------------------------------------------ */

esp_err_t mcp_server_start(void)
{
    snprintf(s_session, sizeof(s_session), "%08lx%08lx%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());

    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size       = 8192;
    cfg.lru_purge_enable = true;

    httpd_handle_t srv = NULL;
    esp_err_t err = httpd_start(&srv, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t u_post = {
        .uri = "/mcp", .method = HTTP_POST, .handler = mcp_post
    };
    static const httpd_uri_t u_get = {
        .uri = "/mcp", .method = HTTP_GET, .handler = mcp_get
    };
    static const httpd_uri_t u_del = {
        .uri = "/mcp", .method = HTTP_DELETE, .handler = mcp_delete
    };
    httpd_register_uri_handler(srv, &u_post);
    httpd_register_uri_handler(srv, &u_get);
    httpd_register_uri_handler(srv, &u_del);

    ESP_LOGI(TAG, "MCP Streamable HTTP endpoint ready at /mcp (%s auth)",
             CONFIG_ANIMUS_AUTH_TOKEN[0] ? "bearer" : "no");
    return ESP_OK;
}
