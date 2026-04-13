#include "backfill.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "discord_task.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "msgstore.h"

static const char TAG[]     = "backfill";
static const char API_BASE[] = "https://discord.com/api/v10";

#define PAGE_LIMIT 100           // we keep at most 100 msgs per channel anyway, so one page

static const config_t *s_cfg          = NULL;
static char           *s_token_header = NULL;  // e.g. "Bot xyz..."
static QueueHandle_t   s_kick_q       = NULL;
static TaskHandle_t    s_task         = NULL;

// Streaming response buffer captured by the http event handler.
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} http_buf_t;

static esp_err_t http_event(esp_http_client_event_t *evt) {
    http_buf_t *b = (http_buf_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->data_len) return ESP_OK;
    size_t need = b->len + evt->data_len + 1;
    if (need > b->cap) {
        size_t newcap = b->cap ? b->cap * 2 : 4096;
        while (newcap < need) newcap *= 2;
        char *nb = realloc(b->buf, newcap);
        if (!nb) return ESP_ERR_NO_MEM;
        b->buf = nb;
        b->cap = newcap;
    }
    memcpy(b->buf + b->len, evt->data, evt->data_len);
    b->len += evt->data_len;
    b->buf[b->len] = '\0';
    return ESP_OK;
}

// Returns parsed cJSON array of messages (caller frees) or NULL on failure.
// Discord returns newest-first.
static cJSON *fetch_page(const char *channel_id, const char *after_id, int limit) {
    char url[256];
    if (after_id) {
        snprintf(url, sizeof(url), "%s/channels/%s/messages?after=%s&limit=%d",
                 API_BASE, channel_id, after_id, limit);
    } else {
        snprintf(url, sizeof(url), "%s/channels/%s/messages?limit=%d",
                 API_BASE, channel_id, limit);
    }

    http_buf_t body = {0};
    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler     = http_event,
        .user_data         = &body,
        .timeout_ms        = 15000,
    };
    esp_http_client_handle_t hc = esp_http_client_init(&cfg);
    if (!hc) return NULL;
    esp_http_client_set_header(hc, "Authorization", s_token_header);
    esp_http_client_set_header(hc, "User-Agent", "TanmatsuDiscord/0.1");

    esp_err_t r      = esp_http_client_perform(hc);
    int       status = esp_http_client_get_status_code(hc);
    esp_http_client_cleanup(hc);

    if (r != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "GET %s -> err=%s status=%d body=%.*s",
                 url, esp_err_to_name(r), status,
                 body.len > 200 ? 200 : (int)body.len, body.buf ? body.buf : "");
        free(body.buf);
        return NULL;
    }

    cJSON *root = cJSON_ParseWithLength(body.buf, body.len);
    free(body.buf);
    if (!cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "%s: response is not a JSON array", channel_id);
        if (root) cJSON_Delete(root);
        return NULL;
    }
    return root;
}

// Push one stored message into the live inbound queue so the chat view
// updates immediately. Allocates a fresh inbound_msg_t (heap, owns strings).
static void push_inbound(const char *channel_id, const char *author, const char *content) {
    QueueHandle_t q = discord_task_inbound_queue();
    if (!q) return;
    inbound_msg_t *im = calloc(1, sizeof(*im));
    if (!im) return;
    im->channel_id = strdup(channel_id ? channel_id : "");
    im->author     = strdup(author ? author : "?");
    im->content    = strdup(content ? content : "");
    if (!im->channel_id || !im->author || !im->content) {
        free(im->channel_id); free(im->author); free(im->content); free(im);
        return;
    }
    if (xQueueSend(q, &im, 0) != pdTRUE) {
        free(im->channel_id); free(im->author); free(im->content); free(im);
    }
}

