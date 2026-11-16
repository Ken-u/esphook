#pragma once
#include "esp_err.h"
#include <stdbool.h>
#define DV_WIDTH 160
#define DV_HEIGHT 80
/* Board-only display port. Apps use esphook/app_ui.h. */
esp_err_t dv_init(void);
void dv_flush(void);
void dv_process(void);
void dv_activity(void);
bool dv_is_initialized(void);
