#include "file_browser.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "fbdraw.h"
#include "icons.h"
#include "shapes/pax_misc.h"
#include "theme.h"

static const char TAG[] = "fb";

#define MAX_ENTRIES 200
#define ROW_H       28
#define HEADER_H    60
#define HINT_BAR_H  40

typedef struct {
    char *name;
    bool  is_dir;
} fb_entry_t;

static char         s_cwd[256] = "/sd";
static fb_entry_t   s_entries[MAX_ENTRIES];
static int          s_count    = 0;
static int          s_sel      = 0;
static int          s_scroll   = 0;
static char        *s_picked   = NULL;

// strcasecmp may not be available on bare newlib in IDF builds.
static int istrcmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static bool is_jpeg(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return false;
    return istrcmp(name + n - 4, ".jpg") == 0 ||
           (n >= 5 && istrcmp(name + n - 5, ".jpeg") == 0);
}

static int sort_cmp(const void *aa, const void *bb) {
    const fb_entry_t *a = aa, *b = bb;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return istrcmp(a->name, b->name);
}

static void clear_entries(void) {
    for (int i = 0; i < s_count; i++) free(s_entries[i].name);
    s_count = 0;
}

static void scan_cwd(void) {
    clear_entries();
    DIR *d = opendir(s_cwd);
    if (!d) {
        ESP_LOGW(TAG, "opendir(%s) failed", s_cwd);
        return;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && s_count < MAX_ENTRIES) {
        if (de->d_name[0] == '.') continue;
        char full[768];
        snprintf(full, sizeof(full), "%s/%s", s_cwd, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        bool is_dir = S_ISDIR(st.st_mode);
        if (!is_dir && !is_jpeg(de->d_name)) continue;
        s_entries[s_count].name   = strdup(de->d_name);
        s_entries[s_count].is_dir = is_dir;
        s_count++;
    }
    closedir(d);
    qsort(s_entries, s_count, sizeof(fb_entry_t), sort_cmp);
    if (s_sel >= s_count) s_sel = s_count > 0 ? s_count - 1 : 0;
    s_scroll = 0;
}

static bool at_root(void) {
    return strcmp(s_cwd, "/sd") == 0;
}

static void go_up(void) {
    if (at_root()) return;
    char *slash = strrchr(s_cwd, '/');
    if (!slash || slash == s_cwd) return;
    *slash = '\0';
    s_sel = 0;
    scan_cwd();
}

static void descend(const char *name) {
    size_t cur = strlen(s_cwd);
    if (cur + 1 + strlen(name) >= sizeof(s_cwd)) return;
    s_cwd[cur] = '/';
    strcpy(s_cwd + cur + 1, name);
    s_sel = 0;
    scan_cwd();
}

void fb_open(const char *start_dir) {
    free(s_picked); s_picked = NULL;
    snprintf(s_cwd, sizeof(s_cwd), "%s", start_dir ? start_dir : "/sd");
    s_sel    = 0;
    s_scroll = 0;
    scan_cwd();
    if (s_count == 0 && strcmp(s_cwd, "/sd") != 0) {
        // start dir empty / unreadable — fall back to /sd
        snprintf(s_cwd, sizeof(s_cwd), "/sd");
        scan_cwd();
    }
}

char *fb_picked_path(void) { return s_picked; }

void fb_draw(pax_buf_t *fb, int W, int H) {
    const ui_theme_t *t = g_theme;
    pax_background(fb, t->bg);
    pax_simple_rect(fb, t->header_bg, 0, 0, W, HEADER_H);
    fbdraw_hershey_string(fb, 8, 16, 1.0f, t->highlight, "Pick an image");
    fbdraw_hershey_string(fb, 8, 38, 0.7f, t->text_dim, s_cwd);

    int list_top = HEADER_H + 4;
    int list_bot = H - HINT_BAR_H - 4;
    int rows     = (list_bot - list_top) / ROW_H;

    // Scroll cursor into view
    if (s_sel < s_scroll) s_scroll = s_sel;
    if (s_sel >= s_scroll + rows) s_scroll = s_sel - rows + 1;

    if (s_count == 0) {
        fbdraw_hershey_string(fb, 8, list_top + 8, 0.9f, t->text_dim,
                              "(no JPEGs or subdirectories here)");
    }
    for (int i = 0; i < rows && (i + s_scroll) < s_count; i++) {
        int idx = i + s_scroll;
        int y   = list_top + i * ROW_H;
        bool selected = (idx == s_sel);
        if (selected) pax_simple_rect(fb, t->focus_bg, 0, y, W, ROW_H);
        pax_col_t col = selected ? t->highlight : t->text;
        const char *prefix = s_entries[idx].is_dir ? "[ ] " : "    ";
        char line[300];
        snprintf(line, sizeof(line), "%s%s%s", prefix, s_entries[idx].name,
                 s_entries[idx].is_dir ? "/" : "");
        fbdraw_hershey_string(fb, 12, y + 6, 0.9f, col, line);
    }
}

fb_result_t fb_handle_input(const bsp_input_event_t *ev) {
    if (ev->type != INPUT_EVENT_TYPE_NAVIGATION) return FB_RESULT_NONE;
    if (!ev->args_navigation.state) return FB_RESULT_NONE;

    switch (ev->args_navigation.key) {
        case BSP_INPUT_NAVIGATION_KEY_UP:
            if (s_sel > 0) s_sel--;
            return FB_RESULT_NONE;
        case BSP_INPUT_NAVIGATION_KEY_DOWN:
            if (s_sel < s_count - 1) s_sel++;
            return FB_RESULT_NONE;
        case BSP_INPUT_NAVIGATION_KEY_RETURN:
            if (s_count == 0) return FB_RESULT_NONE;
            if (s_entries[s_sel].is_dir) {
                descend(s_entries[s_sel].name);
                return FB_RESULT_NONE;
            }
            // file pick
            free(s_picked);
            size_t need = strlen(s_cwd) + 1 + strlen(s_entries[s_sel].name) + 1;
            s_picked    = malloc(need);
            if (s_picked) snprintf(s_picked, need, "%s/%s", s_cwd, s_entries[s_sel].name);
            return FB_RESULT_PICK;
        case BSP_INPUT_NAVIGATION_KEY_ESC:
            if (at_root()) return FB_RESULT_CANCEL;
            go_up();
            return FB_RESULT_NONE;
        default:
            return FB_RESULT_NONE;
    }
}
