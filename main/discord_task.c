#include "discord_task.h"
#include "esp_log.h"

static char const TAG[] = "discord_task";

esp_err_t discord_task_start(const config_t *cfg) {
    (void)cfg;
    ESP_LOGW(TAG, "discord_task_start: stub");
    return ESP_ERR_NOT_SUPPORTED;
}

QueueHandle_t discord_task_inbound_queue(void) { return NULL; }

esp_err_t discord_task_post(outbound_msg_t *msg) {
    (void)msg;
    return ESP_ERR_NOT_SUPPORTED;
}
