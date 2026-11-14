/* main/display_view.h
 * ST7735 0.96" 横屏，LVGL 控件 + esp_lcd SPI 输出。
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "robot_runtime.h"
#include "robot_ui_protocol.h"
#include "basketball_game.h"

#define DV_WIDTH  160
#define DV_HEIGHT 80

#define DV_BLACK   0x0000
#define DV_WHITE   0xFFFF
#define DV_RED     0xF800
#define DV_GREEN   0x07E0
#define DV_YELLOW  0xFFE0
#define DV_BLUE    0x001F
#define DV_GRAY    0x8410
#define DV_CYAN    0x07FF

esp_err_t dv_init(void);

/* 触发一次立即重绘；display_task 中的常规动画由 dv_process 驱动。 */
void dv_flush(void);
void dv_process(void);

/* 记录一次显示活动；若屏幕已熄灭则同时唤醒背光和面板。 */
void dv_activity(void);

void dv_render_status_bar(void);
void dv_render_main(void);
void dv_render_net(int state, const char *ip);
void dv_render_idle(void);

/* Called only by the display task; no LVGL access from radar/CDC tasks. */
void dv_render_robot(const robot_status_t *status, const robot_ui_t *view);

void dv_render_basketball(const basketball_view_t *game);
