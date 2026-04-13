#include "ui_core.h"

#include "bsp/device.h"
#include "bsp/input.h"
#include "esp_log.h"
#include "fbdraw.h"

static const char TAG[] = "ui_core";

typedef struct {
    pax_buf_t        *fb;
    const config_t   *cfg;
    ui_screen_t       screen;
    int               channel_sel;   // highlighted row in channel list
    int               active_channel; // index of channel currently shown in chat
} ui_state_t;

static void blit(ui_state_t *s) {
    int w = pax_buf_get_width(s->fb);
    int h = pax_buf_get_height(s->fb);
    bsp_display_blit(0, 0, w, h, pax_buf_get_pixels(s->fb));
}

// --- Channel list screen -------------------------------------------------

// Hershey simplex cap-height is 21 units; lowercase descenders extend
// ~7 units further down. So at scale s the full vertical footprint of a
// line is roughly 28*s. Row heights and y-offsets below are sized to fit
// that plus a couple of pixels of padding.
#define ROW_HEIGHT 40
#define ROW_Y0     110

static void draw_channel_list(ui_state_t *s) {
    pax_background(s->fb, UI_WHITE);
    // Title: scale 1.5 → cap-height ~32px. Top at y=20, baseline ~52.
    fbdraw_hershey_string(s->fb, 8, 20, 1.5f, UI_BLACK, "Tanmatsu Discord");
    // Hint row below the title, scale 0.8 → ~17px tall. Top at y=70.
    fbdraw_hershey_string(s->fb, 8, 70, 0.8f, UI_GREY, "Up/Down select   Enter open   F1 exit");

    if (s->cfg->num_channels == 0) {
        fbdraw_hershey_string(s->fb, 8, ROW_Y0 + 8, 1.0f, UI_RED,
                              "No channels configured in discord.json");
        blit(s);
        return;
    }

    for (size_t i = 0; i < s->cfg->num_channels; i++) {
        int  y        = ROW_Y0 + (int)i * ROW_HEIGHT;
        bool selected = ((int)i == s->channel_sel);
        if (selected) {
            pax_simple_rect(s->fb, UI_BLUE, 0, y, pax_buf_get_width(s->fb), ROW_HEIGHT);
        }
        pax_col_t color = selected ? UI_WHITE : UI_BLACK;
        const char *name = s->cfg->channels[i].name ? s->cfg->channels[i].name : "(unnamed)";
        // Text top placed at y+8; cap-height 21 + descender ~7 fits inside
        // the 40px row with a few pixels of padding.
        fbdraw_hershey_string(s->fb, 16, y + 8, 1.0f, color, name);
        const char *cid = s->cfg->channels[i].channel_id ? s->cfg->channels[i].channel_id : "";
        fbdraw_hershey_string(s->fb, 320, y + 12, 0.7f, selected ? UI_WHITE : UI_GREY, cid);
    }
    blit(s);
}

static void handle_channel_list_input(ui_state_t *s, const bsp_input_event_t *ev) {
    if (ev->type != INPUT_EVENT_TYPE_NAVIGATION) return;
    if (!ev->args_navigation.state) return;  // only key-down

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
    const channel_t *c = &s->cfg->channels[s->active_channel];
    pax_background(s->fb, UI_WHITE);
    fbdraw_hershey_string(s->fb, 8, 20, 1.5f, UI_BLACK, c->name ? c->name : "(unnamed)");
    fbdraw_hershey_string(s->fb, 8, 70, 0.8f, UI_GREY, "Esc back   (chat view coming soon)");
    fbdraw_hershey_string(s->fb, 8, 120, 1.0f, UI_BLACK, "channel_id:");
    fbdraw_hershey_string(s->fb, 8, 150, 0.9f, UI_BLUE, c->channel_id ? c->channel_id : "?");
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

void ui_run(pax_buf_t *fb, const config_t *cfg, QueueHandle_t input_queue) {
    ui_state_t s = {
        .fb             = fb,
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
