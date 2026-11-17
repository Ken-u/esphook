#include "app_ui.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <string.h>
static uint16_t s_pixels[APP_CANVAS_W * APP_CANVAS_H];
uint16_t *app_ui_pixels(void) { return s_pixels; }
lv_color_t app_ui_color(uint32_t rgb) { return lv_color_hex(rgb); }
void app_ui_plain(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj); lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN); lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN); lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
}
lv_obj_t *app_ui_layer(lv_obj_t *parent)
{
    lv_obj_t *obj = app_ui_rect(parent, 0, 0, APP_WIDTH, APP_HEIGHT, APP_COLOR_BG, 0);
    return obj;
}
lv_obj_t *app_ui_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent); app_ui_plain(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, app_ui_color(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN); return obj;
}
lv_obj_t *app_ui_label(lv_obj_t *parent, const lv_font_t *font, int x, int y, int w, int h, lv_label_long_mode_t mode)
{
    lv_obj_t *obj = lv_label_create(parent); app_ui_plain(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, app_ui_color(APP_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(obj, lv_anim_speed_clamped(18, 300, 10000), LV_PART_MAIN);
    lv_label_set_long_mode(obj, mode); lv_label_set_text(obj, ""); return obj;
}
void app_ui_text(lv_obj_t *obj, const char *text)
{ const char *s = text ? text : ""; if (strcmp(lv_label_get_text(obj), s)) lv_label_set_text(obj, s); }
void app_ui_hide(lv_obj_t *obj, bool hide)
{ if (hide) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN); }
void app_ui_center(lv_obj_t *obj) { lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN); }
lv_obj_t *app_ui_ring(lv_obj_t *parent, int x, int y, int size)
{
    lv_obj_t *ring = lv_arc_create(parent); lv_obj_set_size(ring, size, size); lv_obj_set_pos(ring, x, y);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB); lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(ring, 0, 360); lv_arc_set_rotation(ring, 270); lv_arc_set_range(ring, 0, APP_CONFIRM_MS);
    lv_obj_set_style_arc_width(ring, 2, LV_PART_MAIN); lv_obj_set_style_arc_color(ring, app_ui_color(0x513663), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, app_ui_color(APP_COLOR_ACCENT), LV_PART_INDICATOR); return ring;
}
void app_ui_progress(lv_obj_t *ring, unsigned ms) { lv_arc_set_value(ring, ms > APP_CONFIRM_MS ? APP_CONFIRM_MS : ms); }
void app_ui_plot_refresh(lv_obj_t *image, const lv_image_dsc_t *descriptor)
{ lv_image_cache_drop(descriptor); lv_obj_invalidate(image); }
