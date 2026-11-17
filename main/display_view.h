#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esphook/app_screen.h"
#define DV_WIDTH 160
#define DV_HEIGHT 80
/* Board-only display port. Apps use esphook/app_ui.h. */
esp_err_t dv_init(void);
void dv_flush(void);
void dv_process(void);
void dv_activity(void);
bool dv_is_initialized(void);
/* Panel tuning (app_screen.h services forward here): safe from any task, applied by dv_process() on the display task. */
void dv_screen_read(app_screen_t *out);
bool dv_screen_apply(const app_screen_t *want, bool save);
bool dv_screen_raw(const uint8_t *bytes, size_t length);
