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
#include "device_link.h"
#include "font_store.h"
#include "esp_ota_ops.h"

static const char *TAG = "main";

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
    ESP_ERROR_CHECK(font_store_init());
    dv_init();
    dv_render_net(0, NULL);
    dv_flush();

    /* 蜂鸣器 */
    beeper_init();

    /* 显示任务（消费 display_q，统一改 model） */
    display_task_start();

    /* 输入任务（按键 + CDC 命令已在 handler） */
    input_start();

    /* 主机反向连接（无配置时任务休眠，不影响旧直连模式）。 */
    device_link_start();

    /* 尝试连 Wi-Fi（有凭证则连，连上后启 http） */
    wifi_set_state_cb(on_wifi_state);
    wifi_start();

    /* OTA 回滚模式下，初始化完成后确认新程序；若启动前崩溃则保留旧槽位。 */
    esp_err_t ota_state = esp_ota_mark_app_valid_cancel_rollback();
    if (ota_state != ESP_OK && ota_state != ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
        ESP_LOGW(TAG, "could not confirm OTA app: %s", esp_err_to_name(ota_state));
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
