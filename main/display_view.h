/* main/display_view.h
 * ST7735 0.96" 80x160 横屏，走 esp_lcd 框架(panel_io + ST7789 驱动复用)。
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

/* 大字号：2=10x14，3=15x21。 */
#define DV_SIZE_TITLE 3
#define DV_SIZE_BODY  3
#define DV_SIZE_BAR   1
#define DV_SIZE_NET   1

esp_err_t dv_init(void);

void dv_clear(void);
void dv_fill_rect(int x, int y, int w, int h, uint16_t color);
void dv_draw_text(int x, int y, const char *s, uint16_t color, int size);
void dv_flush(void);

void dv_render_status_bar(void);
void dv_render_main(void);
void dv_render_net(int state, const char *ip);
void dv_render_idle(void);
void dv_tick_scroll(void);
