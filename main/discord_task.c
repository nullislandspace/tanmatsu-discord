#include "discord_task.h"

#include <stdlib.h>
#include <string.h>

#include "attach_upload.h"
#include "backfill.h"
#include "discord.h"
#include "discord/message.h"
#include "discord/user.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "msgstore.h"

static const char TAG[] = "discord_task";

#define INBOUND_Q_LEN  32
#define OUTBOUND_Q_LEN 8

static discord_handle_t s_client        = NULL;
static const config_t  *s_cfg           = NULL;
static QueueHandle_t    s_inbound_q     = NULL;
static QueueHandle_t    s_outbound_q    = NULL;
static TaskHandle_t     s_outbound_task = NULL;

static bool channel_is_configured(const char *channel_id) {
    if (!channel_id || !s_cfg) return false;
    for (size_t i = 0; i < s_cfg->num_channels; i++) {
        const char *cid = s_cfg->channels[i].channel_id;
        if (cid && strcmp(cid, channel_id) == 0) return true;
    }
    return false;
}

static char *safe_strdup(const char *s) {
    return s ? strdup(s) : NULL;
}

static void free_inbound(inbound_msg_t *m) {
    if (!m) return;
    free(m->channel_id);
    free(m->author);
    free(m->content);
    free(m);
}

static void on_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    (void)arg;
    (void)base;
    discord_event_data_t *ev = (discord_event_data_t *)event_data;

    switch ((discord_event_t)event_id) {
        case DISCORD_EVENT_CONNECTED:
            ESP_LOGI(TAG, "gateway connected");
            backfill_kick();
            break;
        case DISCORD_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "gateway disconnected");
            break;
        case DISCORD_EVENT_RECONNECTING:
            ESP_LOGW(TAG, "gateway reconnecting");
            break;
        case DISCORD_EVENT_MESSAGE_RECEIVED: {
            discord_message_t *msg = (discord_message_t *)ev->ptr;
            if (!msg || !msg->channel_id) break;
            if (!channel_is_configured(msg->channel_id)) break;
            if (msg->author && msg->author->bot) break;  // skip other bots incl. self

            const char *author  = msg->author ? msg->author->username : "?";
            const char *content = msg->content ? msg->content : "";
            // Persist first; msgstore_append dedupes by snowflake id, so a
            // gateway message that overlaps with an in-flight backfill is
            // safely dropped here.
            esp_err_t pr = msgstore_append(msg->channel_id, msg->id, author, content);
            if (pr == ESP_ERR_INVALID_STATE) break;  // duplicate, don't notify UI either

            inbound_msg_t *im = calloc(1, sizeof(*im));
            if (!im) break;
            im->channel_id = safe_strdup(msg->channel_id);
            im->author     = safe_strdup(author);
            im->content    = safe_strdup(content);
            if (!im->channel_id || !im->author || !im->content) {
                free_inbound(im);
                break;
            }
            if (xQueueSend(s_inbound_q, &im, 0) != pdTRUE) {
                ESP_LOGW(TAG, "inbound queue full, dropping message");
                free_inbound(im);
            }
            break;
        }
        default:
            break;
    }
}

// Local-echo helpers: after a successful send we want our own message to
// appear in the chat ring + JSONL right away. The gateway also delivers
// it back as MESSAGE_RECEIVED, but we filter author->bot=true there to
// avoid bot spam — so we have to inject the echo ourselves.
static void echo_locally(const char *channel_id, const char *msg_id,
                         const char *author, const char *content) {
    if (!channel_id || !msg_id) return;
    if (msgstore_append(channel_id, msg_id, author, content) != ESP_OK) return;

    inbound_msg_t *im = calloc(1, sizeof(*im));
    if (!im) return;
    im->channel_id = safe_strdup(channel_id);
    im->author     = safe_strdup(author ? author : "me");
    im->content    = safe_strdup(content ? content : "");
    if (!im->channel_id || !im->author || !im->content) { free_inbound(im); return; }
    if (xQueueSend(s_inbound_q, &im, 0) != pdTRUE) free_inbound(im);
}

