#pragma once

#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "pax_gfx.h"

// Color constants shared across screens. RGB8888 on Tanmatsu.
#define UI_BLACK 0xFF000000u
#define UI_WHITE 0xFFFFFFFFu
#define UI_RED   0xFFFF0000u
#define UI_GREY  0xFF808080u
#define UI_BLUE  0xFF3060C0u

typedef enum {
    UI_SCREEN_CHANNEL_LIST = 0,
    UI_SCREEN_CHAT,
} ui_screen_t;

// Single entry point: owns the event loop. Never returns.
void ui_run(pax_buf_t *fb, const config_t *cfg, QueueHandle_t input_queue);
