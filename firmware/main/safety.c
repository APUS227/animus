#include <stdio.h>
#include <string.h>

#include "safety.h"

#define ANIMUS_CONFIG_TABLES /* this file owns the allow-list tables */
#include "animus_config.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

/*
 * SAFETY LAYER
 * ------------
 * Design rule: this is the only translation unit that includes
 * animus_config.h and the only one that calls gpio_* functions.
 * Everything an agent can do to the physical world funnels through
 * safety_set_output() / safety_read_pin(), which enforce:
 *
 *   1. the compiled-in allow-list (unknown pin -> hard refusal),
 *   2. per-output max-on time (esp_timer forces the pin back to its
 *      inactive level even if the OFF command never arrives),
 *   3. an audit log of every actuation and refusal.
 */

static const char *TAG   = "animus.safety";
static const char *AUDIT = "animus.audit";

static esp_timer_handle_t s_off_timer[ANIMUS_OUTPUT_COUNT];

static int out_index(int pin)
{
    for (size_t i = 0; i < ANIMUS_OUTPUT_COUNT; i++) {
        if (ANIMUS_OUTPUTS[i].pin == pin) {
            return (int)i;
        }
    }
    return -1;
}

static int in_index(int pin)
{
    for (size_t i = 0; i < ANIMUS_INPUT_COUNT; i++) {
        if (ANIMUS_INPUTS[i].pin == pin) {
            return (int)i;
        }
    }
    return -1;
}

static void auto_off_cb(void *arg)
{
    size_t i = (size_t)(uintptr_t)arg;
    const animus_output_t *o = &ANIMUS_OUTPUTS[i];
    gpio_set_level(o->pin, !o->active_level);
    ESP_LOGW(AUDIT,
             "AUTO-OFF: output '%s' (pin %d) hit max_on_ms=%u and was forced "
             "off by firmware",
             o->name, o->pin, (unsigned)o->max_on_ms);
}

esp_err_t safety_init(void)
{
    for (size_t i = 0; i < ANIMUS_OUTPUT_COUNT; i++) {
        const animus_output_t *o = &ANIMUS_OUTPUTS[i];

        gpio_config_t c = {
            .pin_bit_mask = 1ULL << o->pin,
            /* INPUT_OUTPUT so the current state can be read back */
            .mode         = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&c));
        gpio_set_level(o->pin, !o->active_level); /* boot in the safe state */

        const esp_timer_create_args_t ta = {
            .callback = auto_off_cb,
            .arg      = (void *)(uintptr_t)i,
            .name     = "animus_off",
        };
        ESP_ERROR_CHECK(esp_timer_create(&ta, &s_off_timer[i]));

        ESP_LOGI(TAG, "output allow-listed: '%s' pin=%d max_on_ms=%u",
                 o->name, o->pin, (unsigned)o->max_on_ms);
    }

    for (size_t i = 0; i < ANIMUS_INPUT_COUNT; i++) {
        const animus_input_t *in = &ANIMUS_INPUTS[i];

        gpio_config_t c = {
            .pin_bit_mask = 1ULL << in->pin,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = (in->pull == 1) ? GPIO_PULLUP_ENABLE
                                            : GPIO_PULLUP_DISABLE,
            .pull_down_en = (in->pull == 2) ? GPIO_PULLDOWN_ENABLE
                                            : GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&c));

        ESP_LOGI(TAG, "input allow-listed: '%s' pin=%d", in->name, in->pin);
    }

    return ESP_OK;
}

size_t safety_output_count(void)
{
    return ANIMUS_OUTPUT_COUNT;
}

void safety_output_info(size_t i, int *pin, const char **name,
                        uint32_t *max_on_ms)
{
    if (i >= ANIMUS_OUTPUT_COUNT) {
        return;
    }
    if (pin)       *pin       = ANIMUS_OUTPUTS[i].pin;
    if (name)      *name      = ANIMUS_OUTPUTS[i].name;
    if (max_on_ms) *max_on_ms = ANIMUS_OUTPUTS[i].max_on_ms;
}

size_t safety_input_count(void)
{
    return ANIMUS_INPUT_COUNT;
}

void safety_input_info(size_t i, int *pin, const char **name)
{
    if (i >= ANIMUS_INPUT_COUNT) {
        return;
    }
    if (pin)  *pin  = ANIMUS_INPUTS[i].pin;
    if (name) *name = ANIMUS_INPUTS[i].name;
}

esp_err_t safety_set_output(int pin, bool on, char *msg, size_t msglen)
{
    int i = out_index(pin);
    if (i < 0) {
        snprintf(msg, msglen,
                 "REFUSED by firmware: GPIO %d is not on this device's "
                 "compiled-in output allow-list. Edit animus_config.h and "
                 "reflash to change what may be actuated.",
                 pin);
        ESP_LOGW(AUDIT, "DENY set pin=%d on=%d (not allow-listed)", pin,
                 (int)on);
        return ESP_ERR_NOT_FOUND;
    }

    const animus_output_t *o = &ANIMUS_OUTPUTS[i];

    esp_timer_stop(s_off_timer[i]); /* fine if it was not running */
    gpio_set_level(o->pin, on ? o->active_level : !o->active_level);

    if (on && o->max_on_ms > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(s_off_timer[i],
                                             (uint64_t)o->max_on_ms * 1000ULL));
        snprintf(msg, msglen,
                 "OK: output '%s' (pin %d) is now ON. Firmware safety limit: "
                 "it will auto-switch OFF after %.1f s unless switched off "
                 "earlier.",
                 o->name, o->pin, o->max_on_ms / 1000.0);
    } else if (on) {
        snprintf(msg, msglen,
                 "OK: output '%s' (pin %d) is now ON (no auto-off configured).",
                 o->name, o->pin);
    } else {
        snprintf(msg, msglen, "OK: output '%s' (pin %d) is now OFF.", o->name,
                 o->pin);
    }

    ESP_LOGI(AUDIT, "SET output '%s' pin=%d -> %s", o->name, o->pin,
             on ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t safety_read_pin(int pin, int *level, const char **name)
{
    int i = in_index(pin);
    if (i >= 0) {
        *name  = ANIMUS_INPUTS[i].name;
        *level = gpio_get_level(pin);
        ESP_LOGD(AUDIT, "READ input '%s' pin=%d level=%d", *name, pin, *level);
        return ESP_OK;
    }

    i = out_index(pin);
    if (i >= 0) {
        *name  = ANIMUS_OUTPUTS[i].name;
        *level = gpio_get_level(pin);
        ESP_LOGD(AUDIT, "READ output '%s' pin=%d level=%d", *name, pin,
                 *level);
        return ESP_OK;
    }

    ESP_LOGW(AUDIT, "DENY read pin=%d (not allow-listed)", pin);
    return ESP_ERR_NOT_FOUND;
}
