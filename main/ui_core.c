#include "ui_core.h"

#include <stdlib.h>
#include <string.h>

#include "bsp/device.h"
#include "bsp/input.h"
#include "discord_task.h"
#include "esp_log.h"
#include "fbdraw.h"
#include "icons.h"
#include "msgstore.h"
#include "shapes/pax_misc.h"

static const char TAG[] = "ui_core";

// --- Chat ring buffer ----------------------------------------------------

#define CHAT_RING_LEN  64

typedef struct {
    char *author;
    char *content;
} chat_msg_t;

static chat_msg_t s_ring[CHAT_RING_LEN];
static int        s_ring_count = 0;     // 0..CHAT_RING_LEN
static int        s_ring_head  = 0;     // next write slot
static int        s_scroll     = 0;     // lines scrolled up from bottom (0 = pinned to bottom)

static void chat_clear(void) {
    for (int i = 0; i < s_ring_count; i++) {
        free(s_ring[i].author);
        free(s_ring[i].content);
        s_ring[i].author = s_ring[i].content = NULL;
    }
    s_ring_count = 0;
    s_ring_head  = 0;
    s_scroll     = 0;
}

static void chat_push(char *author, char *content) {
    int slot = s_ring_head;
    if (s_ring_count == CHAT_RING_LEN) {
        // ring is full: oldest entry (at s_ring_head) gets overwritten
        free(s_ring[slot].author);
        free(s_ring[slot].content);
    } else {
        s_ring_count++;
    }
    s_ring[slot].author  = author;
    s_ring[slot].content = content;
    s_ring_head = (s_ring_head + 1) % CHAT_RING_LEN;
}

static const chat_msg_t *chat_at(int idx_from_oldest) {
    if (idx_from_oldest < 0 || idx_from_oldest >= s_ring_count) return NULL;
    int first = (s_ring_head - s_ring_count + CHAT_RING_LEN) % CHAT_RING_LEN;
    return &s_ring[(first + idx_from_oldest) % CHAT_RING_LEN];
}

// --- UI state ------------------------------------------------------------

typedef struct {
    pax_buf_t      *fb;
    size_t          panel_w;
    size_t          panel_h;
    const config_t *cfg;
    ui_screen_t     screen;
    int             channel_sel;
    int             active_channel;
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

static int draw_hint(pax_buf_t *fb, int x, int y, const hint_t *h) {
    const ui_theme_t *t  = g_theme;
    pax_buf_t        *ic = icons_get(h->icon);
    int               icon_bot = y + HINT_ICON_SZ;

    if (ic) {
        pax_draw_image_sized(fb, ic, (float)x, (float)y, (float)HINT_ICON_SZ, (float)HINT_ICON_SZ);
        x += HINT_ICON_SZ + 6;
    } else {
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

// --- Channel list --------------------------------------------------------

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
        { ICON_F1, "Exit" }, { ICON_F2, "Dim" }, { ICON_F3, "Bright" },
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
                chat_clear();
                const channel_t *c = &s->cfg->channels[s->active_channel];
                // Hydrate the ring from disk so the chat view shows history
                // immediately, even before any live/backfill activity.
                stored_msg_t *tail = NULL;
                size_t        n_tail = 0;
                if (msgstore_load_tail(c->channel_id, CHAT_RING_LEN, &tail, &n_tail) == ESP_OK) {
                    for (size_t i = 0; i < n_tail; i++) {
                        // chat_push takes ownership of the strings.
                        chat_push(tail[i].author, tail[i].content);
                        tail[i].author = tail[i].content = NULL;
                    }
                    msgstore_free_tail(tail, n_tail);
                }
                ESP_LOGI(TAG, "open channel: %s (%s) — %u stored",
                         c->name ? c->name : "?", c->channel_id, (unsigned)n_tail);
            }
            break;
        default:
            break;
    }
}

// --- Chat view -----------------------------------------------------------

#define CHAT_TOP         100
#define CHAT_LINE_H      22
#define CHAT_MSG_GAP     6
#define CHAT_SCALE_AUTH  0.9f
#define CHAT_SCALE_BODY  0.9f

// Greedy word wrap: at scale, no line exceeds max_w pixels. Mutates
// `work` (inserts NULs at break points) and records pointers into it.
// `p` advances every iteration, so the loop can't get stuck.
static int wrap_text(char *work, float scale, int max_w, const char **out_lines, int out_cap) {
    int   n = 0;
    char *p = work;
    while (*p && n < out_cap) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        char *line_start = p;
        char *last_space = NULL;
        for (;;) {
            if (*p == '\0' || *p == '\n') {
                bool had_nl = (*p == '\n');
                if (had_nl) *p = '\0';
                out_lines[n++] = line_start;
                if (had_nl) p++;
                break;
            }
            // Measure substring line_start..p inclusive.
            char saved = *(p + 1);
            *(p + 1)   = '\0';
            int w      = fbdraw_hershey_string_width(scale, line_start);
            *(p + 1)   = saved;
            if (w > max_w) {
                if (last_space && last_space > line_start) {
                    *last_space    = '\0';
                    out_lines[n++] = line_start;
                    p              = last_space + 1;
                } else {
                    // Hard break before p (one char lost — fine for runaway URLs).
                    *p             = '\0';
                    out_lines[n++] = line_start;
                    p++;
                }
                break;
            }
            if (*p == ' ') last_space = p;
            p++;
        }
    }
    return n;
}

