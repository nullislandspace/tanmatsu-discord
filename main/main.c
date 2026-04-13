#include <stdio.h>
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/input.h"
#include "bsp/led.h"
#include "bsp/power.h"
#include "custom_certificates.h"
#include "config.h"
#include "fbdraw.h"
#include "sdcard.h"
#include "ui_core.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "hal/lcd_types.h"
#include "nvs_flash.h"
#include "pax_fonts.h"
#include "pax_gfx.h"
#include "pax_text.h"
#include "portmacro.h"
#include "wifi_connection.h"
#include "wifi_remote.h"

// Constants
static char const TAG[] = "main";

// Global variables
static size_t                       display_h_res        = 0;
static size_t                       display_v_res        = 0;
static lcd_color_rgb_pixel_format_t display_color_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
static lcd_rgb_data_endian_t        display_data_endian  = LCD_RGB_DATA_ENDIAN_LITTLE;
static pax_buf_t                    fb                   = {0};
static QueueHandle_t                input_event_queue    = NULL;
static config_t                     app_config           = {0};

#if defined(CONFIG_BSP_TARGET_KAMI)
// Temporary addition for supporting epaper devices (irrelevant for Tanmatsu)
static pax_col_t palette[] = {0xffffffff, 0xff000000, 0xffff0000};  // white, black, red
#endif

void blit(void) {
    bsp_display_blit(0, 0, display_h_res, display_v_res, pax_buf_get_pixels(&fb));
}

void app_main(void) {
    // Start the GPIO interrupt service
    gpio_install_isr_service(0);

    // Initialize the Non Volatile Storage partition
    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        res = nvs_flash_erase();
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS flash: %d", res);
            return;
        }
        res = nvs_flash_init();
    }
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS flash: %d", res);
        return;
    }

    // Initialize the Board Support Package
    const bsp_configuration_t bsp_configuration = {
        .display =
            {
                .requested_color_format = LCD_COLOR_PIXEL_FORMAT_RGB888,
                .num_fbs                = 1,
            },
    };
    res = bsp_device_initialize(&bsp_configuration);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BSP: %d", res);
        return;
    }

    // Get display parameters and rotation
    res = bsp_display_get_parameters(&display_h_res, &display_v_res, &display_color_format, &display_data_endian);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get display parameters: %d", res);
        return;
    }

    // Convert ESP-IDF color format into PAX buffer type
    pax_buf_type_t format = PAX_BUF_24_888RGB;
    switch (display_color_format) {
        case LCD_COLOR_PIXEL_FORMAT_RGB565:
            format = PAX_BUF_16_565RGB;
            break;
        case LCD_COLOR_PIXEL_FORMAT_RGB888:
            format = PAX_BUF_24_888RGB;
            break;
        default:
            break;
    }

    // Convert BSP display rotation format into PAX orientation type
    bsp_display_rotation_t display_rotation = bsp_display_get_default_rotation();
    pax_orientation_t orientation = PAX_O_UPRIGHT;
    switch (display_rotation) {
        case BSP_DISPLAY_ROTATION_90:
            orientation = PAX_O_ROT_CCW;
            break;
        case BSP_DISPLAY_ROTATION_180:
            orientation = PAX_O_ROT_HALF;
            break;
        case BSP_DISPLAY_ROTATION_270:
            orientation = PAX_O_ROT_CW;
            break;
        case BSP_DISPLAY_ROTATION_0:
        default:
            orientation = PAX_O_UPRIGHT;
            break;
    }

        // Initialize graphics stack
#if defined(CONFIG_BSP_TARGET_KAMI)
    // Temporary addition for supporting epaper devices (irrelevant for Tanmatsu)
    format = PAX_BUF_2_PAL;
#endif
    pax_buf_init(&fb, NULL, display_h_res, display_v_res, format);
    pax_buf_reversed(&fb, display_data_endian == LCD_RGB_DATA_ENDIAN_BIG);
#if defined(CONFIG_BSP_TARGET_KAMI)
    // Temporary addition for supporting epaper devices (irrelevant for Tanmatsu)
    fb.palette      = palette;
    fb.palette_size = sizeof(palette) / sizeof(pax_col_t);
#endif
    pax_buf_set_orientation(&fb, orientation);

