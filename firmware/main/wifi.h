#pragma once
#include "esp_err.h"

/* Connects to the configured WiFi network. Blocks until an IP is obtained. */
esp_err_t wifi_init_sta(void);

/* Returns the current station IP as a string ("0.0.0.0" before connect). */
const char *wifi_get_ip(void);
