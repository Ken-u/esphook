/* main/main.c */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_cdc.h"
#include "config_store.h"
#include "unity.h"

static const char *TAG = "main";

/* test_config_store.c */
void test_keymap_default(void);
void test_keymap_set_get(void);
void test_keymap_partial_set(void);
void test_keymap_index_out_of_range(void);
void test_display_name_missing(void);
void test_display_name_set_get(void);
void test_display_name_delete(void);
void test_wifi_roundtrip(void);

static void on_cdc_line(const char *line)
{
    ESP_LOGI(TAG, "CDC line: %s", line);
    cdc_write("OK echo:");
}

void app_main(void)
{
    ESP_LOGI(TAG, "supermini-aihook starting");
    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(cdc_init(on_cdc_line));
    cdc_write("supermini-aihook CDC ready");

    UNITY_BEGIN();
    RUN_TEST(test_keymap_default);
    RUN_TEST(test_keymap_partial_set);
    RUN_TEST(test_keymap_index_out_of_range);
    RUN_TEST(test_display_name_missing);
    RUN_TEST(test_display_name_set_get);
    RUN_TEST(test_display_name_delete);
    RUN_TEST(test_wifi_roundtrip);
    RUN_TEST(test_keymap_set_get);
    UNITY_END();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
