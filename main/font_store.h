#pragma once

#include "esp_err.h"

/* 将固定 fontdata 分区映射给两个 LVGL 中文字体。必须在 dv_init() 前调用。字库缺失或版本不对时返回错误，
 * 此时中文字体为空并回退到内置 ASCII 字体，设备仍可启动，需要 USB 完整刷机修复。 */
esp_err_t font_store_init(void);
