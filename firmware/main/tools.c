#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools.h"
#include "safety.h"
#include "wifi.h"
#include "animus_config.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#endif

static const char *TAG = "animus.tools";

/* ---- small helper: printf into a heap string ----------------------------- */

static char *xasprintf(const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    char *s = malloc((size_t)n + 1);
    if (s) {
        vsnprintf(s, (size_t)n + 1, fmt, ap2);
    }
    va_end(ap2);
    return s;
}

/* ---- tool: device_info ---------------------------------------------------- */

static char *tool_device_info(const cJSON *args, bool *is_error)
{
    (void)args;
    *is_error = false;
    return xasprintf(
        "Animus device '%s'\n"
        "Chip: %s | ESP-IDF: %s | Firmware: %s\n"
        "Uptime: %llu s | Free heap: %u bytes\n"
        "MCP endpoint: http://%s/mcp\n"
        "Allow-listed outputs: %u | Allow-listed inputs: %u",
        CONFIG_ANIMUS_DEVICE_NAME, CONFIG_IDF_TARGET, esp_get_idf_version(),
        ANIMUS_VERSION,
        (unsigned long long)(esp_timer_get_time() / 1000000ULL),
        (unsigned)esp_get_free_heap_size(), wifi_get_ip(),
        (unsigned)safety_output_count(), (unsigned)safety_input_count());
}

/* ---- tool: temperature_read ----------------------------------------------- */

static char *tool_temperature(const cJSON *args, bool *is_error)
{
    (void)args;
#if SOC_TEMP_SENSOR_SUPPORTED
    static temperature_sensor_handle_t s_temp = NULL;

    if (s_temp == NULL) {
        temperature_sensor_config_t c = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&c, &s_temp) != ESP_OK ||
            temperature_sensor_enable(s_temp) != ESP_OK) {
            s_temp = NULL;
            *is_error = true;
            return xasprintf("Internal temperature sensor failed to start.");
        }
    }

    float t = 0;
    if (temperature_sensor_get_celsius(s_temp, &t) != ESP_OK) {
        *is_error = true;
        return xasprintf("Failed to read the internal temperature sensor.");
    }

    *is_error = false;
    return xasprintf("Chip internal temperature: %.1f degC (silicon die "
                     "temperature, NOT ambient room temperature).",
                     t);
#else
    *is_error = true;
    return xasprintf("This chip has no internal temperature sensor.");
#endif
}

/* ---- tool: gpio_read ------------------------------------------------------- */

static char *tool_gpio_read(const cJSON *args, bool *is_error)
{
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(args, "pin");
    if (!cJSON_IsNumber(p)) {
        *is_error = true;
        return xasprintf("Invalid arguments: integer 'pin' is required.");
    }

    int pin = (int)cJSON_GetNumberValue(p);
    int level = 0;
    const char *name = NULL;

    if (safety_read_pin(pin, &level, &name) != ESP_OK) {
        *is_error = true;
        return xasprintf("REFUSED by firmware: GPIO %d is not on this "
                         "device's allow-list.",
                         pin);
    }

    *is_error = false;
    return xasprintf("Pin %d ('%s') reads level %d (%s).", pin, name, level,
                     level ? "HIGH" : "LOW");
}

/* ---- tool: gpio_set -------------------------------------------------------- */

static char *tool_gpio_set(const cJSON *args, bool *is_error)
{
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(args, "pin");
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(args, "state");
    if (!cJSON_IsNumber(p) || !cJSON_IsBool(s)) {
        *is_error = true;
        return xasprintf("Invalid arguments: integer 'pin' and boolean "
                         "'state' are required.");
    }

    char msg[224];
    esp_err_t err = safety_set_output((int)cJSON_GetNumberValue(p),
                                      cJSON_IsTrue(s), msg, sizeof(msg));
    *is_error = (err != ESP_OK);
    return xasprintf("%s", msg);
}

/* ---- registry -------------------------------------------------------------- */

static const char SCHEMA_EMPTY[] = "{\"type\":\"object\",\"properties\":{}}";

static const char SCHEMA_PIN[] =
    "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\","
    "\"description\":\"GPIO number from this device's allow-list\"}},"
    "\"required\":[\"pin\"]}";

static const char SCHEMA_PIN_STATE[] =
    "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\","
    "\"description\":\"GPIO number from this device's allow-list\"},"
    "\"state\":{\"type\":\"boolean\",\"description\":\"true = ON, "
    "false = OFF\"}},\"required\":[\"pin\",\"state\"]}";

/* Descriptions for the GPIO tools are built at boot so they list the
 * actual allow-list — the agent learns exactly (and only) what exists. */
static char s_desc_read[384];
static char s_desc_set[384];

static const animus_tool_t s_tools[] = {
    { "device_info",
      "Report identity, network address, uptime and safety configuration of "
      "this Animus device.",
      SCHEMA_EMPTY, tool_device_info },
    { "temperature_read",
      "Read the microcontroller's internal die temperature in Celsius "
      "(chip temperature, not ambient).",
      SCHEMA_EMPTY, tool_temperature },
    { "gpio_read", s_desc_read, SCHEMA_PIN, tool_gpio_read },
    { "gpio_set", s_desc_set, SCHEMA_PIN_STATE, tool_gpio_set },
};

#define TOOL_COUNT (sizeof(s_tools) / sizeof(s_tools[0]))

esp_err_t tools_init(void)
{
    int n = snprintf(s_desc_read, sizeof(s_desc_read),
                     "Read the level of an allow-listed pin. Available:");
    for (size_t i = 0; i < safety_input_count() && n > 0 &&
                       (size_t)n < sizeof(s_desc_read);
         i++) {
        int pin;
        const char *name;
        safety_input_info(i, &pin, &name);
        n += snprintf(s_desc_read + n, sizeof(s_desc_read) - (size_t)n,
                      " '%s' (input, pin %d);", name, pin);
    }
    for (size_t i = 0; i < safety_output_count() && n > 0 &&
                       (size_t)n < sizeof(s_desc_read);
         i++) {
        int pin;
        const char *name;
        uint32_t ms;
        safety_output_info(i, &pin, &name, &ms);
        n += snprintf(s_desc_read + n, sizeof(s_desc_read) - (size_t)n,
                      " '%s' (output, pin %d);", name, pin);
    }

    n = snprintf(s_desc_set, sizeof(s_desc_set),
                 "Switch an allow-listed output ON or OFF. The firmware "
                 "refuses any pin not listed here. Available outputs:");
    for (size_t i = 0; i < safety_output_count() && n > 0 &&
                       (size_t)n < sizeof(s_desc_set);
         i++) {
        int pin;
        const char *name;
        uint32_t ms;
        safety_output_info(i, &pin, &name, &ms);
        if (ms > 0) {
            n += snprintf(s_desc_set + n, sizeof(s_desc_set) - (size_t)n,
                          " '%s' (pin %d, firmware auto-off after %u ms);",
                          name, pin, (unsigned)ms);
        } else {
            n += snprintf(s_desc_set + n, sizeof(s_desc_set) - (size_t)n,
                          " '%s' (pin %d);", name, pin);
        }
    }

    ESP_LOGI(TAG, "%u tools registered", (unsigned)TOOL_COUNT);
    return ESP_OK;
}

const animus_tool_t *tools_all(size_t *count)
{
    *count = TOOL_COUNT;
    return s_tools;
}

const animus_tool_t *tools_find(const char *name)
{
    for (size_t i = 0; i < TOOL_COUNT; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            return &s_tools[i];
        }
    }
    return NULL;
}
