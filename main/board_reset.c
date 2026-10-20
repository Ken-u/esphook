#include "board_reset.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static SemaphoreHandle_t s_lock;
static int64_t s_next_allowed_us;

esp_err_t board_reset_init(void)
{
    if (s_lock) return ESP_OK;
    /* Set the output latch before enabling the driver: no startup pulse.
       An external gate pulldown is still required during reset/boot/OTA. */
    esp_err_t err = gpio_set_level(BOARD_RESET_GPIO, 0);
    if (err != ESP_OK) return err;
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&config);
    if (err != ESP_OK) return err;
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t board_reset_press(void)
{
    if (!s_lock || xSemaphoreTake(s_lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (esp_timer_get_time() < s_next_allowed_us) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = gpio_set_level(BOARD_RESET_GPIO, 1);
    if (err == ESP_OK) {
        /* Release depends only on this local delay, never on a network reply.
           One extra tick ensures at least the requested pulse duration. */
        vTaskDelay(pdMS_TO_TICKS(BOARD_RESET_PULSE_MS) + 1);
    }
    esp_err_t release_err = gpio_set_level(BOARD_RESET_GPIO, 0);
    s_next_allowed_us = esp_timer_get_time() + BOARD_RESET_COOLDOWN_MS * 1000LL;
    xSemaphoreGive(s_lock);
    return release_err != ESP_OK ? release_err : err;
}
