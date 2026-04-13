#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "pax_gfx.h"
#include "bsp/input.h"

typedef enum {
    FB_RESULT_NONE = 0,    // still browsing
    FB_RESULT_PICK,        // user picked a file → fb_picked_path()
    FB_RESULT_CANCEL,      // user pressed Esc at the root
} fb_result_t;

// Initialize/reset the browser to the given starting directory (e.g.
// "/sd/DCIM" or "/sd"). Falls back to "/sd" if the start dir can't be
// listed. Filter selects which file extensions show up; pass NULL for
// "no file filter" (directories always show).
void fb_open(const char *start_dir);

// Render the current directory listing using the active theme.
void fb_draw(pax_buf_t *fb, int panel_user_w, int panel_user_h);

// Process one BSP input event. Returns FB_RESULT_PICK once the user
// selects a file (read it via fb_picked_path), FB_RESULT_CANCEL when
// they Esc out of the root, or FB_RESULT_NONE while still browsing.
fb_result_t fb_handle_input(const bsp_input_event_t *ev);

// Heap-allocated absolute path of the most recently picked file.
// Caller frees. Returns NULL if no pick yet.
char *fb_picked_path(void);
