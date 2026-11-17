#include "esphook/app.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include "esphook/app_runtime.h"
#include "dino_game.h"
#include "dino_sprites.h"
#include "freertos/FreeRTOS.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Layout: score row (0..15), 160x48 pixel canvas (16..63), hint row (64..79). The ground line is canvas row 40,
 * so a full 24 px jump puts the dino's head on canvas row 0, directly under the score row. */
#define CANVAS_Y 16
#define CANVAS_H 48
#define GROUND_ROW 40
typedef struct {
    dino_game_t game;
    lv_obj_t *plot, *best, *score, *banner, *hint;
    lv_image_dsc_t image;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "dino state too large");
static dino_view_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 19) & 0x1F) << 11 | ((rgb >> 10) & 0x3F) << 5 | ((rgb >> 3) & 0x1F));
}
static void plot(uint16_t *px, int x, int y, uint16_t color)
{
    if (x >= 0 && x < DINO_FIELD_W && y >= 0 && y < CANVAS_H) px[y * DINO_FIELD_W + x] = color;
}
static void blit(uint16_t *px, const dino_sprite_t *sprite, int x, int y, uint16_t color)
{
    for (int row = 0; row < sprite->h; ++row)
        for (int col = 0; col < sprite->w; ++col)
            if (sprite->rows[row][col] == '#') plot(px, x + col, y + row, color);
}
static void draw_scene(uint16_t *px, const dino_view_t *v, unsigned now_ms)
{
    const uint16_t sky = rgb565(APP_COLOR_BG), cloud = rgb565(0x1B2A3D), ground = rgb565(0x6B7A90),
        speck = rgb565(0x3C4A5E), cactus = rgb565(0x47E6AE), bird = rgb565(0xC178FF);
    const uint16_t dino = v->phase == DINO_OVER ? rgb565(0xFF6B7A) : rgb565(0xE6EAF0);
    for (int i = 0; i < DINO_FIELD_W * CANVAS_H; ++i) px[i] = sky;
    /* Clouds drift slowly; each is a few stacked bars. */
    for (int i = 0; i < 2; ++i) {
        int x = 150 - (int)((v->scroll / 5 + (unsigned)i * 97U) % 230U), y = 5 + i * 9;
        for (int k = 0; k < 14; ++k) { plot(px, x + k, y + 2, cloud); if (k > 2 && k < 11) plot(px, x + k, y + 1, cloud); if (k > 4 && k < 9) plot(px, x + k, y, cloud); }
    }
    for (int x = 0; x < DINO_FIELD_W; ++x) plot(px, x, GROUND_ROW, ground);
    for (unsigned i = 0; i < 9; ++i) { /* Pebbles slide at full speed so the ground reads as moving. */
        int x = DINO_FIELD_W - (int)((v->scroll + i * 41U) % (DINO_FIELD_W + 20U)), y = GROUND_ROW + 3 + (int)(i * 5U % 4U) * 1;
        plot(px, x, y, speck); plot(px, x + 1, y, speck); if (i % 3 == 0) plot(px, x + 2, y, speck);
    }
    for (unsigned i = 0; i < v->count; ++i) {
        const dino_obstacle_t *o = &v->obstacles[i];
        float width, height, bottom;
        dino_kind_size(o->kind, &width, &height, &bottom);
        int x = (int)lroundf(o->x), top = GROUND_ROW - (int)(bottom + height);
        switch (o->kind) {
        case DINO_CACTUS_SMALL: blit(px, &DINO_SPRITE_CACTUS_S, x, top, cactus); break;
        case DINO_CACTUS_LARGE: blit(px, &DINO_SPRITE_CACTUS_L, x, top, cactus); break;
        case DINO_CACTUS_PAIR:
            blit(px, &DINO_SPRITE_CACTUS_S, x, top, cactus);
            blit(px, &DINO_SPRITE_CACTUS_S, x + 8, top, cactus);
            break;
        default: blit(px, ((now_ms / 130U) & 1U) ? &DINO_SPRITE_BIRD_UP : &DINO_SPRITE_BIRD_DOWN, x, top, bird); break;
        }
    }
    const dino_sprite_t *body = (v->phase == DINO_PLAY && !v->airborne && (v->frame & 1U)) ? &DINO_SPRITE_RUN_B
        : &DINO_SPRITE_RUN_A;
    blit(px, body, (int)DINO_X, GROUND_ROW - (int)lroundf(v->y) - body->h, dino);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    dino_start(&s->game, ctx->now_ms, app_random());
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    dino_tick(&s->game, ctx->now_ms);
    dino_view_t view;
    dino_snapshot(&s->game, &view);
    portENTER_CRITICAL(&s_lock); s_status = view; portEXIT_CRITICAL(&s_lock);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->plot) app_ui_plot_refresh(s->plot, &s->image); /* The shared canvas goes to the next app. */
    portENTER_CRITICAL(&s_lock); s_status.phase = (dino_phase_t)255; portEXIT_CRITICAL(&s_lock);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    switch (e->kind) {
    case APP_EVENT_DOWN: (void)dino_press(&s->game, e->ms); break;
    case APP_EVENT_UP: case APP_EVENT_CANCEL: dino_release(&s->game, e->ms); break;
    default: break; /* SHORT/CONFIRM are the same press seen again; the jump already happened on DOWN. */
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    dino_view_t v;
    dino_snapshot(&s->game, &v);
    if (!s->plot) {
        s->best = app_ui_label(ctx->root, &aihook_font_12, 3, 1, 76, 14, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(s->best, app_ui_color(APP_COLOR_MUTED), LV_PART_MAIN);
        s->score = app_ui_label(ctx->root, &aihook_font_12, 79, 1, 78, 14, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(s->score, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        s->image.header.magic = LV_IMAGE_HEADER_MAGIC;
        s->image.header.cf = LV_COLOR_FORMAT_RGB565;
        s->image.header.w = DINO_FIELD_W; s->image.header.h = CANVAS_H; s->image.header.stride = DINO_FIELD_W * 2;
        s->image.data_size = DINO_FIELD_W * CANVAS_H * 2; s->image.data = (const uint8_t *)app_ui_pixels();
        s->plot = lv_image_create(ctx->root);
        lv_obj_set_pos(s->plot, 0, CANVAS_Y);
        lv_image_set_src(s->plot, &s->image);
        s->banner = app_ui_label(ctx->root, &aihook_font_16, 0, 24, DINO_FIELD_W, 20, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(s->banner, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        s->hint = app_ui_label(ctx->root, &aihook_font_12, 2, 65, 156, 14, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(s->hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    char text[48];
    snprintf(text, sizeof text, "最高 %05u", v.best);
    app_ui_text(s->best, text);
    snprintf(text, sizeof text, "%05u", v.score);
    app_ui_text(s->score, text);
    lv_obj_set_style_text_color(s->score, app_ui_color(v.new_best ? APP_COLOR_ACCENT : APP_COLOR_TEXT), LV_PART_MAIN);
    app_ui_hide(s->banner, v.phase == DINO_PLAY);
    if (v.phase == DINO_READY) {
        app_ui_text(s->banner, "小恐龙");
        lv_obj_set_style_text_color(s->banner, app_ui_color(0xE6EAF0), LV_PART_MAIN);
        app_ui_text(s->hint, "按键起跳 按住跳更高");
    } else if (v.phase == DINO_OVER) {
        app_ui_text(s->banner, v.new_best ? "新纪录!" : "游戏结束");
        lv_obj_set_style_text_color(s->banner, app_ui_color(v.new_best ? APP_COLOR_ACCENT : 0xFF6B7A), LV_PART_MAIN);
        app_ui_text(s->hint, v.can_restart ? "按键重来 / 三击菜单" : "");
    } else {
        snprintf(text, sizeof text, "速度 x%u.%u", v.speed_pps / 90U, v.speed_pps * 10U / 90U % 10U);
        app_ui_text(s->hint, text);
    }
    draw_scene(app_ui_pixels(), &v, (unsigned)ctx->now_ms);
    app_ui_plot_refresh(s->plot, &s->image);
}
static bool command(const char *line)
{
    if (!strcmp(line, "robot dino start")) {
        (void)app_transport_write(app_runtime_open("dino") ? "OK dino queued" : "ERR dino queue full");
        return true;
    }
    if (!strcmp(line, "robot dino stop")) { (void)app_runtime_menu(); return true; }
    if (strcmp(line, "robot dino status")) return false;
    dino_view_t v;
    portENTER_CRITICAL(&s_lock); v = s_status; portEXIT_CRITICAL(&s_lock);
    char reply[120];
    snprintf(reply, sizeof reply, "phase=%u score=%u best=%u speed=%u obstacles=%u", (unsigned)v.phase, v.score, v.best,
        v.speed_pps, v.count);
    (void)app_transport_write(reply);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    uint32_t color = selected ? APP_COLOR_ACCENT : 0xE6EAF0;
    app_ui_rect(parent, x + 12, y + 3, 12, 9, color, 2);   /* head */
    app_ui_rect(parent, x + 20, y + 5, 2, 2, 0x071018, 0); /* eye */
    app_ui_rect(parent, x + 8, y + 10, 12, 10, color, 2);  /* body */
    app_ui_rect(parent, x + 2, y + 12, 7, 3, color, 1);    /* tail */
    app_ui_rect(parent, x + 10, y + 19, 3, 6, color, 0);   /* legs */
    app_ui_rect(parent, x + 16, y + 19, 3, 5, color, 0);
    app_ui_rect(parent, x + 1, y + 26, 27, 1, 0x6B7A90, 0); /* ground */
}
const app_descriptor_t esphook_app_dino = {.api_version = APP_API_VERSION, .id = "dino", .title = "小恐龙",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
