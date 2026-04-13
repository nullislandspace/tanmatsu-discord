#include "ui_core.h"

#include "bsp/device.h"
#include "bsp/input.h"
#include "esp_log.h"
#include "fbdraw.h"
#include "icons.h"
#include "shapes/pax_misc.h"

static const char TAG[] = "ui_core";

typedef struct {
    pax_buf_t        *fb;
    size_t            panel_w;       // physical panel dims for bsp_display_blit
    size_t            panel_h;
    const config_t   *cfg;
    ui_screen_t       screen;
    int               channel_sel;
    int               active_channel;
} ui_state_t;

static void blit(ui_state_t *s) {
    bsp_display_blit(0, 0, s->panel_w, s->panel_h, pax_buf_get_pixels(s->fb));
}

// --- Hint bar ------------------------------------------------------------

#define HINT_BAR_H    40
#define HINT_ICON_SZ  28
#define HINT_ITEM_GAP 14
#define HINT_PAD_X    8

typedef struct {
    icon_key_t  icon;
    const char *label;
} hint_t;

// Draw a single icon+label pair at (x, y_top). Returns x after the item.
static int draw_hint(pax_buf_t *fb, int x, int y, const hint_t *h) {
    const ui_theme_t *t  = g_theme;
    pax_buf_t        *ic = icons_get(h->icon);
    int               icon_bot = y + HINT_ICON_SZ;

    if (ic) {
        pax_draw_image_sized(fb, ic, (float)x, (float)y, (float)HINT_ICON_SZ, (float)HINT_ICON_SZ);
        x += HINT_ICON_SZ + 6;
    } else {
        // Fallback text tag for the icon if it failed to load.
        static const char *fallback[] = {
            [ICON_ESC] = "Esc", [ICON_F1] = "F1", [ICON_F2] = "F2",
            [ICON_F3] = "F3",   [ICON_F4] = "F4", [ICON_F5] = "F5", [ICON_F6] = "F6",
        };
        const char *tag = (h->icon >= 0 && h->icon < ICON_KEY_COUNT) ? fallback[h->icon] : "?";
        pax_simple_rect(fb, t->focus_bg, x, y, HINT_ICON_SZ, HINT_ICON_SZ);
        int tw = fbdraw_hershey_string_width(0.8f, tag);
        fbdraw_hershey_string(fb, x + (HINT_ICON_SZ - tw) / 2, y + 6, 0.8f, t->highlight, tag);
        x += HINT_ICON_SZ + 6;
    }

    // Label baseline roughly centered to the icon row.
    fbdraw_hershey_string(fb, x, icon_bot - 22, 0.8f, t->text, h->label);
    x += fbdraw_hershey_string_width(0.8f, h->label) + HINT_ITEM_GAP;
    return x;
}

static void draw_hint_bar(pax_buf_t *fb, int panel_user_w, int panel_user_h,
                          const hint_t *items, int count) {
    const ui_theme_t *t = g_theme;
    int y = panel_user_h - HINT_BAR_H;
    pax_simple_rect(fb, t->header_bg, 0, y, panel_user_w, HINT_BAR_H);
    int x = HINT_PAD_X;
    for (int i = 0; i < count; i++) {
        x = draw_hint(fb, x, y + (HINT_BAR_H - HINT_ICON_SZ) / 2, &items[i]);
    }
}

// --- Channel list screen -------------------------------------------------

#define ROW_HEIGHT 40
#define ROW_Y0     110

static void draw_channel_list(ui_state_t *s) {
    const ui_theme_t *t = g_theme;
    int w = pax_buf_get_width(s->fb);
    int h = pax_buf_get_height(s->fb);

    pax_background(s->fb, t->bg);
    pax_simple_rect(s->fb, t->header_bg, 0, 0, w, 100);
    fbdraw_hershey_string(s->fb, 8, 20, 1.5f, t->highlight, "Tanmatsu Discord");
    fbdraw_hershey_string(s->fb, 8, 70, 0.8f, t->text_dim, "Up/Down select    Enter open");

    if (s->cfg->num_channels == 0) {
        fbdraw_hershey_string(s->fb, 8, ROW_Y0 + 8, 1.0f, t->bad,
                              "No channels configured in discord.json");
    } else {
        for (size_t i = 0; i < s->cfg->num_channels; i++) {
            int  y        = ROW_Y0 + (int)i * ROW_HEIGHT;
            bool selected = ((int)i == s->channel_sel);
            if (selected) {
                pax_simple_rect(s->fb, t->focus_bg, 0, y, w, ROW_HEIGHT);
            }
            pax_col_t name_col = selected ? t->highlight : t->text;
            pax_col_t id_col   = selected ? t->highlight : t->text_dim;
            const char *name = s->cfg->channels[i].name ? s->cfg->channels[i].name : "(unnamed)";
            fbdraw_hershey_string(s->fb, 16, y + 8, 1.0f, name_col, name);
            const char *cid = s->cfg->channels[i].channel_id ? s->cfg->channels[i].channel_id : "";
            fbdraw_hershey_string(s->fb, 320, y + 12, 0.7f, id_col, cid);
        }
    }

    const hint_t hints[] = {
        { ICON_F1, "Exit" },
        { ICON_F2, "Dim" },
        { ICON_F3, "Bright" },
    };
    draw_hint_bar(s->fb, w, h, hints, sizeof(hints) / sizeof(hints[0]));
    blit(s);
}

