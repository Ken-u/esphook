/* main/usb_cdc.h
 * C3 用内置 USB Serial/JTAG 作 CDC 串口（配网命令 + 日志）。
 * 不用 tinyusb（C3 无 USB OTG，tinyusb 不支持 C3）。
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"

/* IDF's default TX ring is 256 bytes and a single write larger than the ring
 * can never succeed. The app list/hardware acks are 230..500 bytes, so size
 * the ring for a full 1024-byte JSON frame plus headroom. tools/check-apps
 * keeps the worst-case `list` ack inside this budget. */
#define CDC_TX_BUFFER_BYTES 2048

typedef void (*cdc_rx_line_cb_t)(const char *line);

esp_err_t cdc_init(cdc_rx_line_cb_t cb);
/* Queue one line (a '\n' is appended when missing). Returns ESP_OK only when
 * the whole line reached the TX ring; ESP_ERR_TIMEOUT when it was dropped. */
esp_err_t cdc_write(const char *str);
void cdc_log(esp_log_level_t level, const char *fmt, ...);
/* Lines dropped since boot because the host did not drain the TX ring. */
uint32_t cdc_tx_drops(void);
