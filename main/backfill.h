#pragma once

#include "config.h"
#include "esp_err.h"

// Spawn the background backfill task. Call once at startup, after
// msgstore_init() and after WiFi is up.
esp_err_t backfill_init(const config_t *cfg, const char *bot_token);

// Trigger a sweep of all configured channels: fetch any messages newer
// than the per-channel last_seen_id (or the most recent 50 if the channel
// has no local history). Safe to call from a Discord event handler.
void backfill_kick(void);
