#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

static char const TAG[] = "config";

static char *dup_str(const cJSON *item) {
    if (!cJSON_IsString(item) || item->valuestring == NULL) return NULL;
    size_t len = strlen(item->valuestring);
    char  *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, item->valuestring, len + 1);
    return out;
}

static esp_err_t read_whole_file(const char *path, char **out_buf, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed", path);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 64 * 1024) {
        fclose(f);
        ESP_LOGE(TAG, "bad config size: %ld", size);
        return ESP_ERR_INVALID_SIZE;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        free(buf);
        return ESP_FAIL;
    }
    buf[size] = '\0';
    *out_buf  = buf;
    *out_len  = (size_t)size;
    return ESP_OK;
}

esp_err_t config_load(const char *path, config_t *out) {
    if (!path || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    char  *raw = NULL;
    size_t len = 0;
    esp_err_t r = read_whole_file(path, &raw, &len);
    if (r != ESP_OK) return r;

    cJSON *root = cJSON_ParseWithLength(raw, len);
    free(raw);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_ERR_INVALID_ARG;
    }

    out->token           = dup_str(cJSON_GetObjectItem(root, "token"));
    out->default_channel = dup_str(cJSON_GetObjectItem(root, "default_channel"));
    if (!out->token) {
        ESP_LOGE(TAG, "missing 'token'");
        cJSON_Delete(root);
        config_free(out);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *chans = cJSON_GetObjectItem(root, "channels");
    if (!cJSON_IsArray(chans)) {
        ESP_LOGE(TAG, "missing 'channels' array");
        cJSON_Delete(root);
        config_free(out);
        return ESP_ERR_INVALID_ARG;
    }

    int n = cJSON_GetArraySize(chans);
    if (n <= 0) {
        cJSON_Delete(root);
        return ESP_OK;
    }
    out->channels = calloc((size_t)n, sizeof(channel_t));
    if (!out->channels) {
        cJSON_Delete(root);
        config_free(out);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_GetArrayItem(chans, i);
        out->channels[i].guild_id   = dup_str(cJSON_GetObjectItem(e, "guild_id"));
        out->channels[i].channel_id = dup_str(cJSON_GetObjectItem(e, "channel_id"));
        out->channels[i].name       = dup_str(cJSON_GetObjectItem(e, "name"));
        if (!out->channels[i].channel_id) {
            ESP_LOGE(TAG, "channel %d missing channel_id", i);
            cJSON_Delete(root);
            config_free(out);
            return ESP_ERR_INVALID_ARG;
        }
    }
    out->num_channels = (size_t)n;
    cJSON_Delete(root);
    ESP_LOGI(TAG, "loaded %zu channels", out->num_channels);
    return ESP_OK;
}

void config_free(config_t *cfg) {
    if (!cfg) return;
    free(cfg->token);
    free(cfg->default_channel);
    for (size_t i = 0; i < cfg->num_channels; ++i) {
        free(cfg->channels[i].guild_id);
        free(cfg->channels[i].channel_id);
        free(cfg->channels[i].name);
    }
    free(cfg->channels);
    memset(cfg, 0, sizeof(*cfg));
}
