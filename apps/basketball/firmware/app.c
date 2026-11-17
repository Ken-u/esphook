#include "esphook/app.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "basketball_game.h"
#include "esphook/app_runtime.h"
#include "freertos/FreeRTOS.h"
typedef struct {
    basketball_game_t game;
    lv_obj_t *court, *results, *hoop, *rim, *ball, *clock_label, *score_label, *shots_label, *hint;
    lv_obj_t *result_made, *result_score, *result_accuracy;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "basketball state too large");
static basketball_view_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static void enter(app_context_t *ctx) { state_t *s=ctx->state; basketball_start(&s->game, ctx->now_ms, app_random()); }
static void tick(app_context_t *ctx)
{
    state_t *s=ctx->state; basketball_tick(&s->game, ctx->now_ms);
    basketball_view_t view; basketball_snapshot(&s->game, ctx->now_ms, &view);
    portENTER_CRITICAL(&s_lock); s_status=view; portEXIT_CRITICAL(&s_lock);
}
static void leave(app_context_t *ctx)
{
    state_t *s=ctx->state; basketball_stop(&s->game);
    portENTER_CRITICAL(&s_lock); s_status.phase=BASKETBALL_OFF; portEXIT_CRITICAL(&s_lock);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s=ctx->state;
    if (e->kind != APP_EVENT_SHORT) return;
    if (s->game.phase == BASKETBALL_RESULT) app_menu(ctx);
    else (void)basketball_shoot(&s->game, e->ms);
}
static void render(app_context_t *ctx)
{
    state_t *s=ctx->state; basketball_view_t view; basketball_snapshot(&s->game, ctx->now_ms, &view);
    const basketball_view_t *game=&view;
    if (!s->court) {
        s->court = app_ui_layer(ctx->root);
        s->results = app_ui_layer(ctx->root);
        s->clock_label = app_ui_label(s->court, &aihook_font_12, 3, 0, 84, 14, LV_LABEL_LONG_MODE_CLIP);
        s->score_label = app_ui_label(s->court, &aihook_font_12, 89, 0, 68, 14, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(s->score_label, app_ui_color(0xFDB927), LV_PART_MAIN);
        /* Central lane is the exact +/-10px window the hoop must be in when the ball reaches the rim. */
        app_ui_rect(s->court, 70, 17, 21, 45, 0x102C22, 0);
        for (int y = 18; y < 61; y += 6) app_ui_rect(s->court, 80, y, 1, 3, 0x54836E, 0);
        app_ui_rect(s->court, 2, 62, 156, 1, 0x59406C, 0);
        app_ui_rect(s->court, 69, 61, 2, 3, 0x57A77E, 0);
        app_ui_rect(s->court, 90, 61, 2, 3, 0x57A77E, 0);
        s->hoop = lv_obj_create(s->court);
        app_ui_plain(s->hoop);
        lv_obj_set_size(s->hoop, 32, 32);
        app_ui_rect(s->hoop, 2, 0, 28, 14, 0xE4E6F0, 1);
        app_ui_rect(s->hoop, 4, 2, 24, 10, 0x38224A, 0);
        app_ui_rect(s->hoop, 10, 7, 12, 7, 0xF9A12A, 0);
        app_ui_rect(s->hoop, 12, 9, 8, 4, 0x38224A, 0);
        s->rim = app_ui_rect(s->hoop, 1, 15, 30, 3, 0xFF922F, 0);
        for (int row = 0; row < 4; ++row) {
            app_ui_rect(s->hoop, 4 + row, 19 + row * 3, 2, 3, 0xBAC0CC, 0);
            app_ui_rect(s->hoop, 26 - row, 19 + row * 3, 2, 3, 0xBAC0CC, 0);
            app_ui_rect(s->hoop, 6 + row, 20 + row * 3, 20 - row * 2, 1, 0x8890A4, 0);
        }
        s->ball = app_ui_rect(s->court, 76, 61, 8, 8, 0xFF922F, 4);
        app_ui_rect(s->ball, 3, 0, 1, 8, 0x51321C, 0);
        app_ui_rect(s->ball, 0, 3, 8, 1, 0x51321C, 0);
        s->shots_label = app_ui_label(s->court, &aihook_font_12, 3, 67, 80, 13, LV_LABEL_LONG_MODE_CLIP);
        s->hint = app_ui_label(s->court, &aihook_font_12, 83, 67, 75, 13, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_t *title = app_ui_label(s->results, &aihook_font_12, 3, 0, 154, 14, LV_LABEL_LONG_MODE_CLIP);
        app_ui_text(title, "时间到!");
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        s->result_made = app_ui_label(s->results, &aihook_font_16, 3, 17, 154, 20, LV_LABEL_LONG_MODE_CLIP);
        s->result_score = app_ui_label(s->results, &aihook_font_16, 3, 36, 154, 20, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(s->result_score, app_ui_color(0xFDB927), LV_PART_MAIN);
        s->result_accuracy = app_ui_label(s->results, &aihook_font_12, 3, 55, 154, 13, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_t *again = app_ui_label(s->results, &aihook_font_12, 2, 68, 156, 12, LV_LABEL_LONG_MODE_CLIP);
        app_ui_text(again, "短按菜单 / 三击菜单");
        lv_obj_set_style_text_align(s->result_made, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(s->result_score, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(s->result_accuracy, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(again, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    char text[80];
    bool ended = game->phase == BASKETBALL_RESULT;
    app_ui_hide(s->court, ended);
    app_ui_hide(s->results, !ended);
    if (ended) {
        snprintf(text, sizeof text, "投中 %lu 次", (unsigned long)game->made);
        app_ui_text(s->result_made, text);
        snprintf(text, sizeof text, "得分 %lu", (unsigned long)game->score);
        app_ui_text(s->result_score, text);
        unsigned accuracy = game->shots ? (unsigned)(game->made * 100 / game->shots) : 0;
        snprintf(text, sizeof text, "命中率 %u%%  共 %lu 次", accuracy, (unsigned long)game->shots);
        app_ui_text(s->result_accuracy, text);
    } else {
        snprintf(text, sizeof text, "剩余 %02u:%02u", game->seconds_left / 60, game->seconds_left % 60);
        app_ui_text(s->clock_label, text);
        snprintf(text, sizeof text, "得分 %lu", (unsigned long)game->score);
        app_ui_text(s->score_label, text);
        snprintf(text, sizeof text, "进 %lu/%lu", (unsigned long)game->made, (unsigned long)game->shots);
        app_ui_text(s->shots_label, text);
        /* The outcome is decided at release but shown only when the ball reaches the moving rim. */
        app_ui_text(s->hint, !game->in_flight ? "按键投篮" : !game->reached_rim ? "球飞行中"
            : game->last_hit ? "命中! +2" : "没进");
        lv_obj_set_style_text_color(s->hint, app_ui_color(game->reached_rim
            ? game->last_hit ? 0x47E6AE : 0xFF6B7A : 0xC178FF), LV_PART_MAIN);
        lv_obj_set_pos(s->hoop, (int)lroundf(game->hoop_x) - 16, 18);
        bool centered = fabsf(game->hoop_x - 80) <= BASKETBALL_HIT_PX;
        lv_obj_set_style_bg_color(s->rim, app_ui_color(centered ? 0x47E6AE : 0xFF922F), LV_PART_MAIN);
        lv_obj_set_pos(s->ball, (int)lroundf(game->ball_x) - 4, (int)lroundf(game->ball_y) - 4);
    }
}
static bool command(const char *line)
{
    if (!strcmp(line, "robot game start")) { (void)app_transport_write(app_runtime_open("basketball") ? "OK game queued" : "ERR game queue full"); return true; }
    if (!strcmp(line, "robot game stop")) { (void)app_runtime_menu(); return true; }
    if (strcmp(line, "robot game status")) return false;
    basketball_view_t v; portENTER_CRITICAL(&s_lock); v=s_status; portEXIT_CRITICAL(&s_lock);
    char reply[160]; snprintf(reply, sizeof reply, "phase=%u seconds=%u shots=%lu made=%lu score=%lu", (unsigned)v.phase,
        v.seconds_left, (unsigned long)v.shots, (unsigned long)v.made, (unsigned long)v.score);
    (void)app_transport_write(reply); return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    lv_obj_t *ball=app_ui_rect(parent,x+2,y+2,24,24,selected?APP_COLOR_ACCENT:0xFF922F,12);
    app_ui_rect(ball,11,0,2,24,0x573122,0); app_ui_rect(ball,0,11,24,2,0x573122,0);
    app_ui_rect(ball,5,1,1,22,0xA85C28,0); app_ui_rect(ball,18,1,1,22,0xA85C28,0);
}
const app_descriptor_t esphook_app_basketball = {.api_version=APP_API_VERSION, .id="basketball", .title="投篮",
    .refresh_ms=33, .state_size=sizeof(state_t), .on_enter=enter, .on_leave=leave, .on_event=event,
    .on_tick=tick, .render=render, .icon=icon, .command=command};
