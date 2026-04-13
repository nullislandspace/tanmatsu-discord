#pragma once

#include <stdbool.h>
#include "pax_gfx.h"

typedef enum {
    ICON_ESC = 0,
    ICON_F1,
    ICON_F2,
    ICON_F3,
    ICON_F4,
    ICON_F5,
    ICON_F6,
    ICON_KEY_COUNT,
} icon_key_t;

// Load F1-F6 + ESC icons from /int/icons/*.png. Missing files are
// tolerated — the caller falls back to text labels.
void       icons_load(void);
pax_buf_t *icons_get(icon_key_t key);
bool       icons_any_missing(void);
