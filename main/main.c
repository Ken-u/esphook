/* main/main.c */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_cdc.h"

static const char *TAG = "main";

static void on_cdc_line(const char *line)
{
    ESP_LOGI(TAG, "CDC line: %s", line);
    cdc_write("OK echo:");
}

void app_main(void)
{
    ESP_LOGI(TAG, "supermini-aihook starting");
    ESP_ERROR_CHECK(cdc_init(on_cdc_line));
    cdc_write("supermini-aihook CDC ready");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
