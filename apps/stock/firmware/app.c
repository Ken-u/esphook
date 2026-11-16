#include "esphook/app.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "remote_game.h"
#include "stock_transport.h"
typedef struct {
    lv_obj_t *chart, *header, *footer, *panel, *title, *profit, *detail, *hint;
    lv_image_dsc_t image;
    remote_game_view_t view;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "stock state too large");
static void enter(app_context_t *ctx) { stock_transport_event("launch", ctx->now_ms); }
static void leave(app_context_t *ctx)
{
    state_t *s=ctx->state; stock_transport_event("menu", ctx->now_ms);
    if(s->chart) app_ui_plot_refresh(s->chart,&s->image);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    if (e->kind == APP_EVENT_DOWN) stock_transport_event("down", e->ms);
    if (e->kind == APP_EVENT_CONFIRM) stock_transport_event("hold", e->ms);
    if (e->kind == APP_EVENT_UP) stock_transport_event("up", e->ms);
    if (e->kind == APP_EVENT_SHORT) { state_t *s=ctx->state;
        stock_transport_view(ctx->now_ms, &s->view);
        if (!s->view.active || s->view.offline) app_menu(ctx); }
}
static void render(app_context_t *ctx)
{
    state_t *s=ctx->state; stock_transport_view(ctx->now_ms, &s->view);
    remote_game_view_t *v=&s->view;
    v->held_ms=app_hold_ms(ctx);
    if (!s->chart) {
        s->header = app_ui_label(ctx->root, &aihook_font_12, 2, 0, 156, 13, LV_LABEL_LONG_MODE_CLIP);
        s->image.header.magic = LV_IMAGE_HEADER_MAGIC;
        s->image.header.cf = LV_COLOR_FORMAT_RGB565;
        s->image.header.w = 160; s->image.header.h = 48; s->image.header.stride = 320;
        s->image.data_size = 160 * 48 * 2; s->image.data = (const uint8_t *)app_ui_pixels();
        s->chart = lv_image_create(ctx->root);
        lv_obj_set_pos(s->chart, 0, 15); lv_image_set_src(s->chart, &s->image);
        s->footer = app_ui_label(ctx->root, &aihook_font_12, 2, 66, 156, 14, LV_LABEL_LONG_MODE_CLIP);
        s->panel = app_ui_layer(ctx->root);
        s->title = app_ui_label(s->panel, &aihook_font_12, 2, 0, 156, 16, LV_LABEL_LONG_MODE_CLIP);
        s->profit = app_ui_label(s->panel, &aihook_font_16, 2, 19, 156, 22, LV_LABEL_LONG_MODE_CLIP);
        s->detail = app_ui_label(s->panel, &aihook_font_12, 2, 43, 156, 16, LV_LABEL_LONG_MODE_CLIP);
        s->hint = app_ui_label(s->panel, &aihook_font_12, 2, 65, 156, 15, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(s->title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(s->profit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(s->detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(s->hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    char text[120];
    bool overlay = !v->active || v->offline || v->phase != REMOTE_PLAY;
    app_ui_hide(s->panel, !overlay);
    if (overlay) {
        app_ui_text(s->title, !v->active ? "等待PC连接" : v->offline ? "PC 已断开" : v->phase == REMOTE_LOADING ? "加载下一天..." : "做T结束");
        snprintf(text, sizeof text, "收益 %+.2f%%", v->pct_bps / 100.0);
        app_ui_text(s->profit, v->active ? text : "做T模拟");
        lv_obj_set_style_text_color(s->profit, app_ui_color(v->pct_bps >= 0 ? 0xFF6363 : 0x47E6AE), LV_PART_MAIN);
        snprintf(text, sizeof text, "%+.2f元  交易%u次", v->pnl_cents / 100.0, v->trades);
        app_ui_text(s->detail, v->active ? text : "运行 stock-t.sh");
        app_ui_text(s->hint, !v->active || v->offline ? "短按返回菜单" : "长按继续 / 短按退出");
    } else {
        int32_t price = v->count ? v->bars[v->count - 1].close : 0;
        snprintf(text, sizeof text, "%s %s %.2f", v->code, v->clock, price / 100.0);
        app_ui_text(s->header, text);
        snprintf(text, sizeof text, "%s %+.2f%%  30/60", v->holding ? "持仓" : "空仓", v->pct_bps / 100.0);
        app_ui_text(s->footer, text);
        stock_chart_draw(app_ui_pixels(), v);
        app_ui_plot_refresh(s->chart, &s->image);
    }
}
static void icon(void *parent, int x, int y, bool selected)
{
    for (int k=0;k<4;++k) { int top=16-k*4; uint32_t color=selected?APP_COLOR_ACCENT:k==1?0x47E6AE:0xFF6363;
        app_ui_rect(parent,x+3+k*6,y+top,1,12,color,0); app_ui_rect(parent,x+1+k*6,y+top+3,5,6,color,0); }
}
const app_descriptor_t esphook_app_stock = {.api_version=APP_API_VERSION, .id="stock", .title="做T",
    .refresh_ms=40, .state_size=sizeof(state_t), .init=stock_transport_init, .on_enter=enter,
    .on_leave=leave, .on_event=event, .render=render, .icon=icon, .command=stock_transport_command};
