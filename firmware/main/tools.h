#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

/* A tool returns a heap-allocated text answer (caller frees) and sets
 * *is_error when the call was refused or failed. */
typedef char *(*animus_tool_fn)(const cJSON *args, bool *is_error);

typedef struct {
    const char *name;
    const char *description;   /* shown to the model */
    const char *input_schema;  /* static JSON Schema string */
    animus_tool_fn fn;
} animus_tool_t;

esp_err_t tools_init(void);
const animus_tool_t *tools_all(size_t *count);
const animus_tool_t *tools_find(const char *name);
