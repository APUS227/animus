#pragma once
#include "esp_err.h"

/* Connects to the given WiFi network. Blocks until an IP is obtained,
 * or returns ESP_FAIL after too many failed attempts (so the caller
 * can fall back to the setup portal). */
esp_err_t wifi_init_sta(const char *ssid, const char *pass);

/* Returns the current station IP as a string ("0.0.0.0" before connect). */
const char *wifi_get_ip(void);
