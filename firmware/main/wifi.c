#include <stdio.h>
#include <string.h>

#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "animus.wifi";

#define GOT_IP_BIT BIT0
#define FAIL_BIT   BIT1

/* After this many consecutive failures we give up so the caller can
 * fall back to the setup portal (roughly 30-60 s). */
#define WIFI_MAX_RETRY 30

static EventGroupHandle_t s_evt;
static char s_ip[16] = "0.0.0.0";
static int  s_retry  = 0;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_evt, GOT_IP_BIT);
        if (++s_retry > WIFI_MAX_RETRY) {
            ESP_LOGE(TAG, "giving up after %d attempts", WIFI_MAX_RETRY);
            xEventGroupSetBits(s_evt, FAIL_BIT);
            return;
        }
        ESP_LOGW(TAG, "disconnected, retrying (%d/%d)...", s_retry,
                 WIFI_MAX_RETRY);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        ESP_LOGI(TAG, "got ip: %s", s_ip);
        xEventGroupSetBits(s_evt, GOT_IP_BIT);
    }
}

esp_err_t wifi_init_sta(const char *ssid, const char *pass)
{
    s_evt = xEventGroupCreate();

    /* esp_netif_init() and the default event loop are set up in main.c,
     * because the setup portal needs them too. */
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, NULL, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to \"%s\"...", ssid);
    EventBits_t bits = xEventGroupWaitBits(s_evt, GOT_IP_BIT | FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & GOT_IP_BIT) ? ESP_OK : ESP_FAIL;
}

const char *wifi_get_ip(void)
{
    return s_ip;
}
