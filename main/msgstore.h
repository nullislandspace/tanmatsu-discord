#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Per-channel JSONL message persistence in /sd/discord/.
// File layout: one JSON object per line:
//   {"id":"<snowflake>","author":"<name>","content":"<text>"}
// IDs are monotonic so the last line is always the newest message.

typedef struct {
    char *id;
    char *author;
    char *content;
} stored_msg_t;

esp_err_t msgstore_init(void);  // creates /sd/discord/ if missing

// Append a message if its id is strictly greater than the file's current
// last_id (dedup against backfill/live overlap). Returns ESP_OK on write,
// ESP_ERR_INVALID_STATE if skipped as duplicate, error otherwise.
esp_err_t msgstore_append(const char *channel_id, const char *msg_id,
                          const char *author, const char *content);

// Read the snowflake id of the newest entry. Returns a heap string
// (caller frees), or NULL if the file is missing/empty.
char *msgstore_last_id(const char *channel_id);

// Load up to `max` most-recent entries, oldest-first. On success populates
// *out_msgs (caller frees each field and the array) and *out_count.
esp_err_t msgstore_load_tail(const char *channel_id, size_t max,
                             stored_msg_t **out_msgs, size_t *out_count);

void msgstore_free_tail(stored_msg_t *msgs, size_t count);
