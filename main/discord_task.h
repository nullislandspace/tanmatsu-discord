#pragma once

#include "config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    char *channel_id;
    char *author;
    char *content;
} inbound_msg_t;

typedef struct {
    char *channel_id;
    char *text;
    char *jpeg_path;
} outbound_msg_t;

esp_err_t      discord_task_start(const config_t *cfg);
QueueHandle_t  discord_task_inbound_queue(void);
esp_err_t      discord_task_post(outbound_msg_t *msg);
