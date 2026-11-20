#include "esphook/app.h"
#include "esphook/app_command.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include "esphook/radar_view.h"
#include <stdio.h>
#include <string.h>

typedef struct { lv_obj_t *plot, *heading, *labels[3]; } state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "radar state too large");
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    s->heading = app_ui_label(ctx->root, &aihook_font_12, 2, 0, 106, 15, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_t *hint = app_ui_label(ctx->root, &aihook_font_12, 110, 0, 50, 15, LV_LABEL_LONG_MODE_CLIP);
    app_ui_text(hint, "长按切换");
    for (unsigned i = 0; i < LD2450_TARGETS; ++i) {
        s->labels[i] = app_ui_label(ctx->root, LV_FONT_DEFAULT, 109, 17 + i * 21, 51, 20, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_letter_space(s->labels[i], -1, LV_PART_MAIN);
        uint16_t color = radar_target_color(i);
        lv_obj_set_style_text_color(s->labels[i], lv_color_make((color>>11)*255/31,((color>>5)&63)*255/63,(color&31)*255/31), LV_PART_MAIN);
    }
    s->plot = app_ui_canvas(ctx->root, 0, 16, RADAR_VIEW_WIDTH, RADAR_VIEW_HEIGHT);
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    app_radar_t radar;
    app_radar_read(&radar);
    unsigned count = 0;
    for (unsigned i = 0; i < LD2450_TARGETS; ++i) {
        const ld2450_target_t *target = &radar.frame.targets[i];
        char value[24];
        if (radar.online && target->valid) {
            ++count;
            /* Mirror X so the viewer's right is positive. */
            snprintf(value, sizeof value, "%u%+d\nY%d", i + 1, -(int)target->x_mm, (int)target->y_mm);
        } else {
            snprintf(value, sizeof value, "%u --\nY --", i + 1);
        }
        app_ui_text(s->labels[i], value);
    }
    char text[48];
    if (!radar.available) snprintf(text, sizeof text, "雷达不可用");
    else if (!radar.online) snprintf(text, sizeof text, "NO DATA %u mm", radar.range_mm);
    else snprintf(text, sizeof text, "R%u %u X/Y mm", count, radar.range_mm);
    app_ui_text(s->heading, text);
    radar_view_draw(app_ui_pixels(), &radar.frame, radar.online, radar.range_mm, (unsigned)ctx->now_ms);
    app_ui_canvas_refresh(s->plot);
}
static void leave(app_context_t *ctx) { state_t *s=ctx->state; app_ui_canvas_refresh(s->plot); }
static void event(app_context_t *ctx, const app_event_t *event)
{
    if (event->kind == APP_EVENT_SHORT) {
        app_menu(ctx);
    } else if (event->kind == APP_EVENT_CONFIRM) {
        app_radar_t radar;
        app_radar_read(&radar);
        unsigned range = RADAR_VIEW_MAX_RANGE_MM;
        if (radar.range_mm > 3000) range = 3000;
        else if (radar.range_mm > 1500) range = 1500;
        (void)app_radar_set_view_range(range);
    }
}
static void icon(void *parent,int x,int y,bool selected)
{
    /* The machine at the top, its beam opening downwards towards the person in front of it. */
    uint32_t color=selected?APP_COLOR_ACCENT:0x47E6AE;
    for(int k=0;k<3;++k) {
        lv_obj_t *arc=lv_arc_create(parent); lv_obj_set_size(arc,28-k*8,28-k*8); lv_obj_set_pos(arc,x+k*4,y-9+k*4);
        lv_obj_remove_style(arc,NULL,LV_PART_KNOB); lv_obj_clear_flag(arc,LV_OBJ_FLAG_CLICKABLE);
        lv_arc_set_bg_angles(arc,0,180); lv_arc_set_range(arc,0,100); lv_arc_set_value(arc,0);
        lv_obj_set_style_arc_color(arc,app_ui_color(color),LV_PART_MAIN); lv_obj_set_style_arc_width(arc,2,LV_PART_MAIN);
    }
    app_ui_rect(parent,x+13,y+5,2,15,color,0);
    app_ui_rect(parent,x+8,y+1,12,5,0xC8D2E0,2);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot radar")) return false;
    char error[96]; const char *json="{\"v\":1,\"mode\":\"radar\"}";
    (void)app_display_update(json,strlen(json),error,sizeof error);
    (void)app_command_reply(app_command_open("radar") ? "OK radar queued" : "ERR app queue full");
    return true;
}
const app_descriptor_t esphook_app_radar={.api_version=APP_API_VERSION,.id="radar",.title="雷达",
    .flags=APP_STANDBY,.refresh_ms=80,.state_size=sizeof(state_t),.on_enter=enter,.on_leave=leave,.on_event=event,.render=render,.icon=icon,.command=command};
