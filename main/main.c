/* main/main.c */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "supermini-aihook starting");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