#if defined(CONFIG_BSP_TARGET_KAMI)
#define BLACK 0
#define WHITE 1
#define RED   2
#else
#define BLACK 0xFF000000
#define WHITE 0xFFFFFFFF
#define RED   0xFFFF0000
#endif

    // Get input event queue from BSP
    ESP_ERROR_CHECK(bsp_input_get_queue(&input_event_queue));

    // LEDs
    bsp_led_set_pixel(0, 0xFF0000);  // Red
    bsp_led_set_pixel(1, 0x00FF00);  // Green
    bsp_led_set_pixel(2, 0x0000FF);  // Blue
    bsp_led_set_pixel(3, 0xFFFF00);  // Yellow
    bsp_led_set_pixel(4, 0x00FFFF);  // Magenta
    bsp_led_set_pixel(5, 0xFF00FF);  // Cyan
    bsp_led_send();                  // Send data to the coprocessor
    bsp_led_set_mode(false);         // Take control over all LEDs by disabling automatic mode

    // Mount SD card so /sd/discord.json can be read later
    pax_background(&fb, WHITE);
    fbdraw_hershey_string(&fb, 8, 32, 1.5f, BLACK, "Mounting SD card...");
    blit();
    if (sdcard_init() != ESP_OK) {
        pax_background(&fb, RED);
        fbdraw_hershey_string(&fb, 8, 32, 1.5f, WHITE, "SD card missing or unreadable.");
        fbdraw_hershey_string(&fb, 8, 72, 1.0f, WHITE, "Insert SD card with /discord.json and reboot.");
        blit();
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Load Discord configuration from SD card
    pax_background(&fb, WHITE);
    fbdraw_hershey_string(&fb, 8, 32, 1.5f, BLACK, "Loading /sd/discord.json...");
    blit();
    if (config_load("/sd/discord.json", &app_config) != ESP_OK) {
        pax_background(&fb, RED);
        fbdraw_hershey_string(&fb, 8, 32, 1.5f, WHITE, "Config missing or invalid.");
        fbdraw_hershey_string(&fb, 8, 72, 1.0f, WHITE, "Write /discord.json to the SD card.");
        fbdraw_hershey_string(&fb, 8, 102, 1.0f, WHITE, "See SETUP.md for the format.");
        blit();
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Start WiFi stack (if your app does not require WiFi or BLE you can remove this section)
    pax_background(&fb, WHITE);
    pax_draw_text(&fb, BLACK, pax_font_sky_mono, 16, 0, 0, "Connecting to radio...");
    blit();

    // Force the radio coprocessor off before bringing it up, in case it was
    // left in APPLICATION mode by a previously running app with a transfer
    // still in flight. Without this clean power-cycle the radio can come up
    // in an inconsistent state and cause crashes shortly after launch.
    // (Same pattern as tanmatsu-launcher/main/main.c:473.)
    bsp_power_set_radio_state(BSP_POWER_RADIO_STATE_OFF);
    vTaskDelay(pdMS_TO_TICKS(200));

    if (wifi_remote_initialize() == ESP_OK) {

        pax_background(&fb, WHITE);
        pax_draw_text(&fb, BLACK, pax_font_sky_mono, 16, 0, 0, "Starting WiFi stack...");
        blit();
        wifi_connection_init_stack();  // Start the Espressif WiFi stack

        pax_background(&fb, WHITE);
        pax_draw_text(&fb, BLACK, pax_font_sky_mono, 16, 0, 0, "Connecting to WiFi network...");
        blit();

        if (wifi_connect_try_all() == ESP_OK) {
            pax_background(&fb, WHITE);
            pax_draw_text(&fb, BLACK, pax_font_sky_mono, 16, 0, 0, "Succesfully connected to WiFi network");
            blit();
        } else {
            pax_background(&fb, RED);
            pax_draw_text(&fb, WHITE, pax_font_sky_mono, 16, 0, 0, "Failed to connect to WiFi network");
            blit();
        }
    } else {
        bsp_power_set_radio_state(BSP_POWER_RADIO_STATE_OFF);
        ESP_LOGE(TAG, "WiFi radio not responding, WiFi not available");
        pax_background(&fb, RED);
        pax_draw_text(&fb, WHITE, pax_font_sky_mono, 16, 0, 0, "WiFi unavailable");
        blit();
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    // Main section of the app

    // This example shows how to read from the BSP event queue to read input events

    // If you want to run something at an interval in this same main thread you can replace portMAX_DELAY with an amount
    // of ticks to wait, for example pdMS_TO_TICKS(1000)

    ui_run(&fb, &app_config, input_event_queue);
}
