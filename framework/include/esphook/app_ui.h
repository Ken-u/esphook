#pragma once
#include "app.h"
#include "lvgl.h"
#define APP_WIDTH 160
#define APP_HEIGHT 80
#define APP_COLOR_BG 0x071018
#define APP_COLOR_TEXT 0xEDF4FB
#define APP_COLOR_MUTED 0x8A8196
#define APP_COLOR_ACCENT 0xFDB927
extern const lv_font_t aihook_font_12, aihook_font_16;
lv_color_t app_ui_color(uint32_t rgb);
void app_ui_plain(lv_obj_t *obj);
lv_obj_t *app_ui_layer(lv_obj_t *parent);
lv_obj_t *app_ui_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb, int radius);
lv_obj_t *app_ui_label(lv_obj_t *parent, const lv_font_t *font, int x, int y, int w, int h, lv_label_long_mode_t mode);
void app_ui_text(lv_obj_t *obj, const char *text);
void app_ui_hide(lv_obj_t *obj, bool hide);
void app_ui_center(lv_obj_t *obj);
lv_obj_t *app_ui_ring(lv_obj_t *parent, int x, int y, int size);
void app_ui_progress(lv_obj_t *ring, unsigned ms);
/* One framework-owned 160x48 RGB565 scratch canvas, reused between applications.
 * Repaint before invalidating the image; never retain across an app transition. */
uint16_t *app_ui_pixels(void);
void app_ui_plot_refresh(lv_obj_t *image, const lv_image_dsc_t *descriptor);
