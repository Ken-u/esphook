#include "esphook/app.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "remote_game.h"
#include "stock_transport.h"
typedef struct {
    lv_obj_t *chart, *header, *price, *badge, *badge_text, *footer, *panel, *card, *accent, *title, *profit, *detail, *hint;
    remote_game_view_t view;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "stock state too large");
static void enter(app_context_t *ctx) { stock_transport_event("launch", ctx->now_ms); }
static void leave(app_context_t *ctx)
{
    state_t *s=ctx->state; stock_transport_event("menu", ctx->now_ms);
    if(s->chart) app_ui_canvas_refresh(s->chart);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    if (e->kind == APP_EVENT_DOWN) stock_transport_event("down", e->ms);
    if (e->kind == APP_EVENT_CONFIRM) stock_transport_event("hold", e->ms);
    if (e->kind == APP_EVENT_UP) stock_transport_event("up", e->ms);
    if (e->kind == APP_EVENT_CANCEL) stock_transport_event("cancel", e->ms);
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
        s->header = app_ui_label(ctx->root, &aihook_font_12, 2, 0, 100, 13, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(s->header, app_ui_color(0xB9C3E0), LV_PART_MAIN);
        s->price = app_ui_label(ctx->root, &aihook_font_12, 100, 0, 58, 13, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(s->price, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        s->chart = app_ui_canvas(ctx->root, 0, 15, 160, 48);
        s->badge = app_ui_rect(ctx->root, 2, 66, 32, 13, 0x2A3350, 4);
        s->badge_text = app_ui_label(s->badge, &aihook_font_12, 0, -1, 32, 14, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(s->badge_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        s->footer = app_ui_label(ctx->root, &aihook_font_12, 38, 66, 66, 14, LV_LABEL_LONG_MODE_CLIP);
        app_ui_rect(ctx->root, 108, 70, 6, 3, 0xFFFF00, 1);   /* legend: the two moving averages */
        lv_obj_t *l30 = app_ui_label(ctx->root, &aihook_font_12, 116, 66, 16, 14, LV_LABEL_LONG_MODE_CLIP);
        app_ui_rect(ctx->root, 134, 70, 6, 3, 0x00FFFF, 1);
        lv_obj_t *l60 = app_ui_label(ctx->root, &aihook_font_12, 142, 66, 16, 14, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(l30, app_ui_color(0xB9C3E0), LV_PART_MAIN); app_ui_text(l30, "30");
        lv_obj_set_style_text_color(l60, app_ui_color(0xB9C3E0), LV_PART_MAIN); app_ui_text(l60, "60");
        s->panel = app_ui_layer(ctx->root);
        s->card = app_ui_rect(s->panel, 6, 5, 148, 70, 0x141C34, 6);
        s->accent = app_ui_rect(s->card, 0, 0, 148, 3, 0x5A6BA0, 2);
        s->title = app_ui_label(s->card, &aihook_font_12, 0, 6, 148, 14, LV_LABEL_LONG_MODE_CLIP);
        s->profit = app_ui_label(s->card, &aihook_font_16, 0, 20, 148, 22, LV_LABEL_LONG_MODE_CLIP);
        s->detail = app_ui_label(s->card, &aihook_font_12, 0, 42, 148, 14, LV_LABEL_LONG_MODE_CLIP);
        s->hint = app_ui_label(s->card, &aihook_font_12, 0, 55, 148, 14, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(s->hint, app_ui_color(APP_COLOR_MUTED), LV_PART_MAIN);
        lv_obj_set_style_text_align(s->title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(s->profit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(s->detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(s->hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    char text[120];
    bool overlay = !v->active || v->offline || v->phase != REMOTE_PLAY;
    app_ui_hide(s->panel, !overlay);
    if (overlay) {
        bool waiting = !v->active || v->offline;
        app_ui_text(s->title, !v->active ? "等待PC连接" : v->offline ? "PC 已断开" : v->phase == REMOTE_LOADING ? "加载下一天..." : "做T结束");
        snprintf(text, sizeof text, "收益 %+.2f%%", v->pct_bps / 100.0);
        app_ui_text(s->profit, v->active ? text : "做T模拟");
        uint32_t tone = v->pct_bps >= 0 ? 0xFF6363 : 0x47E6AE;
        lv_obj_set_style_text_color(s->profit, app_ui_color(v->active ? tone : APP_COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s->accent, app_ui_color(waiting || v->phase == REMOTE_LOADING ? 0x5A6BA0 : tone), LV_PART_MAIN);
        snprintf(text, sizeof text, "%+.2f元  交易%u次", v->pnl_cents / 100.0, v->trades);
        app_ui_text(s->detail, v->active ? text : "运行 stock-t.sh");
        app_ui_text(s->hint, !v->active || v->offline ? "短按返回菜单" : "长按继续 / 短按退出");
    } else {
        int32_t price = v->count ? v->bars[v->count - 1].close : 0;
        bool up = !v->count || v->bars[v->count - 1].close >= v->bars[v->count - 1].open;
        snprintf(text, sizeof text, "%s %s", v->code, v->clock);
        app_ui_text(s->header, text);
        snprintf(text, sizeof text, "%.2f", price / 100.0);
        app_ui_text(s->price, text);
        lv_obj_set_style_text_color(s->price, app_ui_color(up ? 0xFF6363 : 0x47E6AE), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s->badge, app_ui_color(v->holding ? 0x8A2433 : 0x2A3350), LV_PART_MAIN);
        app_ui_text(s->badge_text, v->holding ? "持仓" : "空仓");
        snprintf(text, sizeof text, "%+.2f%%", v->pct_bps / 100.0);
        app_ui_text(s->footer, text);
        lv_obj_set_style_text_color(s->footer, app_ui_color(v->pct_bps >= 0 ? 0xFF6363 : 0x47E6AE), LV_PART_MAIN);
        stock_chart_draw(app_ui_pixels(), v);
        app_ui_canvas_refresh(s->chart);
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