static void handle_channel_list_input(ui_state_t *s, const bsp_input_event_t *ev) {
    if (ev->type != INPUT_EVENT_TYPE_NAVIGATION) return;
    if (!ev->args_navigation.state) return;

    int n = (int)s->cfg->num_channels;
    switch (ev->args_navigation.key) {
        case BSP_INPUT_NAVIGATION_KEY_UP:
            if (n > 0) s->channel_sel = (s->channel_sel - 1 + n) % n;
            break;
        case BSP_INPUT_NAVIGATION_KEY_DOWN:
            if (n > 0) s->channel_sel = (s->channel_sel + 1) % n;
            break;
        case BSP_INPUT_NAVIGATION_KEY_RETURN:
            if (n > 0) {
                s->active_channel = s->channel_sel;
                s->screen         = UI_SCREEN_CHAT;
                ESP_LOGI(TAG, "open channel: %s (%s)",
                         s->cfg->channels[s->active_channel].name ? s->cfg->channels[s->active_channel].name : "?",
                         s->cfg->channels[s->active_channel].channel_id);
            }
            break;
        default:
            break;
    }
}

// --- Chat screen (placeholder until step 7) ------------------------------

static void draw_chat(ui_state_t *s) {
    const ui_theme_t *t = g_theme;
    const channel_t  *c = &s->cfg->channels[s->active_channel];
    int w = pax_buf_get_width(s->fb);
    int h = pax_buf_get_height(s->fb);

    pax_background(s->fb, t->bg);
    pax_simple_rect(s->fb, t->header_bg, 0, 0, w, 100);
    fbdraw_hershey_string(s->fb, 8, 20, 1.5f, t->highlight, c->name ? c->name : "(unnamed)");
    fbdraw_hershey_string(s->fb, 8, 70, 0.8f, t->text_dim, "(chat view coming soon)");
    fbdraw_hershey_string(s->fb, 8, 120, 1.0f, t->text, "channel_id:");
    fbdraw_hershey_string(s->fb, 8, 150, 0.9f, t->info, c->channel_id ? c->channel_id : "?");

    const hint_t hints[] = {
        { ICON_ESC, "Back" },
        { ICON_F1,  "Exit" },
        { ICON_F2,  "Dim" },
        { ICON_F3,  "Bright" },
    };
    draw_hint_bar(s->fb, w, h, hints, sizeof(hints) / sizeof(hints[0]));
    blit(s);
}

static void handle_chat_input(ui_state_t *s, const bsp_input_event_t *ev) {
    if (ev->type != INPUT_EVENT_TYPE_NAVIGATION) return;
    if (!ev->args_navigation.state) return;
    if (ev->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_ESC) {
        s->screen = UI_SCREEN_CHANNEL_LIST;
    }
}

// --- Main loop -----------------------------------------------------------

static bool handle_global_input(const bsp_input_event_t *ev) {
    if (ev->type != INPUT_EVENT_TYPE_NAVIGATION) return false;
    if (!ev->args_navigation.state) return false;
    switch (ev->args_navigation.key) {
        case BSP_INPUT_NAVIGATION_KEY_F1:
            bsp_device_restart_to_launcher();
            return true;
        case BSP_INPUT_NAVIGATION_KEY_F2:
            bsp_input_set_backlight_brightness(0);
            return true;
        case BSP_INPUT_NAVIGATION_KEY_F3:
            bsp_input_set_backlight_brightness(100);
            return true;
        default:
            return false;
    }
}

static void render(ui_state_t *s) {
    switch (s->screen) {
        case UI_SCREEN_CHANNEL_LIST: draw_channel_list(s); break;
        case UI_SCREEN_CHAT:         draw_chat(s);         break;
    }
}

void ui_run(pax_buf_t *fb, size_t panel_w, size_t panel_h,
            const config_t *cfg, QueueHandle_t input_queue) {
    ui_state_t s = {
        .fb             = fb,
        .panel_w        = panel_w,
        .panel_h        = panel_h,
        .cfg            = cfg,
        .screen         = UI_SCREEN_CHANNEL_LIST,
        .channel_sel    = 0,
        .active_channel = 0,
    };

    render(&s);

    while (1) {
        bsp_input_event_t ev;
        if (xQueueReceive(input_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        if (handle_global_input(&ev)) continue;

        switch (s.screen) {
            case UI_SCREEN_CHANNEL_LIST: handle_channel_list_input(&s, &ev); break;
            case UI_SCREEN_CHAT:         handle_chat_input(&s, &ev);         break;
        }
        render(&s);
    }
}
