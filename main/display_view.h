/* main/display_view.h
 * ST7735 0.96" 160x80 横屏驱动 + 布局渲染。
 * 引脚沿用 esp32c3-test.ino 验证过的：CS=0/DC=3/RST=2/SCL=4/SDA=5/BLK=1
 */
#pragma once
#include "esp_err.h"

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

/* 基础原语 */
void dv_clear(void);
void dv_fill_rect(int x, int y, int w, int h, uint16_t color);
/* size=1 为 6x8 像素/字符；size=2 为 12x16 */
void dv_draw_text(int x, int y, const char *s, uint16_t color, int size);
void dv_flush(void);

/* 布局渲染：消费 display_model + config_store 当前状态 */
void dv_render_status_bar(void);
void dv_render_main(void);
void dv_render_net(int state, const char *ip);  /* 0=no cfg 1=connecting 2=connected */
void dv_render_idle(void);
void dv_tick_scroll(void);  /* 周期调，推进红点栏滚动 */
