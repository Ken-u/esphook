#pragma once

#include "esp_err.h"

/* 将固定 fontdata 分区映射给两个 LVGL 中文字体。必须在 dv_init() 前调用。 */
esp_err_t font_store_init(void);
