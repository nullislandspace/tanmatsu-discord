#pragma once

#include <stdint.h>
#include "pax_gfx.h"

int fbdraw_hershey_char(pax_buf_t *fb, int x, int y, float scale, pax_col_t color, int c);
int fbdraw_hershey_string(pax_buf_t *fb, int x, int y, float scale, pax_col_t color, const char *s);
int fbdraw_hershey_string_width(float scale, const char *s);
