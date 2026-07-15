#pragma once
#include <stdint.h>

/*
 * ============================================================================
 *  ANIMUS DEVICE CONFIGURATION — the hardware allow-list
 * ============================================================================
 *
 *  This file is the single source of truth for what an AI agent may touch
 *  on this device. It is compiled into the firmware: nothing sent over the
 *  network can add pins, remove limits or change polarity at runtime.
 *
 *  To change what your device exposes: edit the tables below and reflash.
 *  (v0.3 will generate this file from a YAML description — see ROADMAP.md.)
 */

#define ANIMUS_VERSION "0.1.0"

/* ---- Types ----------------------------------------------------------------- */

typedef struct {
    int         pin;           /* GPIO number */
    const char *name;          /* human/agent readable name */
    uint32_t    max_on_ms;     /* firmware auto-off after this many ms.
                                  0 = no limit (discouraged for anything
                                  that moves, heats or pumps). */
    int         active_level;  /* 1 = active-high, 0 = active-low */
} animus_output_t;

typedef struct {
    int         pin;
    const char *name;
    int         pull;          /* 0 = floating, 1 = pull-up, 2 = pull-down */
} animus_input_t;

/*
 * The tables below are instantiated ONLY inside safety.c (which defines
 * ANIMUS_CONFIG_TABLES before including this header). That keeps the
 * safety layer the single owner of the hardware allow-list.
 */
#ifdef ANIMUS_CONFIG_TABLES

/* ---- Outputs (things the agent may actuate) ------------------------------ */

static const animus_output_t ANIMUS_OUTPUTS[] = {
    /* Demo: wire an LED (with resistor) or a relay module to GPIO 21.
     * The firmware will force it OFF after 10 s no matter what. */
    { .pin = 21, .name = "demo_led", .max_on_ms = 10000, .active_level = 1 },
};

/* ---- Inputs (things the agent may read) ---------------------------------- */

static const animus_input_t ANIMUS_INPUTS[] = {
    /* Demo: the BOOT button present on virtually every ESP32 dev board.
     * Pressed = level 0 (it pulls GPIO0 to ground). */
    { .pin = 0, .name = "boot_button", .pull = 1 },
};

#define ANIMUS_OUTPUT_COUNT (sizeof(ANIMUS_OUTPUTS) / sizeof(ANIMUS_OUTPUTS[0]))
#define ANIMUS_INPUT_COUNT  (sizeof(ANIMUS_INPUTS)  / sizeof(ANIMUS_INPUTS[0]))

#endif /* ANIMUS_CONFIG_TABLES */