// Render the chat view. Builds a combined list of wrapped lines for all
// messages in the ring and paints the last `visible_lines - s_scroll`
// lines from bottom upward.
static void draw_chat(ui_state_t *s) {
    const ui_theme_t *t = g_theme;
    const channel_t  *c = &s->cfg->channels[s->active_channel];
    int W = pax_buf_get_width(s->fb);
    int H = pax_buf_get_height(s->fb);

    pax_background(s->fb, t->bg);
    pax_simple_rect(s->fb, t->header_bg, 0, 0, W, 60);
    fbdraw_hershey_string(s->fb, 8, 20, 1.2f, t->highlight, c->name ? c->name : "(unnamed)");

    int chat_top = 70;
    int chat_bot = H - HINT_BAR_H - 4;
    int max_line_w = W - 16;

    // Walk messages newest -> oldest, stacking wrapped lines upward.
    int y = chat_bot;
    int lines_skipped = 0;
    for (int i = s_ring_count - 1; i >= 0 && y > chat_top; i--) {
        const chat_msg_t *m = chat_at(i);
        if (!m) break;

        size_t author_len = m->author ? strlen(m->author) : 1;
        size_t body_len   = m->content ? strlen(m->content) : 0;
        char  *work       = malloc(author_len + body_len + 16);
        if (!work) continue;
        snprintf(work, author_len + body_len + 16, "%s: %s",
                 m->author ? m->author : "?",
                 m->content ? m->content : "");

        const char *lines[32];
        int n = wrap_text(work, CHAT_SCALE_BODY, max_line_w, lines, 32);

        // Draw from last wrapped line upward
        for (int k = n - 1; k >= 0; k--) {
            if (lines_skipped < s_scroll) {
                lines_skipped++;
                continue;
            }
            int row_y = y - CHAT_LINE_H;
            if (row_y < chat_top) break;
            // author portion (first line only, in highlight)
            if (k == 0) {
                // draw the full line; we also overlay the author in the theme's
                // highlight colour by redrawing just the "name:" prefix on top.
                fbdraw_hershey_string(s->fb, 8, row_y + 4, CHAT_SCALE_BODY, t->text, lines[k]);
                char  head[96];
                snprintf(head, sizeof(head), "%s:", m->author ? m->author : "?");
                int hw = fbdraw_hershey_string_width(CHAT_SCALE_BODY, head);
                (void)hw;
                fbdraw_hershey_string(s->fb, 8, row_y + 4, CHAT_SCALE_BODY, t->highlight, head);
            } else {
                fbdraw_hershey_string(s->fb, 8, row_y + 4, CHAT_SCALE_BODY, t->text, lines[k]);
            }
            y = row_y;
        }
        y -= CHAT_MSG_GAP;
        free(work);
    }

    if (s_ring_count == 0) {
        fbdraw_hershey_string(s->fb, 8, chat_top + 8, 0.9f, t->text_dim,
                              "No messages yet. Messages will appear here as they arrive.");
    }

    const hint_t hints[] = {
        { ICON_ESC, "Back" }, { ICON_F1, "Exit" }, { ICON_F2, "Dim" }, { ICON_F3, "Bright" },
    };
    draw_hint_bar(s->fb, W, H, hints, sizeof(hints) / sizeof(hints[0]));
    blit(s);
}

static void handle_chat_input(ui_state_t *s, const bsp_input_event_t *ev) {
    if (ev->type != INPUT_EVENT_TYPE_NAVIGATION) return;
    if (!ev->args_navigation.state) return;
    switch (ev->args_navigation.key) {
        case BSP_INPUT_NAVIGATION_KEY_ESC:
            s->screen = UI_SCREEN_CHANNEL_LIST;
            break;
        case BSP_INPUT_NAVIGATION_KEY_UP:
            s_scroll++;
            break;
        case BSP_INPUT_NAVIGATION_KEY_DOWN:
            if (s_scroll > 0) s_scroll--;
            break;
        default:
            break;
    }
}

// --- Inbound drain -------------------------------------------------------

// Returns true if at least one inbound message was consumed.
static bool drain_inbound(ui_state_t *s) {
    QueueHandle_t q = discord_task_inbound_queue();
    if (!q) return false;
    bool any = false;
    inbound_msg_t *im;
    while (xQueueReceive(q, &im, 0) == pdTRUE) {
        // Route: if chat is open on this channel, push into ring; else drop.
        bool routed = false;
        if (s->screen == UI_SCREEN_CHAT) {
            const channel_t *c = &s->cfg->channels[s->active_channel];
            if (c->channel_id && im->channel_id && strcmp(c->channel_id, im->channel_id) == 0) {
                chat_push(im->author, im->content);
                im->author = im->content = NULL;  // ownership transferred
                routed = true;
                any    = true;
            }
        }
        if (!routed) {
            free(im->author);
            free(im->content);
        }
        free(im->channel_id);
        free(im);
    }
    return any;
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
        // Poll input with a short timeout so we can also drain the Discord
        // inbound queue and repaint the chat view when messages arrive.
        bool need_redraw = false;
        if (xQueueReceive(input_queue, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (!handle_global_input(&ev)) {
                switch (s.screen) {
                    case UI_SCREEN_CHANNEL_LIST: handle_channel_list_input(&s, &ev); break;
                    case UI_SCREEN_CHAT:         handle_chat_input(&s, &ev);         break;
                }
                need_redraw = true;
            }
        }
        if (drain_inbound(&s) && s.screen == UI_SCREEN_CHAT) {
            need_redraw = true;
        }
        if (need_redraw) render(&s);
    }
}
