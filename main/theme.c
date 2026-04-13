#include "theme.h"

// Paperclips-grace-inspired dark palette. Navy background, amber accent.
const ui_theme_t ui_theme_dark = {
    .name      = "dark",
    .bg        = 0xFF1A1A2Eu,
    .header_bg = 0xFF16213Eu,
    .focus_bg  = 0xFF2A2A4Eu,
    .text      = 0xFFE0E0E0u,
    .text_dim  = 0xFF808090u,
    .highlight = 0xFFFFC107u,
    .good      = 0xFF4CAF50u,
    .bad       = 0xFFF44336u,
    .info      = 0xFF00BCD4u,
};

const ui_theme_t *g_theme = &ui_theme_dark;

void ui_theme_set(const ui_theme_t *t) {
    if (t) g_theme = t;
}
