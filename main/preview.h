#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "pax_gfx.h"

// Decode a JPEG file from disk into a PSRAM RGB565 buffer.
// Returns ESP_OK on success; other codes indicate file-too-big,
// out-of-memory, or decode error. The decoded image stays cached
// inside this module until preview_unload() is called.
esp_err_t preview_load(const char *path);

// Render the cached image scaled-to-fit on `fb`, centred in the
// area between `top_y` (inclusive) and `bot_y` (exclusive),
// preserving aspect ratio.
void preview_draw(pax_buf_t *fb, int top_y, int bot_y);

// Free the cached decode.
void preview_unload(void);

// Return the path passed to preview_load(), or NULL.
const char *preview_path(void);

// True if the preview module is currently holding a decoded image.
bool preview_loaded(void);

// Return decoded image dimensions for diagnostic display.
void preview_dimensions(uint32_t *w, uint32_t *h);
