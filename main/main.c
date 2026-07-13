/* main/main.c */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb_cdc.h"
#include "config_store.h"
#include "display_model.h"
#include "display_view.h"
#include "net_wifi.h"
#include "beeper.h"
#include "events.h"
#include "http_server.h"
#include "input_task.h"
#include "display_task.h"
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

static bool s_http_started = false;

static void on_wifi_state(int state, const char *ip)
{
    ESP_LOGI(TAG, "wifi state=%d ip=%s", state, ip ? ip : "");
    display_event_t e = {0};
    e.kind = EVT_NET_STATE;
    e.net_state = state;
    strncpy(e.ip, ip ? ip : "", sizeof e.ip - 1);
    xQueueSend(display_q, &e, 0);
    if (state == 2 && !s_http_started) {
        http_start();
        s_http_started = true;
        cdc_write("wifi up, http started");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "supermini-aihook starting");
    ESP_ERROR_CHECK(config_store_init());

    display_q = xQueueCreate(16, sizeof(display_event_t));
    beeper_q  = xQueueCreate(8, sizeof(beep_kind_t));

    /* USB Serial/JTAG CDC（配网 + 日志） */
    ESP_ERROR_CHECK(cdc_init(input_cdc_handler));
    cdc_write("supermini-aihook ready (try: help)");

    /* 显示 */
    dm_init();
    dv_init();
    dv_render_net(0, NULL);
    dv_flush();

    /* 蜂鸣器 */
    beeper_init();

    /* 显示任务（消费 display_q，统一改 model） */
    display_task_start();

    /* 输入任务（按键 + CDC 命令已在 handler） */
    input_start();

    /* 尝试连 Wi-Fi（有凭证则连，连上后启 http） */
    wifi_set_state_cb(on_wifi_state);
    wifi_start();

    /* unity 自测（开发期；发布可关） */
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
