#include "attach_upload.h"
#include "esp_log.h"

static char const TAG[] = "attach_upload";

esp_err_t attach_upload_jpeg(const char *bot_token, const char *channel_id, const char *jpeg_path,
                             const char *caption) {
    (void)bot_token; (void)channel_id; (void)jpeg_path; (void)caption;
    ESP_LOGW(TAG, "attach_upload_jpeg: stub");
    return ESP_ERR_NOT_SUPPORTED;
}