// Backfill a channel: fetch the most recent PAGE_LIMIT messages and push
// any whose snowflake id is strictly newer than the local last_seen_id
// into the store + inbound queue. We never page beyond PAGE_LIMIT — the
// store caps at MAX_KEEP=100 anyway, so deeper history would just be
// trimmed away. If the device was offline long enough that more than 100
// messages were posted, the older ones in that gap are lost.
static void backfill_channel(const channel_t *ch) {
    if (!ch || !ch->channel_id) return;

    char *last_id = msgstore_last_id(ch->channel_id);

    // Always omit ?after=: we want the most recent PAGE_LIMIT, period.
    // msgstore_append handles dedup against last_id internally.
    cJSON *arr = fetch_page(ch->channel_id, NULL, PAGE_LIMIT);
    free(last_id);
    if (!arr) return;

    int n     = cJSON_GetArraySize(arr);
    int total = 0;

    // Discord returns newest-first; iterate oldest-first so JSONL stays
    // monotonic and msgstore's snowflake dedup works.
    for (int i = n - 1; i >= 0; i--) {
        cJSON *m   = cJSON_GetArrayItem(arr, i);
        cJSON *jid = cJSON_GetObjectItem(m, "id");
        cJSON *jc  = cJSON_GetObjectItem(m, "content");
        cJSON *ja  = cJSON_GetObjectItem(m, "author");
        const char *id = cJSON_IsString(jid) ? jid->valuestring : NULL;
        if (!id) continue;

        const char *content = cJSON_IsString(jc) ? jc->valuestring : "";
        const char *author  = "?";
        bool        is_bot  = false;
        if (cJSON_IsObject(ja)) {
            cJSON *ju = cJSON_GetObjectItem(ja, "username");
            cJSON *jb = cJSON_GetObjectItem(ja, "bot");
            if (cJSON_IsString(ju)) author = ju->valuestring;
            if (cJSON_IsBool(jb))   is_bot = cJSON_IsTrue(jb);
        }
        if (is_bot) continue;

        if (msgstore_append(ch->channel_id, id, author, content) == ESP_OK) {
            push_inbound(ch->channel_id, author, content);
            total++;
        }
    }
    cJSON_Delete(arr);

    if (total > 0) {
        ESP_LOGI(TAG, "channel %s: backfilled %d new",
                 ch->name ? ch->name : ch->channel_id, total);
    }
}

static void task_loop(void *arg) {
    (void)arg;
    int kick;
    while (1) {
        if (xQueueReceive(s_kick_q, &kick, portMAX_DELAY) != pdTRUE) continue;
        // Drain any extra kicks that piled up while we were running.
        while (xQueueReceive(s_kick_q, &kick, 0) == pdTRUE) { }
        ESP_LOGI(TAG, "sweep starting");
        for (size_t i = 0; i < s_cfg->num_channels; i++) {
            backfill_channel(&s_cfg->channels[i]);
        }
        ESP_LOGI(TAG, "sweep done");
    }
}

esp_err_t backfill_init(const config_t *cfg, const char *bot_token) {
    if (!cfg || !bot_token) return ESP_ERR_INVALID_ARG;
    if (s_task) return ESP_OK;
    s_cfg = cfg;
    // Build the Authorization header value. Accept the token with or
    // without a "Bot " prefix (we normalize either way).
    const char *raw = bot_token;
    if (strncmp(raw, "Bot ", 4) == 0) raw += 4;
    while (*raw == ' ') raw++;
    size_t need = strlen(raw) + 5;  // "Bot " + token + NUL
    s_token_header = malloc(need);
    if (!s_token_header) return ESP_ERR_NO_MEM;
    snprintf(s_token_header, need, "Bot %s", raw);
    s_kick_q = xQueueCreate(4, sizeof(int));
    if (!s_kick_q) return ESP_ERR_NO_MEM;
    if (xTaskCreate(task_loop, "discord_bf", 8192, NULL, 4, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void backfill_kick(void) {
    if (!s_kick_q) return;
    int v = 1;
    xQueueSend(s_kick_q, &v, 0);
}
