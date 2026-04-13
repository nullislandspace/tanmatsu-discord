#pragma once

#include "esp_err.h"

esp_err_t attach_upload_jpeg(const char *bot_token, const char *channel_id, const char *jpeg_path,
                             const char *caption);
