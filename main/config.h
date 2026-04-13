#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct {
    char *guild_id;
    char *channel_id;
    char *name;
} channel_t;

typedef struct {
    char      *token;
    char      *default_channel;
    channel_t *channels;
    size_t     num_channels;
} config_t;

esp_err_t config_load(const char *path, config_t *out);
void      config_free(config_t *cfg);
