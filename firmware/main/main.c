#include "nvs_flash.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "animus_config.h"
#include "wifi.h"
#include "safety.h"
#include "tools.h"
#include "mcp_server.h"

static const char *TAG = "animus";

void app_main(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(r);
    }

    /* Order matters: pins go to their safe state before the network is up. */
    ESP_ERROR_CHECK(safety_init());
    ESP_ERROR_CHECK(tools_init());
    ESP_ERROR_CHECK(wifi_init_sta());
    ESP_ERROR_CHECK(mcp_server_start());

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " Animus %s is alive.", ANIMUS_VERSION);
    ESP_LOGI(TAG, " MCP endpoint : http://%s/mcp", wifi_get_ip());
    ESP_LOGI(TAG, " Connect from Claude Code:");
    ESP_LOGI(TAG, "   claude mcp add --transport http %s http://%s/mcp",
             CONFIG_ANIMUS_DEVICE_NAME, wifi_get_ip());
    ESP_LOGI(TAG, "==================================================");
}
