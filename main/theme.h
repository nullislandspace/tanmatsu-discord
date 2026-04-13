#pragma once

#include "pax_gfx.h"

// Central UI color palette. Every screen reads from this struct rather
// than hard-coding ARGB values, so swapping themes later is a one-liner.
typedef struct {
    const char *name;

    // Background layers
    pax_col_t bg;          // main app background
    pax_col_t header_bg;   // title/header bar background
    pax_col_t focus_bg;    // selected row / active element background

    // Text
    pax_col_t text;        // primary body text
    pax_col_t text_dim;    // secondary / hint text
    pax_col_t highlight;   // accent: titles, selected text, key values

    // Status
    pax_col_t good;        // success / connected
    pax_col_t bad;         // error / disconnected
    pax_col_t info;        // secondary accent (e.g., IDs)
} ui_theme_t;

// Currently active theme. Points to a theme_* in theme.c. Swap at runtime
// with ui_theme_set(); defaults to ui_theme_dark at boot.
extern const ui_theme_t *g_theme;

// Built-in themes
extern const ui_theme_t ui_theme_dark;

void ui_theme_set(const ui_theme_t *t);
