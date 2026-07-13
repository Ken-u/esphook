/* main/usb_cdc.h
 * C3 用内置 USB Serial/JTAG 作 CDC 串口（配网命令 + 日志）。
 * 不用 tinyusb（C3 无 USB OTG，tinyusb 不支持 C3）。
 */
#pragma once
#include "esp_err.h"
#include "esp_log.h"

typedef void (*cdc_rx_line_cb_t)(const char *line);

esp_err_t cdc_init(cdc_rx_line_cb_t cb);
esp_err_t cdc_write(const char *str);
void cdc_log(esp_log_level_t level, const char *fmt, ...);
