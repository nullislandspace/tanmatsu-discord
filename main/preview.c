#include "preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/jpeg_decode.h"
#include "esp_log.h"
#include "shapes/pax_misc.h"

static const char TAG[] = "preview";

// Bound the work we'll do per image. JPEG input bytes — Discord's
// non-Nitro upload limit is 25 MB anyway. Decoded RGB565 pixels —
// a 12 MP photo lands at ~24 MB which exceeds typical PSRAM headroom
// alongside everything else; cap so we degrade gracefully.
#define MAX_INPUT_BYTES   (10 * 1024 * 1024)
#define MAX_OUTPUT_BYTES  (16 * 1024 * 1024)

static char     *s_path    = NULL;
static uint8_t  *s_decoded = NULL;     // RGB565 pixel buffer (DMA-capable, PSRAM)
static pax_buf_t s_buf     = {0};      // pax_buf_t wrapping s_decoded
static bool      s_loaded  = false;
static uint32_t  s_w       = 0;
static uint32_t  s_h       = 0;

void preview_unload(void) {
    if (s_loaded) pax_buf_destroy(&s_buf);
    if (s_decoded) free(s_decoded);
    free(s_path);
    s_path    = NULL;
    s_decoded = NULL;
    s_loaded  = false;
    s_w = s_h = 0;
}

esp_err_t preview_load(const char *path) {
    preview_unload();
    if (!path) return ESP_ERR_INVALID_ARG;
    s_path = strdup(path);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "open(%s) failed", path);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0 || fsz > MAX_INPUT_BYTES) {
        ESP_LOGE(TAG, "bad/too-big size: %ld", fsz);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    jpeg_decode_memory_alloc_cfg_t in_cfg = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
    size_t   in_alloc = 0;
    uint8_t *in_buf   = jpeg_alloc_decoder_mem((size_t)fsz, &in_cfg, &in_alloc);
    if (!in_buf) { fclose(f); ESP_LOGE(TAG, "in alloc"); return ESP_ERR_NO_MEM; }
    if (fread(in_buf, 1, (size_t)fsz, f) != (size_t)fsz) {
        free(in_buf); fclose(f); return ESP_FAIL;
    }
    fclose(f);

    jpeg_decoder_handle_t       dec  = NULL;
    jpeg_decode_engine_cfg_t    decc = { .timeout_ms = 5000 };
    if (jpeg_new_decoder_engine(&decc, &dec) != ESP_OK) {
        free(in_buf); return ESP_FAIL;
    }

    jpeg_decode_picture_info_t info = {0};
    if (jpeg_decoder_get_info(in_buf, (uint32_t)fsz, &info) != ESP_OK ||
        info.width == 0 || info.height == 0) {
        ESP_LOGE(TAG, "bad JPEG header");
        free(in_buf); jpeg_del_decoder_engine(dec);
        return ESP_FAIL;
    }

    uint32_t pw = (info.width + 15u) & ~15u;
    uint32_t ph = (info.height + 15u) & ~15u;
    size_t   out_sz = (size_t)pw * ph * 2u;
    if (out_sz > MAX_OUTPUT_BYTES) {
        ESP_LOGE(TAG, "image %ux%u too large to decode (%zu B)", (unsigned)info.width, (unsigned)info.height, out_sz);
        free(in_buf); jpeg_del_decoder_engine(dec);
        return ESP_ERR_NO_MEM;
    }

    jpeg_decode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
    size_t   out_alloc = 0;
    uint8_t *out_buf   = jpeg_alloc_decoder_mem(out_sz, &out_cfg, &out_alloc);
    if (!out_buf) {
        free(in_buf); jpeg_del_decoder_engine(dec);
        ESP_LOGE(TAG, "out alloc (%zu)", out_sz);
        return ESP_ERR_NO_MEM;
    }

    // The HW decoder's "RGB" order produces a byte layout that PAX's
    // PAX_BUF_16_565RGB reads as red/blue-swapped. Use BGR to compensate
    // (same convention as the camera app's splash.c).
    jpeg_decode_cfg_t dcfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
    };
    uint32_t produced = 0;
    esp_err_t r = jpeg_decoder_process(dec, &dcfg, in_buf, (uint32_t)fsz,
                                       out_buf, out_alloc, &produced);
    free(in_buf);
    jpeg_del_decoder_engine(dec);
    if (r != ESP_OK) {
        free(out_buf);
        ESP_LOGE(TAG, "decode failed: %d", r);
        return r;
    }

    s_decoded = out_buf;
    s_w = pw;
    s_h = ph;
    pax_buf_init(&s_buf, s_decoded, pw, ph, PAX_BUF_16_565RGB);
    s_loaded = true;
    ESP_LOGI(TAG, "decoded %s → %ux%u", path, (unsigned)info.width, (unsigned)info.height);
    return ESP_OK;
}

void preview_draw(pax_buf_t *fb, int top_y, int bot_y) {
    if (!s_loaded) return;
    int W      = pax_buf_get_width(fb);
    int avail_w = W;
    int avail_h = bot_y - top_y;
    if (avail_w <= 0 || avail_h <= 0) return;

    // Aspect-preserving fit
    float sx = (float)avail_w / (float)s_w;
    float sy = (float)avail_h / (float)s_h;
    float sc = sx < sy ? sx : sy;
    int   dw = (int)(s_w * sc);
    int   dh = (int)(s_h * sc);
    int   dx = (avail_w - dw) / 2;
    int   dy = top_y + (avail_h - dh) / 2;
    pax_draw_image_sized(fb, &s_buf, (float)dx, (float)dy, (float)dw, (float)dh);
}

const char *preview_path(void) { return s_path; }
bool        preview_loaded(void) { return s_loaded; }
void        preview_dimensions(uint32_t *w, uint32_t *h) {
    if (w) *w = s_w;
    if (h) *h = s_h;
}
