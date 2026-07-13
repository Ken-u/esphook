/* main/usb_cdc.c
 * 用 IDF 的 USB Serial/JTAG 驱动（esp_driver_usb_serial_jtag 组件）。
 * 行缓冲：遇 \n 触发回调。日志带 [I]/[E]/[W] 前缀。
 */
#include "usb_cdc.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static const char *TAG = "cdc";
static cdc_rx_line_cb_t s_rx_cb = NULL;
static char s_linebuf[128];
static size_t s_linepos = 0;

static void rx_task(void *arg)
{
    uint8_t buf[64];
    while (1) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n <= 0) continue;
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (s_linepos > 0) {
                    s_linebuf[s_linepos] = '\0';
                    if (s_rx_cb) s_rx_cb(s_linebuf);
                    s_linepos = 0;
                }
            } else if (s_linepos < sizeof(s_linebuf) - 1) {
                s_linebuf[s_linepos++] = c;
            }
        }
    }
}

esp_err_t cdc_init(cdc_rx_line_cb_t cb)
{
    s_rx_cb = cb;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    xTaskCreate(rx_task, "cdc_rx", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "USB Serial/JTAG CDC ready");
    return ESP_OK;
}

esp_err_t cdc_write(const char *str)
{
    size_t len = strlen(str);
    usb_serial_jtag_write_bytes(str, len, pdMS_TO_TICKS(100));
    if (len == 0 || str[len - 1] != '\n') {
        usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(100));
    }
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
    return ESP_OK;
}

void cdc_log(esp_log_level_t level, const char *fmt, ...)
{
    char buf[160];
    const char *prefix = "[I] ";
    if (level == ESP_LOG_ERROR) prefix = "[E] ";
    else if (level == ESP_LOG_WARN) prefix = "[W] ";
    int n = snprintf(buf, sizeof(buf), "%s", prefix);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
    va_end(ap);
    cdc_write(buf);
}
