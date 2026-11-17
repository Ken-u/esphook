#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include "esphook/app_runtime.h"
#include "dino_game.h"
#include "dino_sprites.h"
#include "freertos/FreeRTOS.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Layout: an 8 px sky strip, the 64 row pixel canvas (screen y 8..71), an 8 px dirt strip. The ground line is canvas
 * row GROUND_ROW, so a full 38 px jump puts the dino's head on canvas row 0. HUD pills float over the canvas. */
#define CANVAS_Y 8
#define EXIT_HOLD_MS 2000U
#define HINT_SLOT_MS 2600U
#define GROUND_ROW 56
#define CYCLE_PX 6000.0
#define HOLD_PX 2200.0
#define BLEND_PX 800.0

typedef struct {
    dino_game_t game;
    lv_obj_t *top, *plot, *bottom, *hold, *hud, *best, *score, *panel, *title, *line, *hint_pill, *hint;
    lv_image_dsc_t image;
    uint32_t top_rgb, dirt_rgb;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "dino state too large");
static dino_view_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* Day turns to night and back as the run goes on; the blend is smoothed so it never flashes. */
typedef struct {
    uint32_t top, low, sun, hill, hill_cap, cloud, cloud_shade, grass, grass_dark, dirt, pebble;
    unsigned stars; /* 0..256 */
} theme_t;
static const theme_t DAY = {0x5BB5EE, 0xDFF3FF, 0xFFE27A, 0xA9D3B8, 0xCFEBD8, 0xFFFFFF, 0xCFE4F4, 0x6FC95A, 0x4DA044, 0xC79B64, 0x9A7040, 0};
static const theme_t NIGHT = {0x080E2B, 0x2C3C78, 0xE8EEFF, 0x1F2C5C, 0x34457F, 0x2B3A74, 0x202C5E, 0x3C7D58, 0x2B5E44, 0x4A4259, 0x302A40, 256};
static theme_t mix_theme(const theme_t *a, const theme_t *b, unsigned k)
{
    return (theme_t){gfx_mix(a->top, b->top, k), gfx_mix(a->low, b->low, k), gfx_mix(a->sun, b->sun, k), gfx_mix(a->hill, b->hill, k),
        gfx_mix(a->hill_cap, b->hill_cap, k), gfx_mix(a->cloud, b->cloud, k), gfx_mix(a->cloud_shade, b->cloud_shade, k),
        gfx_mix(a->grass, b->grass, k), gfx_mix(a->grass_dark, b->grass_dark, k), gfx_mix(a->dirt, b->dirt, k),
        gfx_mix(a->pebble, b->pebble, k), a->stars + (unsigned)(((int)b->stars - (int)a->stars) * (int)k / 256)};
}
static theme_t theme_at(unsigned distance)
{
    double p = fmod((double)distance, CYCLE_PX), t;
    const theme_t *a = &DAY, *b = &NIGHT;
    if (p < HOLD_PX) return DAY;
    if (p < HOLD_PX + BLEND_PX) t = (p - HOLD_PX) / BLEND_PX;
    else if (p < 2.0 * HOLD_PX + BLEND_PX) return NIGHT;
    else { t = (p - 2.0 * HOLD_PX - BLEND_PX) / BLEND_PX; a = &NIGHT; b = &DAY; }
    return mix_theme(a, b, (unsigned)(t * t * (3.0 - 2.0 * t) * 256.0));
}

