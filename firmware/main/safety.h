#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/*
 * The safety layer is the heart of Animus. It is the ONLY code path
 * allowed to touch GPIO, and it enforces the compiled-in allow-list
 * from animus_config.h. Nothing an LLM sends over the network can
 * widen these limits at runtime.
 */

esp_err_t safety_init(void);

size_t safety_output_count(void);
void   safety_output_info(size_t i, int *pin, const char **name, uint32_t *max_on_ms);

size_t safety_input_count(void);
void   safety_input_info(size_t i, int *pin, const char **name);

/* Switch an allow-listed output. Always fills `msg` with a human/agent
 * readable explanation. ESP_OK on success, ESP_ERR_NOT_FOUND if the pin
 * is not allow-listed. */
esp_err_t safety_set_output(int pin, bool on, char *msg, size_t msglen);

/* Read an allow-listed pin (input or output). */
esp_err_t safety_read_pin(int pin, int *level, const char **name);
