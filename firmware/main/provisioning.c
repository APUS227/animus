#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "provisioning.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "dhcpserver/dhcpserver.h"
#include "sdkconfig.h"

static const char *TAG = "animus.prov";

#define NVS_NS  "animus"
#define AP_SSID "animus-setup"
#define AP_IP   "192.168.4.1"

/* ---- credential storage ---------------------------------------------------- */

bool provisioning_get_credentials(char *ssid, size_t ssid_len,
                                  char *pass, size_t pass_len)
{
    ssid[0] = '\0';
    pass[0] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        uint8_t force = 0;
        if (nvs_get_u8(h, "force_portal", &force) == ESP_OK && force) {
            nvs_erase_key(h, "force_portal");
            nvs_commit(h);
            nvs_close(h);
            ESP_LOGW(TAG, "setup portal was requested by a previous boot");
            return false;
        }

        size_t sl = ssid_len, pl = pass_len;
        esp_err_t e1 = nvs_get_str(h, "ssid", ssid, &sl);
        esp_err_t e2 = nvs_get_str(h, "pass", pass, &pl);
        nvs_close(h);

        if (e1 == ESP_OK && ssid[0] != '\0') {
            if (e2 != ESP_OK) {
                pass[0] = '\0';
            }
            ESP_LOGI(TAG, "using WiFi credentials from NVS (ssid: %s)", ssid);
            return true;
        }
    }

    /* Developer fallback from menuconfig. Empty by default. */
    if (strlen(CONFIG_ANIMUS_WIFI_SSID) > 0) {
        strlcpy(ssid, CONFIG_ANIMUS_WIFI_SSID, ssid_len);
        strlcpy(pass, CONFIG_ANIMUS_WIFI_PASSWORD, pass_len);
        ESP_LOGI(TAG, "using WiFi credentials from Kconfig (ssid: %s)", ssid);
        return true;
    }

    return false;
}

static esp_err_t save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_set_str(h, "ssid", ssid);
    if (e == ESP_OK) {
        e = nvs_set_str(h, "pass", pass);
    }
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e;
}

void provisioning_request_portal_and_reboot(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "force_portal", 1);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(TAG, "rebooting into the setup portal...");
    esp_restart();
}

/* ---- catch-all DNS server ---------------------------------------------------
 * Answers every A query with our own address. This is what makes phones
 * pop up the "sign in to network" page automatically. */

static void dns_task(void *arg)
{
    (void)arg;
    uint8_t buf[512];

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    ESP_LOGI(TAG, "captive DNS listening on :53");

    while (1) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&src, &slen);
        if (len < 12 || len + 16 > (int)sizeof(buf)) {
            continue;
        }

        /* Turn the query into a response: QR=1, AA=1, one answer that
         * points back at the query name and resolves to 192.168.4.1. */
        buf[2]  = 0x84;
        buf[3]  = 0x00;
        buf[6]  = 0x00; buf[7]  = 0x01; /* ANCOUNT = 1 */
        buf[8]  = 0x00; buf[9]  = 0x00; /* NSCOUNT = 0 */
        buf[10] = 0x00; buf[11] = 0x00; /* ARCOUNT = 0 */

        uint8_t *p = buf + len;
        *p++ = 0xC0; *p++ = 0x0C;             /* name: pointer to question */
        *p++ = 0x00; *p++ = 0x01;             /* type A */
        *p++ = 0x00; *p++ = 0x01;             /* class IN */
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 30; /* TTL 30 s */
        *p++ = 0x00; *p++ = 0x04;             /* RDLENGTH */
        *p++ = 192;  *p++ = 168; *p++ = 4; *p++ = 1;

        sendto(sock, buf, p - buf, 0, (struct sockaddr *)&src, slen);
    }
}

/* ---- config page ------------------------------------------------------------ */

