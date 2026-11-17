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
/* A 2 px progress line for holds. `line` is any rectangle the app created (full width, hidden by default). With
 * `confirm` it fills gold during the 500 ms confirmation; the exit hold (see app_set_exit_hold) then fills a red one
 * up to `exit_ms`. Without `confirm` nothing shows until 600 ms, so ordinary presses never flicker. */
void app_ui_hold_line(lv_obj_t *line, unsigned hold_ms, bool confirm, unsigned exit_ms);
/* One framework-owned RGB565 scratch canvas of APP_CANVAS_W x APP_CANVAS_H, reused between applications.
 * Radar and stock use its first 48 rows, games use all of it. Repaint before invalidating the image;
 * never retain across an app transition. */
#define APP_CANVAS_W 160
#define APP_CANVAS_H 64
uint16_t *app_ui_pixels(void);
void app_ui_plot_refresh(lv_obj_t *image, const lv_image_dsc_t *descriptor);
/* The usual way to show the shared canvas: an image object at (x, y) under `parent` that displays the first `h` rows
 * of `w` pixels of app_ui_pixels() (w <= APP_CANVAS_W, h <= APP_CANVAS_H; 0 or too large means the whole canvas).
 * The framework owns the image descriptor, so the app keeps only the returned object - no lv_image_dsc_t in its
 * state. Create it once in `init`; after repainting the pixels call app_ui_canvas_refresh(). */
lv_obj_t *app_ui_canvas(lv_obj_t *parent, int x, int y, int w, int h);
void app_ui_canvas_refresh(lv_obj_t *canvas);
/* The 16 px status strip under the canvas (y = APP_CANVAS_H .. APP_HEIGHT): background `bg` and, unless `rule` is
 * APP_UI_NO_RULE, a 1 px line on top of it in `rule`. Returns the strip; put its labels inside it. */
#define APP_UI_NO_RULE 0xFFFFFFFFU
lv_obj_t *app_ui_band(lv_obj_t *parent, uint32_t bg, uint32_t rule);
/* The hold progress line app_ui_hold_line() draws into: 2 px high, zero wide until a hold is under way, at row `y`
 * of `parent` in colour `rgb` (the framework recolours it for confirmation and exit holds). */
lv_obj_t *app_ui_hold_bar(lv_obj_t *parent, int y, uint32_t rgb);
