#pragma once

#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "pax_gfx.h"
#include "theme.h"

typedef enum {
    UI_SCREEN_CHANNEL_LIST = 0,
    UI_SCREEN_CHAT,
} ui_screen_t;

// Single entry point: owns the event loop. Never returns.
// panel_w/panel_h are the physical (pre-rotation) display dimensions
// returned by bsp_display_get_parameters, used by bsp_display_blit.
void ui_run(pax_buf_t *fb, size_t panel_w, size_t panel_h,
            const config_t *cfg, QueueHandle_t input_queue);