static const char PAGE_HEAD[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Animus setup</title><style>"
    "body{font-family:system-ui;background:#111;color:#eee;max-width:26rem;"
    "margin:2rem auto;padding:0 1rem}h1{color:#e8a33d}"
    "input,button{width:100%;padding:.7rem;margin:.3rem 0 1rem;"
    "border-radius:.5rem;border:1px solid #444;background:#1c1c1c;color:#eee;"
    "font-size:1rem;box-sizing:border-box}"
    "button{background:#e8a33d;color:#111;font-weight:700;border:0;"
    "cursor:pointer}"
    ".n{padding:.5rem .7rem;border:1px solid #333;border-radius:.5rem;"
    "margin:.25rem 0;cursor:pointer}.n:hover{background:#222}"
    "small{color:#888}</style></head><body>"
    "<h1>Animus</h1><p>Connect this device to your WiFi network.</p>"
    "<form method=\"POST\" action=\"/save\">"
    "<label>Network name (SSID)</label>"
    "<input id=\"s\" name=\"ssid\" maxlength=\"32\" required>"
    "<label>Password</label>"
    "<input name=\"pass\" type=\"password\" maxlength=\"64\">"
    "<button>Save &amp; reboot</button></form>"
    "<p><small>Nearby networks (tap to fill):</small></p>";

static const char PAGE_TAIL[] =
    "<script>for(const e of document.querySelectorAll('.n'))"
    "e.onclick=()=>{document.getElementById('s').value=e.dataset.v;"
    "window.scrollTo(0,0)}</script></body></html>";

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);

    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
        uint16_t num = 12;
        wifi_ap_record_t recs[12];
        if (esp_wifi_scan_get_ap_records(&num, recs) == ESP_OK) {
            char line[160];
            for (int i = 0; i < num; i++) {
                const char *s = (const char *)recs[i].ssid;
                /* keep the HTML trivially safe: skip exotic SSIDs */
                if (s[0] == '\0' || strpbrk(s, "<>\"'&") != NULL) {
                    continue;
                }
                snprintf(line, sizeof(line),
                         "<div class=\"n\" data-v=\"%s\">%s "
                         "<small>(%d dBm)</small></div>",
                         s, s, recs[i].rssi);
                httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);
            }
        }
    }

    httpd_resp_send_chunk(req, PAGE_TAIL, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* ---- form parsing ------------------------------------------------------------ */

static void url_decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '+') {
            *o++ = ' ';
            s++;
        } else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], '\0' };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *o++ = *s++;
        }
    }
    *o = '\0';
}

static bool form_field(const char *body, const char *key, char *out,
                       size_t outlen)
{
    size_t klen = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == body || p[-1] == '&') && p[klen] == '=') {
            p += klen + 1;
            const char *e = strchr(p, '&');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n >= outlen) {
                n = outlen - 1;
            }
            memcpy(out, p, n);
            out[n] = '\0';
            url_decode(out);
            return true;
        }
        p += klen;
    }
    out[0] = '\0';
    return false;
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[256] = { 0 };
    int total = req->content_len;
    if (total <= 0 || total >= (int)sizeof(body)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Bad form data");
    }

    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, body + got, (size_t)(total - got));
        if (r <= 0) {
            return ESP_FAIL;
        }
        got += r;
    }

    char ssid[33], pass[65];
    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "pass", pass, sizeof(pass));

    if (ssid[0] == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "SSID is required");
    }

    if (save_credentials(ssid, pass) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "Failed to save credentials");
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!doctype html><body style=\"font-family:system-ui;background:#111;"
        "color:#eee;text-align:center;padding-top:3rem\">"
        "<h1 style=\"color:#e8a33d\">Saved!</h1>"
        "<p>Animus is rebooting and joining your network.</p>"
        "<p>You can close this page.</p></body>");

    ESP_LOGI(TAG, "credentials saved (ssid: %s) — rebooting", ssid);
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

/* Any other URL (connectivity probes like generate_204, hotspot-detect...)
 * gets redirected to the portal — this triggers the phone's popup. */
static esp_err_t redirect_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_IP "/");
    return httpd_resp_send(req, NULL, 0);
}

/* ---- portal bring-up ---------------------------------------------------------- */

void provisioning_start_portal(void)
{
    ESP_LOGI(TAG, "no WiFi credentials — starting setup portal");

    esp_netif_t *ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.ap.ssid, AP_SSID, sizeof(wc.ap.ssid));
    wc.ap.ssid_len       = strlen(AP_SSID);
    wc.ap.authmode       = WIFI_AUTH_OPEN;
    wc.ap.max_connection = 4;
    wc.ap.channel        = 1;

    /* APSTA so the config page can scan for nearby networks */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Tell DHCP clients that we are their DNS server. */
    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_str_to_ip4(AP_IP, &dns.ip.u_addr.ip4);
    esp_netif_dhcps_stop(ap);
    esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
    dhcps_offer_t offer_dns = OFFER_DNS;
    esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                           &offer_dns, sizeof(offer_dns));
    esp_netif_dhcps_start(ap);

    xTaskCreate(dns_task, "animus_dns", 3072, NULL, 5, NULL);

    httpd_config_t hc  = HTTPD_DEFAULT_CONFIG();
    hc.uri_match_fn    = httpd_uri_match_wildcard;
    hc.stack_size      = 8192;
    hc.lru_purge_enable = true;

    httpd_handle_t srv = NULL;
    ESP_ERROR_CHECK(httpd_start(&srv, &hc));

    static const httpd_uri_t u_root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get
    };
    static const httpd_uri_t u_save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_post
    };
    static const httpd_uri_t u_any = {
        .uri = "/*", .method = HTTP_GET, .handler = redirect_get
    };
    httpd_register_uri_handler(srv, &u_root);
    httpd_register_uri_handler(srv, &u_save);
    httpd_register_uri_handler(srv, &u_any);

    ESP_LOGI(TAG, "portal ready — join WiFi '%s', then open http://%s/",
             AP_SSID, AP_IP);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
