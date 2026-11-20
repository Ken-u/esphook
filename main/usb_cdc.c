/* main/usb_cdc.c
 * 用 IDF 的 USB Serial/JTAG 驱动（esp_driver_usb_serial_jtag 组件）。
 * 行缓冲：遇 \n 触发回调。日志带 [I]/[E]/[W] 前缀。
 */
#include "usb_cdc.h"
#include "usb_cdc_tx.h"
#include "sdkconfig.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include "robot_ui_protocol.h"
#include "freertos/task.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* Each chunk needs this much free TX ring; a chunk that cannot be queued within
 * CDC_TX_WAIT_MS means the host stopped reading, so the frame is dropped. */
#define CDC_TX_CHUNK 256
#define CDC_TX_WAIT_MS 100

static const char *TAG = "cdc";
static cdc_rx_line_cb_t s_rx_cb = NULL;
static char s_linebuf[ROBOT_UI_JSON_MAX + 8];
static size_t s_linepos = 0;
static bool s_line_overflow;
static SemaphoreHandle_t s_tx_lock;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_tx_drops;

static void rx_task(void *arg)
{
    uint8_t buf[64];
    while (1) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n <= 0) continue;
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\0') { s_line_overflow = true; continue; }
            if (c == '\n' || c == '\r') {
                if (s_line_overflow) {
                    cdc_write("ERR command too long or invalid byte");
                } else if (s_linepos > 0) {
                    s_linebuf[s_linepos] = '\0';
                    if (s_rx_cb) s_rx_cb(s_linebuf);
                }
                s_linepos = 0;
                s_line_overflow = false;
            } else if (s_linepos < sizeof(s_linebuf) - 1) {
                s_linebuf[s_linepos++] = c;
            } else {
                s_line_overflow = true;
            }
        }
    }
}

esp_err_t cdc_init(cdc_rx_line_cb_t cb)
{
    s_tx_lock = xSemaphoreCreateMutex();
    if (!s_tx_lock) return ESP_ERR_NO_MEM;
    s_rx_cb = cb;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    /* Every build answers `app` requests, so the TX ring is not robot-only. */
    cfg.tx_buffer_size = CDC_TX_BUFFER_BYTES;
    /* A complete 1024-byte JSON frame must fit while other tasks render. */
    cfg.rx_buffer_size = 2048;
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    xTaskCreate(rx_task, "cdc_rx", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "USB Serial/JTAG CDC ready");
    return ESP_OK;
}

static int tx_sink(void *ctx, const char *data, size_t len)
{
    (void)ctx;
    /* usb_serial_jtag_write_bytes() returns 0, not an error code, when the TX
     * ring cannot take the bytes within the timeout. */
    int n = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(CDC_TX_WAIT_MS));
    return n > 0 ? n : 0;
}

static void note_drop(size_t len)
{
    portENTER_CRITICAL(&s_stats_lock);
    uint32_t drops = ++s_tx_drops;
    portEXIT_CRITICAL(&s_stats_lock);
    /* Log on powers of two only; with no host attached every line is dropped. */
    if ((drops & (drops - 1)) == 0)
        ESP_LOGW(TAG, "USB TX dropped a %u-byte line (%u dropped since boot); host is not reading", (unsigned)len, (unsigned)drops);
}

uint32_t cdc_tx_drops(void)
{
    portENTER_CRITICAL(&s_stats_lock);
    uint32_t drops = s_tx_drops;
    portEXIT_CRITICAL(&s_stats_lock);
    return drops;
}

esp_err_t cdc_write(const char *str)
{
    if (!str) return ESP_ERR_INVALID_ARG;
    if (!s_tx_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        note_drop(strlen(str));
        return ESP_ERR_TIMEOUT;
    }
    bool sent = cdc_tx_frame(str, CDC_TX_CHUNK, tx_sink, NULL);
    if (sent) (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
    else note_drop(strlen(str));
    xSemaphoreGive(s_tx_lock);
    return sent ? ESP_OK : ESP_ERR_TIMEOUT;
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
