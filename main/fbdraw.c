#include "fbdraw.h"

#include "pax_shapes.h"
#include "shapes/pax_lines.h"

#include "hershey.h"

#define HERSHEY_BASE_HEIGHT 21

int fbdraw_hershey_char(pax_buf_t *fb, int x, int y, float scale, pax_col_t color, int c) {
    int idx = (int)(unsigned char)c - 32;
    if (idx < 0 || idx >= 95) {
        return x + (int)(16.0f * scale);
    }

    const int *glyph        = simplex[idx];
    int        num_vertices = glyph[0];
    int        char_width   = glyph[1];

    if (num_vertices == 0) {
        return x + (int)((float)char_width * scale);
    }

    bool pen_down = false;
    int  prev_px = 0, prev_py = 0;

    for (int i = 0; i < num_vertices; i++) {
        int vx = glyph[2 + i * 2];
        int vy = glyph[2 + i * 2 + 1];

        if (vx == -1 && vy == -1) {
            pen_down = false;
            continue;
        }

        int px = x + (int)((float)vx * scale);
        int py = y + (int)((float)(HERSHEY_BASE_HEIGHT - vy) * scale);

        if (pen_down) {
            pax_simple_line(fb, color, (float)prev_px, (float)prev_py, (float)px, (float)py);
        }
        prev_px  = px;
        prev_py  = py;
        pen_down = true;
    }

    return x + (int)((float)char_width * scale);
}

int fbdraw_hershey_string(pax_buf_t *fb, int x, int y, float scale, pax_col_t color, const char *s) {
    while (*s) {
        x = fbdraw_hershey_char(fb, x, y, scale, color, (unsigned char)*s);
        s++;
    }
    return x;
}

int fbdraw_hershey_string_width(float scale, const char *s) {
    int width = 0;
    while (*s) {
        int idx = (int)(unsigned char)*s - 32;
        if (idx >= 0 && idx < 95) {
            width += (int)((float)simplex[idx][1] * scale);
        } else {
            width += (int)(16.0f * scale);
        }
        s++;
    }
    return width;
}
