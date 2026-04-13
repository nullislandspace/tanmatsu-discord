#include "msgstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_log.h"

static const char TAG[]    = "msgstore";
static const char DIR[]    = "/sd/discord";
#define MAX_LINE_LEN (8 * 1024)   // 2000 content chars * ~4 escape expansion + overhead
#define MAX_KEEP     100          // hard cap on lines kept per channel JSONL

esp_err_t msgstore_init(void) {
    struct stat st;
    if (stat(DIR, &st) == 0) return ESP_OK;
    if (mkdir(DIR, 0777) != 0) {
        ESP_LOGE(TAG, "mkdir %s failed", DIR);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "created %s", DIR);
    return ESP_OK;
}

static void path_for(const char *channel_id, char *out, size_t outlen) {
    snprintf(out, outlen, "%s/%s.jsonl", DIR, channel_id);
}

// Snowflake IDs are 64-bit decimal strings. Compare lexicographically
// once padded to equal length — or just compare numerically.
static int snowflake_cmp(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la != lb) return (la < lb) ? -1 : 1;
    return strcmp(a, b);
}

char *msgstore_last_id(const char *channel_id) {
    char path[128];
    path_for(channel_id, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    // Seek from end, scan back for the last '\n' before end, return the
    // id field of the last line.
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long end = ftell(f);
    if (end <= 0) { fclose(f); return NULL; }

    long read_from = end > MAX_LINE_LEN ? end - MAX_LINE_LEN : 0;
    long size      = end - read_from;
    char *buf      = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    fseek(f, read_from, SEEK_SET);
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';

    // Find the last line boundary (skip trailing newlines)
    char *p = buf + got;
    while (p > buf && (*(p - 1) == '\n' || *(p - 1) == '\r')) { *(--p) = '\0'; }
    char *lb = strrchr(buf, '\n');
    const char *last_line = lb ? lb + 1 : buf;

    cJSON *root = cJSON_Parse(last_line);
    char  *id   = NULL;
    if (root) {
        cJSON *ji = cJSON_GetObjectItem(root, "id");
        if (cJSON_IsString(ji) && ji->valuestring) {
            id = strdup(ji->valuestring);
        }
        cJSON_Delete(root);
    }
    free(buf);
    return id;
}

// Rewrite `path` to retain only the last `keep` lines. Cheap enough at
// this scale (file is ≤ ~MAX_KEEP * a few-KB).
static void trim_file(const char *path, size_t keep) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[sz] = '\0';

    size_t total = 0;
    for (char *p = buf; *p; p++) if (*p == '\n') total++;
    if (total <= keep) { free(buf); return; }

    // Find start of (total - keep)th newline → first kept char follows it.
    size_t skip   = total - keep;
    size_t seen   = 0;
    char  *retain = buf;
    for (char *p = buf; *p; p++) {
        if (*p == '\n') {
            if (++seen == skip) { retain = p + 1; break; }
        }
    }
    size_t retain_len = (size_t)((buf + sz) - retain);

    FILE *out = fopen(path, "wb");
    if (out) {
        fwrite(retain, 1, retain_len, out);
        fclose(out);
        ESP_LOGI(TAG, "trimmed %s: %u → %u lines", path, (unsigned)total, (unsigned)keep);
    }
    free(buf);
}

esp_err_t msgstore_append(const char *channel_id, const char *msg_id,
                          const char *author, const char *content) {
    if (!channel_id || !msg_id) return ESP_ERR_INVALID_ARG;

    // Dedup against current last id
    char *last = msgstore_last_id(channel_id);
    if (last && snowflake_cmp(msg_id, last) <= 0) {
        free(last);
        return ESP_ERR_INVALID_STATE;
    }
    free(last);

    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(obj, "id", msg_id);
    cJSON_AddStringToObject(obj, "author", author ? author : "");
    cJSON_AddStringToObject(obj, "content", content ? content : "");
    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!line) return ESP_ERR_NO_MEM;

    char path[128];
    path_for(channel_id, path, sizeof(path));
    FILE *f = fopen(path, "ab");
    if (!f) {
        ESP_LOGE(TAG, "open-append %s failed", path);
        free(line);
        return ESP_FAIL;
    }
    fputs(line, f);
    fputc('\n', f);
    fclose(f);
    free(line);

    trim_file(path, MAX_KEEP);
    return ESP_OK;
}

// Load tail: read the whole file (cheap for small files; TODO rotation).
esp_err_t msgstore_load_tail(const char *channel_id, size_t max,
                             stored_msg_t **out_msgs, size_t *out_count) {
    *out_msgs  = NULL;
    *out_count = 0;

    char path[128];
    path_for(channel_id, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_OK;  // no file = no history, not an error

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return ESP_OK; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[sz] = '\0';

    // Count lines and remember starting positions.
    size_t total = 0;
    for (char *p = buf; *p; p++) if (*p == '\n') total++;
    if (total == 0) { free(buf); return ESP_OK; }

    size_t keep  = total < max ? total : max;
    size_t skip  = total - keep;
    stored_msg_t *arr = calloc(keep, sizeof(*arr));
    if (!arr) { free(buf); return ESP_ERR_NO_MEM; }

    size_t line_idx = 0, out_idx = 0;
    char  *line     = buf;
    while (*line && out_idx < keep) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line_idx >= skip && *line) {
            cJSON *r = cJSON_Parse(line);
            if (r) {
                cJSON *ji = cJSON_GetObjectItem(r, "id");
                cJSON *ja = cJSON_GetObjectItem(r, "author");
                cJSON *jc = cJSON_GetObjectItem(r, "content");
                arr[out_idx].id      = (cJSON_IsString(ji) && ji->valuestring) ? strdup(ji->valuestring) : strdup("");
                arr[out_idx].author  = (cJSON_IsString(ja) && ja->valuestring) ? strdup(ja->valuestring) : strdup("");
                arr[out_idx].content = (cJSON_IsString(jc) && jc->valuestring) ? strdup(jc->valuestring) : strdup("");
                cJSON_Delete(r);
                out_idx++;
            }
        }
        line_idx++;
        if (!nl) break;
        line = nl + 1;
    }
    free(buf);
    *out_msgs  = arr;
    *out_count = out_idx;
    return ESP_OK;
}

void msgstore_free_tail(stored_msg_t *msgs, size_t count) {
    if (!msgs) return;
    for (size_t i = 0; i < count; i++) {
        free(msgs[i].id);
        free(msgs[i].author);
        free(msgs[i].content);
    }
    free(msgs);
}