static void outbound_task(void *arg) {
    (void)arg;
    outbound_msg_t *om;
    while (1) {
        if (xQueueReceive(s_outbound_q, &om, portMAX_DELAY) != pdTRUE) continue;

        if (om->jpeg_path) {
            // Attachment path: esp-discord doesn't do multipart, so we POST
            // directly via esp_http_client. No real local echo (we don't
            // have a snowflake id), but the gateway will deliver it back
            // and the bot-author filter will drop it — accept that the
            // user only sees their upload once it lands via the gateway
            // (which it won't, since we filter bots). Practical solution:
            // post a plain text "uploaded <file>" line locally so the user
            // gets confirmation.
            esp_err_t r = attach_upload_jpeg(s_cfg->token, om->channel_id,
                                             om->jpeg_path, om->text);
            if (r == ESP_OK) {
                char *fname = strrchr(om->jpeg_path, '/');
                fname = fname ? fname + 1 : om->jpeg_path;
                inbound_msg_t *im = calloc(1, sizeof(*im));
                if (im) {
                    im->channel_id = safe_strdup(om->channel_id);
                    im->author     = safe_strdup("me");
                    char note[160];
                    snprintf(note, sizeof(note), "[uploaded %s]%s%s", fname,
                             (om->text && *om->text) ? " " : "",
                             (om->text && *om->text) ? om->text : "");
                    im->content    = safe_strdup(note);
                    if (!im->channel_id || !im->author || !im->content) free_inbound(im);
                    else if (xQueueSend(s_inbound_q, &im, 0) != pdTRUE) free_inbound(im);
                }
            } else {
                ESP_LOGW(TAG, "attach upload failed: %s", esp_err_to_name(r));
            }
        } else {
            discord_message_t  msg    = { .channel_id = om->channel_id, .content = om->text };
            discord_message_t *result = NULL;
            esp_err_t          r      = discord_message_send(s_client, &msg, &result);
            if (r != ESP_OK) {
                ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(r));
            } else if (result) {
                const char *author  = (result->author && result->author->username)
                                          ? result->author->username : "me";
                const char *content = result->content ? result->content : om->text;
                echo_locally(om->channel_id, result->id, author, content);
            }
            if (result) discord_message_free(result);
        }

        free(om->channel_id);
        free(om->text);
        free(om->jpeg_path);
        free(om);
    }
}

esp_err_t discord_task_start(const config_t *cfg) {
    if (!cfg || !cfg->token) return ESP_ERR_INVALID_ARG;
    if (s_client) return ESP_OK;

    s_cfg        = cfg;
    s_inbound_q  = xQueueCreate(INBOUND_Q_LEN, sizeof(inbound_msg_t *));
    s_outbound_q = xQueueCreate(OUTBOUND_Q_LEN, sizeof(outbound_msg_t *));
    if (!s_inbound_q || !s_outbound_q) return ESP_ERR_NO_MEM;

    // esp-discord prepends "Bot " itself when building the Authorization
    // header (see managed_components/.../src/discord/private/_api.c). If the
    // user wrote "Bot ..." in discord.json we must strip it, otherwise the
    // REST API sees "Bot Bot <token>" and 401s. The gateway IDENTIFY path
    // ignores the prefix, which is why login appears to work either way.
    const char *raw = cfg->token;
    if (strncmp(raw, "Bot ", 4) == 0) raw += 4;
    while (*raw == ' ') raw++;

    discord_config_t dcfg = {
        .token   = (char *)raw,
        .intents = DISCORD_INTENT_GUILD_MESSAGES | DISCORD_INTENT_MESSAGE_CONTENT,
    };
    s_client = discord_create(&dcfg);
    if (!s_client) return ESP_FAIL;

    ESP_ERROR_CHECK(discord_register_events(s_client, DISCORD_EVENT_ANY, on_event, NULL));

    // 16 KB: outbound_task can call attach_upload_jpeg which pulls in
    // esp_http_client + mbedTLS + a 4 KB chunk buffer + msg-send paths.
    if (xTaskCreate(outbound_task, "discord_tx", 16384, NULL, 5, &s_outbound_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t r = discord_login(s_client);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "login failed: %s", esp_err_to_name(r));
        return r;
    }
    ESP_LOGI(TAG, "discord_login issued");
    return ESP_OK;
}

QueueHandle_t discord_task_inbound_queue(void) { return s_inbound_q; }

esp_err_t discord_task_post(outbound_msg_t *msg) {
    if (!msg || !s_outbound_q) return ESP_ERR_INVALID_ARG;
    if (xQueueSend(s_outbound_q, &msg, 0) != pdTRUE) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
