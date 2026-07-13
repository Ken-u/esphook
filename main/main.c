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

/* test_display_model.c */
void test_notify_increments(void);
void test_dismiss_decrements(void);
void test_dismiss_removes_when_zero(void);
void test_dismiss_nonexistent_noop(void);
void test_multi_session_same_client_sums(void);
void test_dismiss_one_session_only(void);
void test_multi_client_separate(void);
void test_current_set_on_notify(void);
void test_clear_current(void);
void test_dismiss_current_clears_current(void);
void test_overflow_drops_oldest(void);
void test_client_at_enumerates(void);

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

    RUN_TEST(test_notify_increments);
    RUN_TEST(test_dismiss_decrements);
    RUN_TEST(test_dismiss_removes_when_zero);
    RUN_TEST(test_dismiss_nonexistent_noop);
    RUN_TEST(test_multi_session_same_client_sums);
    RUN_TEST(test_dismiss_one_session_only);
    RUN_TEST(test_multi_client_separate);
    RUN_TEST(test_current_set_on_notify);
    RUN_TEST(test_clear_current);
    RUN_TEST(test_dismiss_current_clears_current);
    RUN_TEST(test_overflow_drops_oldest);
    RUN_TEST(test_client_at_enumerates);
    UNITY_END();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