static void draw_sky(uint16_t *px, const theme_t *t, unsigned scroll, unsigned now_ms)
{
    gfx_gradient(px, 0, GROUND_ROW, t->top, t->low);
    for (unsigned i = 0; i < 16U && t->stars > 0; ++i) { /* stars come out as it gets dark */
        uint32_t h = gfx_hash(i * 977U + 11U);
        if ((h & 0xFFU) >= t->stars) continue;
        int x = (int)((h >> 8) % 160U), y = (int)((h >> 16) % 38U);
        bool twinkle = ((now_ms / 450U + i) & 3U) == 0U;
        gfx_plot(px, x, y, gfx_rgb565(twinkle ? 0xFFFFFF : gfx_mix(t->top, 0xFFFFFF, 180)));
    }
    gfx_sun(px, 78, 28, t->sun, gfx_mix(t->top, t->low, 150), gfx_mix(t->top, t->low, 90), t->stars > 140U);
    /* Far hills creep, near hills move a little faster, clouds drift in between. */
    gfx_ridge(px, (int)(scroll / 14U), 30, GROUND_ROW, 5, 14, 0x3141U, t->hill, t->hill_cap);
    gfx_ridge(px, (int)(scroll / 6U), 20, GROUND_ROW, 1, 7, 0x5927U, gfx_mix(t->hill, t->grass, 70), gfx_mix(t->hill_cap, t->grass, 70));
    static const int cloud_w[3] = {20, 14, 24};
    for (int i = 0; i < 3; ++i) {
        int x = 160 - (int)((scroll / 9U + (unsigned)i * 83U) % 240U), y = 5 + i * 9 + (i == 2 ? 3 : 0);
        gfx_cloud(px, x, y, cloud_w[i], gfx_rgb565(t->cloud), gfx_rgb565(t->cloud_shade));
    }
}
static void draw_ground(uint16_t *px, const theme_t *t, unsigned scroll)
{
    gfx_rect(px, 0, GROUND_ROW, APP_CANVAS_W, 1, gfx_rgb565(gfx_mix(t->grass, 0xFFFFFF, 50)));
    gfx_rect(px, 0, GROUND_ROW + 1, APP_CANVAS_W, 2, gfx_rgb565(t->grass));
    gfx_rect(px, 0, GROUND_ROW + 3, APP_CANVAS_W, 1, gfx_rgb565(t->grass_dark));
    gfx_rect(px, 0, GROUND_ROW + 4, APP_CANVAS_W, APP_CANVAS_H - GROUND_ROW - 4, gfx_rgb565(t->dirt));
    const uint16_t pebble = gfx_rgb565(t->pebble), tuft = gfx_rgb565(t->grass_dark), tuft_hi = gfx_rgb565(t->grass);
    for (unsigned i = 0; i < 14U; ++i) { /* pebbles and grass tufts slide at full speed so the ground reads as moving */
        uint32_t h = gfx_hash(i * 131U + 3U);
        int x = 160 - (int)((scroll + i * 47U + (h & 15U)) % 200U), y = GROUND_ROW + 5 + (int)((h >> 8) % 3U) * 2;
        gfx_plot(px, x, y, pebble);
        if (i & 1U) gfx_plot(px, x + 1, y, pebble);
        if (i % 3U == 0U) gfx_plot(px, x + 3, y + 1, pebble);
    }
    for (unsigned i = 0; i < 6U; ++i) {
        int x = 160 - (int)((scroll + i * 53U) % 200U);
        gfx_plot(px, x, GROUND_ROW - 1, tuft);
        gfx_plot(px, x - 1, GROUND_ROW - 2, tuft_hi);
        gfx_plot(px, x + 1, GROUND_ROW - 2, tuft_hi);
    }
}
static void draw_dust(uint16_t *px, const dino_view_t *v, const theme_t *t)
{
    const uint16_t puff = gfx_rgb565(gfx_mix(t->dirt, 0xFFFFFF, 120)), puff_dark = gfx_rgb565(gfx_mix(t->dirt, 0xFFFFFF, 60));
    const int foot = (int)DINO_X + 8;
    if (v->phase != DINO_PLAY) return;
    if (!v->airborne && v->since_land_ms < v->since_jump_ms && v->since_land_ms < 260U) { /* landing: a puff both ways */
        int age = (int)v->since_land_ms;
        for (int i = 0; i < 6; ++i) {
            int dir = i < 3 ? -1 : 1, k = i % 3;
            int x = foot + dir * (3 + k * 3 + age / 28), y = GROUND_ROW - 1 - age / 55 - (k & 1);
            gfx_plot(px, x, y, i & 1 ? puff_dark : puff);
            if (k == 0 && age < 130) gfx_plot(px, x - dir, y, puff);
        }
    } else if (v->airborne && v->since_jump_ms < 220U) { /* take-off: a trail that stays behind in the world */
        int age = (int)v->since_jump_ms;
        for (int k = 0; k < 3; ++k) gfx_plot(px, foot - 3 - k * 3 - (int)((unsigned)age * v->speed_pps / 1000U), GROUND_ROW - 1 - age / 70 - (k & 1), k & 1 ? puff_dark : puff);
    } else if (!v->airborne && (v->frame & 1U)) { /* running: a speck behind the heel */
        gfx_plot(px, foot - 6 - (int)(v->scroll % 3U), GROUND_ROW - 1, puff_dark);
    }
}
static void blit(uint16_t *px, const dino_sprite_t *sprite, int x, int y)
{
    static uint16_t table[15]; /* filled on first use; only the UI task draws */
    static const uint32_t rgb[15] = {0x1E5A3A, 0x58C862, 0x3A9A50, 0xC8F4B0, 0xFFFFFF, 0x141824, 0xFFD040, 0x2E8B57, 0x62D07A, 0x4A2A80,
                                     0x8A4FC8, 0xC090F0, 0xFF8A3A, 0xFF9AA8, 0xE05A8A};
    if (!table[1]) for (int i = 0; i < 15; ++i) table[i] = gfx_rgb565(rgb[i]);
    gfx_blit(px, sprite->w, sprite->h, sprite->rows, x, y, DINO_SPRITE_KEYS, table, false);
}
static void draw_scene(uint16_t *px, const dino_view_t *v, const theme_t *t, unsigned now_ms)
{
    draw_sky(px, t, v->scroll, now_ms);
    draw_ground(px, t, v->scroll);
    for (unsigned i = 0; i < v->count; ++i) {
        const dino_obstacle_t *o = &v->obstacles[i];
        float width, height, bottom;
        dino_kind_size(o->kind, &width, &height, &bottom);
        int x = (int)lroundf(o->x), top = GROUND_ROW - (int)(bottom + height);
        switch (o->kind) {
        case DINO_CACTUS_SMALL: blit(px, &DINO_SPRITE_CACTUS_S, x, top); break;
        case DINO_CACTUS_LARGE: blit(px, &DINO_SPRITE_CACTUS_L, x, top); break;
        case DINO_CACTUS_PAIR:
            blit(px, &DINO_SPRITE_CACTUS_S, x, top);
            blit(px, &DINO_SPRITE_CACTUS_S, x + 10, top);
            break;
        default: blit(px, ((now_ms / 130U) & 1U) ? &DINO_SPRITE_BIRD_UP : &DINO_SPRITE_BIRD_DOWN, x, top); break;
        }
        if (bottom > 0.0f) { /* birds throw a small shadow on the ground */
            int sw = 10 - (int)(bottom / 4.0f);
            gfx_hline(px, x + 2, x + 2 + sw, GROUND_ROW + 1, gfx_rgb565(gfx_mix(t->grass, 0x000000, 90)));
        }
    }
    draw_dust(px, v, t);
    const dino_sprite_t *body = v->phase == DINO_OVER ? &DINO_SPRITE_DEAD
        : v->airborne ? &DINO_SPRITE_JUMP : (v->phase == DINO_PLAY && (v->frame & 1U)) ? &DINO_SPRITE_RUN_B : &DINO_SPRITE_RUN_A;
    if (v->airborne) { /* the shadow shrinks as the dino climbs */
        int half = 7 - (int)(v->y / 6.0f);
        if (half < 2) half = 2;
        gfx_hline(px, (int)DINO_X + 9 - half, (int)DINO_X + 9 + half, GROUND_ROW + 1, gfx_rgb565(gfx_mix(t->grass, 0x000000, 110)));
    }
    blit(px, body, (int)DINO_X, GROUND_ROW - (int)lroundf(v->y) - body->h);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    dino_start(&s->game, ctx->now_ms, app_random());
    /* Hopping is done by mashing the key, and a press starts a run: three quick presses are never an exit, holding
     * the key for two seconds (a jump never lasts that long) is. */
    app_set_triple_exit(ctx, false);
    app_set_exit_hold(ctx, EXIT_HOLD_MS);
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
static lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb, unsigned opa)
{
    lv_obj_t *p = app_ui_rect(parent, x, y, w, h, rgb, 5);
    lv_obj_set_style_bg_opa(p, (lv_opa_t)opa, LV_PART_MAIN);
    return p;
}
static void build(state_t *s, lv_obj_t *root)
{
    s->top = app_ui_rect(root, 0, 0, APP_WIDTH, CANVAS_Y, DAY.top, 0);
    s->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->image.header.cf = LV_COLOR_FORMAT_RGB565;
    s->image.header.w = APP_CANVAS_W; s->image.header.h = APP_CANVAS_H; s->image.header.stride = APP_CANVAS_W * 2;
    s->image.data_size = APP_CANVAS_W * APP_CANVAS_H * 2; s->image.data = (const uint8_t *)app_ui_pixels();
    s->plot = lv_image_create(root);
    lv_obj_set_pos(s->plot, 0, CANVAS_Y);
    lv_image_set_src(s->plot, &s->image);
    s->bottom = app_ui_rect(root, 0, CANVAS_Y + APP_CANVAS_H, APP_WIDTH, APP_HEIGHT - CANVAS_Y - APP_CANVAS_H, DAY.dirt, 0);
    s->top_rgb = DAY.top; s->dirt_rgb = DAY.dirt;
    s->hold = app_ui_rect(root, 0, 0, 0, 2, APP_COLOR_ACCENT, 0); /* the hold towards the exit */
    app_ui_hide(s->hold, true);
    s->hud = pill(root, 44, 10, 113, 15, 0x0B1630, 130);
    s->best = app_ui_label(s->hud, &aihook_font_12, 4, 1, 66, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->best, app_ui_color(APP_COLOR_MUTED), LV_PART_MAIN);
    s->score = app_ui_label(s->hud, &aihook_font_12, 70, 1, 39, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s->score, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s->panel = pill(root, 20, 9, 120, 36, 0x0B1630, 205);
    s->title = app_ui_label(s->panel, &aihook_font_16, 0, 2, 120, 20, LV_LABEL_LONG_MODE_CLIP);
    s->line = app_ui_label(s->panel, &aihook_font_12, 0, 21, 120, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->title); app_ui_center(s->line);
    lv_obj_set_style_text_color(s->line, app_ui_color(APP_COLOR_MUTED), LV_PART_MAIN);
    s->hint_pill = pill(root, 14, 65, 132, 15, 0x0B1630, 150);
    s->hint = app_ui_label(s->hint_pill, &aihook_font_12, 0, 1, 132, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->hint);
}
static void sync_strips(state_t *s, const theme_t *t)
{
    if (s->top_rgb != t->top) { lv_obj_set_style_bg_color(s->top, app_ui_color(t->top), LV_PART_MAIN); s->top_rgb = t->top; }
    if (s->dirt_rgb != t->dirt) { lv_obj_set_style_bg_color(s->bottom, app_ui_color(t->dirt), LV_PART_MAIN); s->dirt_rgb = t->dirt; }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    dino_view_t v;
    dino_snapshot(&s->game, &v);
    if (!s->plot) build(s, ctx->root);
    const theme_t t = theme_at(v.scroll);
    sync_strips(s, &t);
    char text[48];
    bool play = v.phase == DINO_PLAY;
    snprintf(text, sizeof text, "最高 %05u", v.best);
    app_ui_text(s->best, text);
    snprintf(text, sizeof text, "%05u", v.score);
    app_ui_text(s->score, text);
    bool blink = play && v.score >= 100U && v.score % 100U < 8U && ((ctx->now_ms / 90U) & 1U); /* flashes at every 100 */
    lv_obj_set_style_text_color(s->score, app_ui_color(blink ? APP_COLOR_ACCENT : APP_COLOR_TEXT), LV_PART_MAIN);
    app_ui_hold_line(s->hold, app_exit_hold_progress(ctx), false, EXIT_HOLD_MS);
    const bool exit_hint = (ctx->now_ms / HINT_SLOT_MS) % 2U == 1U; /* the hint of the way out alternates */
    app_ui_hide(s->hud, !play);
    app_ui_hide(s->panel, play);
    app_ui_hide(s->hint_pill, play || (v.phase == DINO_OVER && !v.can_restart));
    if (v.phase == DINO_READY) {
        app_ui_text(s->title, "小恐龙");
        lv_obj_set_style_text_color(s->title, app_ui_color(APP_COLOR_ACCENT), LV_PART_MAIN);
        snprintf(text, sizeof text, "最高 %05u", v.best);
        app_ui_text(s->line, text);
        app_ui_text(s->hint, exit_hint ? "按住2秒退出" : "按键起跳 按住跳更高");
    } else if (v.phase == DINO_OVER) {
        app_ui_text(s->title, v.new_best ? "新纪录!" : "游戏结束");
        lv_obj_set_style_text_color(s->title, app_ui_color(v.new_best ? APP_COLOR_ACCENT : 0xFF8A7A), LV_PART_MAIN);
        snprintf(text, sizeof text, "得分 %u  最高 %u", v.score, v.best);
        app_ui_text(s->line, text);
        app_ui_text(s->hint, exit_hint ? "按住2秒退出" : "按键重来");
    }
    draw_scene(app_ui_pixels(), &v, &t, (unsigned)ctx->now_ms);
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
    const uint32_t body = selected ? APP_COLOR_ACCENT : 0x58C862;
    app_ui_rect(parent, x + 1, y + 2, 27, 25, 0x5BB5EE, 4);   /* sky */
    app_ui_rect(parent, x + 16, y + 6, 8, 3, 0xFFFFFF, 1);    /* cloud */
    app_ui_rect(parent, x + 1, y + 21, 27, 6, 0xC79B64, 3);   /* ground */
    app_ui_rect(parent, x + 1, y + 20, 27, 2, 0x6FC95A, 0);
    app_ui_rect(parent, x + 11, y + 7, 8, 6, body, 1);        /* head */
    app_ui_rect(parent, x + 16, y + 8, 2, 2, 0x141824, 0);    /* eye */
    app_ui_rect(parent, x + 7, y + 12, 9, 6, body, 1);        /* body */
    app_ui_rect(parent, x + 3, y + 13, 5, 2, body, 1);        /* tail */
    app_ui_rect(parent, x + 8, y + 17, 2, 4, body, 0);        /* legs */
    app_ui_rect(parent, x + 13, y + 17, 2, 3, body, 0);
    app_ui_rect(parent, x + 22, y + 13, 3, 8, 0x2E8B57, 1);   /* cactus */
    app_ui_rect(parent, x + 20, y + 15, 2, 3, 0x2E8B57, 0);
}
const app_descriptor_t esphook_app_dino = {.api_version = APP_API_VERSION, .id = "dino", .title = "小恐龙",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
