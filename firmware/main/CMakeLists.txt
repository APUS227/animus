idf_component_register(
    SRCS "main.c" "wifi.c" "safety.c" "tools.c" "mcp_server.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server json driver esp_timer esp_hw_support)
