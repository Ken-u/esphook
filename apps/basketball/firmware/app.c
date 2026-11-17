#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "basketball_game.h"
#include "esphook/app_runtime.h"
#include "freertos/FreeRTOS.h"

/* Layout: an 8 px strip (timer bar), the 64 row pixel canvas (screen y 8..71), an 8 px strip. Game coordinates are
 * screen coordinates; the scene is painted in canvas coordinates (screen y - CANVAS_Y). The hoop hangs from the
 * ceiling above the wall; the wall meets the wood floor at canvas row HORIZON. HUD pills float over the canvas. */
#define CANVAS_Y 8
#define HORIZON 46
#define BACKBOARD_TOP 10
#define RIM_Y 25
#define FX_MS 700U
#define WALL_TOP 0x120C2AU
#define WALL_LOW 0x4A3578U
#define FLOOR_TOP 0xCF9250U
#define FLOOR_LOW 0x8C5A2BU
#define TRAIL 4
#define EXIT_HOLD_MS 2000U

typedef struct {
    basketball_game_t game;
    lv_obj_t *top, *plot, *bottom, *hold, *hud, *results;
    lv_obj_t *clock_pill, *clock_label, *score_pill, *score_label, *bar, *shots_pill, *shots_label, *hint_pill, *hint, *float_label;
    lv_obj_t *title, *result_score, *result_line, *result_hint;
    lv_image_dsc_t image;
    uint64_t fx_ms;
    float fx_x, prev_hoop_x, sway;
    int16_t trail_x[TRAIL], trail_y[TRAIL];
    uint8_t trail_n;
    bool fx_hit, fx_live, was_reached, ended;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "basketball state too large");
static basketball_view_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void enter(app_context_t *ctx)
{
    state_t *s=ctx->state; basketball_start(&s->game, ctx->now_ms, app_random());
    /* Shots come fast: no global exit during the two minutes (it is back on the result screen, where any short press
     * leaves anyway). Holding the key for two seconds, which a shot never does, leaves at any time. */
    app_set_exit_hold(ctx, EXIT_HOLD_MS);
}
static void tick(app_context_t *ctx)
{
    state_t *s=ctx->state; basketball_tick(&s->game, ctx->now_ms);
    basketball_view_t view; basketball_snapshot(&s->game, ctx->now_ms, &view);
    app_set_triple_exit(ctx, view.phase != BASKETBALL_PLAY);
    portENTER_CRITICAL(&s_lock); s_status=view; portEXIT_CRITICAL(&s_lock);
}
static void leave(app_context_t *ctx)
{
    state_t *s=ctx->state; basketball_stop(&s->game);
    if (s->plot) app_ui_plot_refresh(s->plot, &s->image); /* The shared canvas goes to the next app. */
    portENTER_CRITICAL(&s_lock); s_status.phase=BASKETBALL_OFF; portEXIT_CRITICAL(&s_lock);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s=ctx->state;
    if (e->kind != APP_EVENT_SHORT) return;
    if (s->game.phase == BASKETBALL_RESULT) app_menu(ctx);
    else (void)basketball_shoot(&s->game, e->ms);
}

/* ---- Scene ------------------------------------------------------------------------------------------------------ */
static void draw_wall(uint16_t *px, unsigned now_ms)
{
    gfx_gradient(px, 0, HORIZON, WALL_TOP, WALL_LOW);
    /* A dim crowd on the far side: heads and shoulders in muted team colours, with the odd flash of a phone. */
    static const uint32_t shirts[5] = {0x5A3F8F, 0x7A4FA8, 0x3E5BA8, 0xA84F7A, 0x4F8FA8};
    for (int x = 2; x < APP_CANVAS_W; x += 6) {
        uint32_t h = gfx_hash((uint32_t)x * 17U + 5U);
        int y = 37 + (int)(h % 4U);
        uint32_t shirt = gfx_mix(WALL_LOW, shirts[(h >> 8) % 5U], 110);
        gfx_rect(px, x - 2, y + 3, 5, HORIZON - y - 3, gfx_rgb565(gfx_mix(shirt, 0x000000, 70)));
        gfx_disc(px, x, y, 2, gfx_rgb565(gfx_mix(shirt, 0xF2C79C, 90)));
        if (((now_ms / 700U) + (h >> 16)) % 23U == 0U) gfx_plot(px, x + 2, y - 3, gfx_rgb565(0xFFFFFF));
    }
    /* Advertising boards along the foot of the wall. */
    static const uint32_t boards[4] = {0xC0392B, 0x2D6FD0, 0xE0A21A, 0x2AA876};
    for (int x = 0; x < APP_CANVAS_W; x += 20) {
        uint32_t c = gfx_mix(boards[(x / 20) % 4], 0x000000, 70);
        gfx_rect(px, x, HORIZON - 3, 20, 3, gfx_rgb565(c));
        gfx_hline(px, x, x + 19, HORIZON - 3, gfx_rgb565(gfx_mix(c, 0xFFFFFF, 90)));
    }
    /* The +-10 px window the hoop has to be in when the ball reaches it, as a faint column on the wall. */
    gfx_tint(px, 70, 0, 21, HORIZON - 3, 0xFFFFFF, 20);
    for (int y = 1; y < HORIZON - 3; y += 6) { gfx_tint_plot(px, 70, y, 0xFFFFFF, 90); gfx_tint_plot(px, 70, y + 1, 0xFFFFFF, 90);
        gfx_tint_plot(px, 90, y, 0xFFFFFF, 90); gfx_tint_plot(px, 90, y + 1, 0xFFFFFF, 90); gfx_tint_plot(px, 80, y + 2, 0xFFFFFF, 60); }
}
static void draw_floor(uint16_t *px)
{
    gfx_gradient(px, HORIZON, APP_CANVAS_H, FLOOR_TOP, FLOOR_LOW);
    const uint16_t seam = gfx_rgb565(0x74491F);
    for (int k = -6; k <= 6; ++k) { /* planks converge towards the wall */
        int bottom = 80 + k * 24;
        gfx_line(px, 80 + k * 10, HORIZON, bottom, APP_CANVAS_H - 1, seam);
    }
    gfx_hline(px, 0, APP_CANVAS_W - 1, HORIZON, gfx_rgb565(gfx_mix(FLOOR_TOP, 0xFFFFFF, 90))); /* skirting highlight */
    for (int y = HORIZON; y < APP_CANVAS_H; ++y) { /* painted lane under the hoop */
        int widen = (y - HORIZON) * 4 / (APP_CANVAS_H - HORIZON);
        gfx_tint(px, 70 - widen, y, 21 + 2 * widen, 1, 0xE0503A, 95);
        gfx_tint_plot(px, 70 - widen, y, 0xFFFFFF, 190);
        gfx_tint_plot(px, 90 + widen, y, 0xFFFFFF, 190);
    }
}
static void draw_ball(uint16_t *px, int cx, int cy, int r)
{
    const uint16_t base = gfx_rgb565(0xF08A2C), dark = gfx_rgb565(0xB4521A), light = gfx_rgb565(0xFFC27A), seam = gfx_rgb565(0x4A2A14);
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x) {
            if (x * x + y * y > r * r + r / 2) continue;
            uint16_t c = x + y > r - 1 ? dark : x + y < -r + 1 ? light : base;
            if (x == 0 || y == 0) c = seam;
            gfx_plot(px, cx + x, cy + y, c);
        }
    if (r >= 4) { /* curved seams */
        gfx_plot(px, cx - 3, cy - 2, seam); gfx_plot(px, cx - 3, cy + 2, seam);
        gfx_plot(px, cx + 3, cy - 2, seam); gfx_plot(px, cx + 3, cy + 2, seam);
    }
}
static void draw_backboard(uint16_t *px, int hx)
{
    gfx_rect(px, hx - 1, 0, 3, BACKBOARD_TOP, gfx_rgb565(0x8A90A8)); /* hanging arm */
    gfx_vline(px, hx - 1, 0, BACKBOARD_TOP - 1, gfx_rgb565(0xC9CEDD));
    const int x0 = hx - 14, w = 29, h = 14;
    gfx_rect(px, x0, BACKBOARD_TOP, w, h, gfx_rgb565(0xE4E6F0));                       /* frame */
    gfx_rect(px, x0 + 1, BACKBOARD_TOP + 1, w - 2, h - 2, gfx_rgb565(0x7288C8));       /* glass */
    gfx_tint(px, x0 + 1, BACKBOARD_TOP + 1, w - 2, 5, 0xFFFFFF, 40);
    for (int i = 0; i < 4; ++i) { gfx_tint_plot(px, x0 + 3 + i, BACKBOARD_TOP + 5 - i, 0xFFFFFF, 140); gfx_tint_plot(px, x0 + 8 + i, BACKBOARD_TOP + 5 - i, 0xFFFFFF, 90); }
    const uint16_t target = gfx_rgb565(0xFF5A3A);
    gfx_rect(px, hx - 6, BACKBOARD_TOP + 6, 13, 1, target);                            /* target square */
    gfx_rect(px, hx - 6, BACKBOARD_TOP + 11, 13, 1, target);
    gfx_vline(px, hx - 6, BACKBOARD_TOP + 6, BACKBOARD_TOP + 11, target);
    gfx_vline(px, hx + 6, BACKBOARD_TOP + 6, BACKBOARD_TOP + 11, target);
}
static void draw_rim_back(uint16_t *px, int hx, uint32_t rim)
{
    gfx_hline(px, hx - 11, hx + 11, RIM_Y, gfx_rgb565(gfx_mix(rim, 0x000000, 110)));
}
static void draw_rim_front(uint16_t *px, int hx, uint32_t rim, bool glow)
{
    const uint16_t c = gfx_rgb565(rim), hi = gfx_rgb565(gfx_mix(rim, 0xFFFFFF, 110));
    gfx_hline(px, hx - 12, hx + 12, RIM_Y + 1, hi);
    gfx_hline(px, hx - 12, hx + 12, RIM_Y + 2, c);
    gfx_plot(px, hx - 13, RIM_Y, c); gfx_plot(px, hx + 13, RIM_Y, c);
    if (glow) gfx_tint(px, hx - 14, RIM_Y - 1, 29, 5, rim, 55); /* the rim lights up while it is inside the window */
}
/* Diamond mesh hanging from the rim. `sway` moves the bottom sideways, `bulge` widens it as a ball passes through. */
static void draw_net(uint16_t *px, int hx, float sway, float bulge)
{
    int nx[5][5], ny[5];
    for (int r = 0; r < 5; ++r) {
        float t = (float)r / 4.0f, half = 10.5f - 5.5f * t + bulge * sinf(t * 3.14159f);
        ny[r] = RIM_Y + 3 + r * 3 + (int)(bulge * t);
        for (int c = 0; c < 5; ++c) nx[r][c] = hx + (int)lroundf(sway * t + half * ((float)c - 2.0f) / 2.0f);
    }
    const uint16_t front = gfx_rgb565(0xEEF1F8), back = gfx_rgb565(0xA3AABD);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            gfx_line(px, nx[r][c], ny[r], nx[r + 1][c + 1], ny[r + 1], (r + c) & 1 ? front : back);
            gfx_line(px, nx[r][c + 1], ny[r], nx[r + 1][c], ny[r + 1], (r + c) & 1 ? back : front);
        }
    gfx_line(px, nx[4][0], ny[4], nx[4][4], ny[4], front);
}
static void draw_scene(state_t *s, const basketball_view_t *v, uint64_t now)
{
    uint16_t *px = app_ui_pixels();
    const int hx = (int)lroundf(v->hoop_x);
    draw_wall(px, (unsigned)now);
    draw_floor(px);
    /* effects driven by the last time a ball reached the rim */
    uint64_t age = s->fx_live ? now - s->fx_ms : FX_MS;
    bool centered = fabsf(v->hoop_x - BASKETBALL_CENTER_X) <= BASKETBALL_HIT_PX;
    uint32_t rim = centered ? 0x5CF0B0 : 0xFF8A2A;
    int shake = (s->fx_live && !s->fx_hit && age < 180U) ? ((age / 30U) & 1U ? 1 : -1) : 0;
    draw_backboard(px, hx + shake);
    draw_rim_back(px, hx + shake, rim);
    /* Ball, its shadow and its trail. Height above the floor is measured from where it rests (screen y 57). */
    const int bx = (int)lroundf(v->ball_x), by = (int)lroundf(v->ball_y) - CANVAS_Y;
    float height = 57.0f - v->ball_y;
    if (height < 0.0f) height = 0.0f;
    int half = 5 - (int)(height / 10.0f);
    if (half < 2) half = 2;
    unsigned shade = height < 40.0f ? 150U - (unsigned)(height * 2.5f) : 50U;
    for (int dy = -1; dy <= 1; ++dy) {
        int span = dy == 0 ? half : half - 2;
        if (span > 0) gfx_tint(px, bx - span, 54 + dy, 2 * span + 1, 1, 0x000000, shade);
    }
    for (unsigned i = 0; i < s->trail_n; ++i) {
        unsigned alpha = 60U + 40U * i;
        gfx_tint(px, s->trail_x[i] - 1, s->trail_y[i] - CANVAS_Y - 1, 3, 3, 0xFFB35A, alpha > 190U ? 190U : alpha);
    }
    float bulge = 0.0f;
    if (s->fx_live && s->fx_hit && age < 450U) bulge = 2.5f * sinf((float)age / 450.0f * 3.14159f);
    draw_net(px, hx + shake, s->sway, bulge);
    draw_rim_front(px, hx + shake, rim, centered);
    draw_ball(px, bx, by, by < 40 ? 3 : 4); /* last, so a ball dropping through the net stays visible */
    if (s->fx_live && s->fx_hit && age < 520U) { /* sparkle burst from the rim */
        static const uint32_t spark[3] = {0xFFE066, 0xFFFFFF, 0x7FF0FF};
        for (unsigned i = 0; i < 12U; ++i) {
            if (age > 300U && (i & 1U)) continue;
            if (age > 430U && (i % 3U)) continue;
            float a = (float)i * 0.5236f + 0.2f, r = 4.0f + (float)age * 0.032f;
            int x = (int)lroundf(s->fx_x + cosf(a) * r), y = RIM_Y + 2 + (int)lroundf(sinf(a) * r * 0.7f);
            gfx_plot(px, x, y, gfx_rgb565(spark[i % 3U]));
            if (age < 220U) gfx_plot(px, x + 1, y, gfx_rgb565(spark[i % 3U]));
        }
    }
}
static void draw_result(const basketball_view_t *v, uint64_t now)
{
    uint16_t *px = app_ui_pixels();
    gfx_gradient(px, 0, APP_CANVAS_H, WALL_TOP, WALL_LOW);
    if (v->made > 0) { /* confetti, as many pieces as shots made (up to 30) */
        static const uint32_t colors[5] = {0xFFD23F, 0xFF6B9A, 0x5CF0B0, 0x7FC8FF, 0xFFFFFF};
        unsigned pieces = v->made < 30U ? v->made : 30U;
        for (unsigned i = 0; i < pieces + 6U; ++i) {
            uint32_t h = gfx_hash(i * 311U + 1U);
            int x = (int)(h % 160U), y = (int)(((h >> 8) % 64U + (unsigned)(now / (22U + (h >> 20) % 18U))) % 64U);
            gfx_rect(px, x, y, 2, 1 + (int)(i & 1U), gfx_rgb565(colors[(h >> 16) % 5U]));
        }
    }
    /* Three stars: 10, 20 and 30 baskets. */
    static const char *const star[8] = {"....o....", "...ogo...", "..oogoo..", "ooogggooo", ".ogggggo.", "..ogggo..", ".ogo.ogo.", ".oo...oo."};
    unsigned earned = v->made >= 30U ? 3U : v->made >= 20U ? 2U : v->made >= 10U ? 1U : 0U;
    for (unsigned i = 0; i < 3U; ++i) {
        const uint16_t colors[2] = {gfx_rgb565(i < earned ? 0xC77A12 : 0x6B5A9A), gfx_rgb565(i < earned ? 0xFFD23F : 0x2A2050)}; /* "og" */
        gfx_blit(px, 9, 8, star, 80 - 4 + ((int)i - 1) * 18, 13, "og", colors, false);
    }
}

/* ---- HUD -------------------------------------------------------------------------------------------------------- */
static lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb, unsigned opa)
{
    lv_obj_t *p = app_ui_rect(parent, x, y, w, h, rgb, 5);
    lv_obj_set_style_bg_opa(p, (lv_opa_t)opa, LV_PART_MAIN);
    return p;
}
static lv_obj_t *overlay(lv_obj_t *parent) /* transparent full-screen group (app_ui_layer paints an opaque background) */
{
    lv_obj_t *obj = lv_obj_create(parent);
    app_ui_plain(obj);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, APP_WIDTH, APP_HEIGHT);
    return obj;
}
static lv_obj_t *centered(lv_obj_t *label)
{
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    return label;
}
static void build(state_t *s, lv_obj_t *root)
{
    s->top = app_ui_rect(root, 0, 0, APP_WIDTH, CANVAS_Y, WALL_TOP, 0);
    s->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->image.header.cf = LV_COLOR_FORMAT_RGB565;
    s->image.header.w = APP_CANVAS_W; s->image.header.h = APP_CANVAS_H; s->image.header.stride = APP_CANVAS_W * 2;
    s->image.data_size = APP_CANVAS_W * APP_CANVAS_H * 2; s->image.data = (const uint8_t *)app_ui_pixels();
    s->plot = lv_image_create(root);
    lv_obj_set_pos(s->plot, 0, CANVAS_Y);
    lv_image_set_src(s->plot, &s->image);
    s->bottom = app_ui_rect(root, 0, CANVAS_Y + APP_CANVAS_H, APP_WIDTH, APP_HEIGHT - CANVAS_Y - APP_CANVAS_H, FLOOR_LOW, 0);
    s->hud = overlay(root);
    s->results = overlay(root);
    pill(s->hud, 3, 1, 154, 5, 0x241A48, 255);
    s->bar = pill(s->hud, 3, 1, 154, 5, 0x47E6AE, 255);
    s->clock_pill = pill(s->hud, 3, 10, 62, 15, 0x0B0820, 200);
    s->clock_label = centered(app_ui_label(s->clock_pill, &aihook_font_12, 0, 1, 62, 14, LV_LABEL_LONG_MODE_CLIP));
    s->score_pill = pill(s->hud, 97, 10, 60, 15, 0x0B0820, 200);
    s->score_label = centered(app_ui_label(s->score_pill, &aihook_font_12, 0, 1, 60, 14, LV_LABEL_LONG_MODE_CLIP));
    lv_obj_set_style_text_color(s->score_label, app_ui_color(APP_COLOR_ACCENT), LV_PART_MAIN);
    s->hold = app_ui_rect(s->hud, 0, 0, 0, 2, APP_COLOR_ACCENT, 0); /* the hold towards the exit */
    app_ui_hide(s->hold, true);
    s->shots_pill = pill(s->hud, 3, 65, 56, 14, 0x0B0820, 150);
    s->shots_label = centered(app_ui_label(s->shots_pill, &aihook_font_12, 0, 0, 56, 14, LV_LABEL_LONG_MODE_CLIP));
    s->hint_pill = pill(s->hud, 63, 65, 94, 14, 0x2A1A5A, 190);
    s->hint = centered(app_ui_label(s->hint_pill, &aihook_font_12, 0, 0, 94, 14, LV_LABEL_LONG_MODE_CLIP));
    s->float_label = app_ui_label(s->hud, &aihook_font_16, 0, 0, 30, 20, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->float_label, app_ui_color(0xFFE066), LV_PART_MAIN);
    app_ui_text(s->float_label, "+2");
    s->title = centered(app_ui_label(s->results, &aihook_font_16, 0, 0, 160, 20, LV_LABEL_LONG_MODE_CLIP));
    app_ui_text(s->title, "时间到!");
    lv_obj_set_style_text_color(s->title, app_ui_color(APP_COLOR_ACCENT), LV_PART_MAIN);
    s->result_score = centered(app_ui_label(s->results, &aihook_font_16, 0, 31, 160, 20, LV_LABEL_LONG_MODE_CLIP));
    s->result_line = centered(app_ui_label(s->results, &aihook_font_12, 0, 51, 160, 14, LV_LABEL_LONG_MODE_CLIP));
    lv_obj_set_style_text_color(s->result_line, app_ui_color(0xC9BFE0), LV_PART_MAIN);
    s->result_hint = centered(app_ui_label(s->results, &aihook_font_12, 0, 66, 160, 14, LV_LABEL_LONG_MODE_CLIP));
    lv_obj_set_style_text_color(s->result_hint, app_ui_color(APP_COLOR_MUTED), LV_PART_MAIN);
    app_ui_text(s->result_hint, "短按菜单 / 三击菜单");
    s->ended = true; /* forces the strips to sync on the first frame */
}
static void sync_strips(state_t *s, bool ended)
{
    if (s->ended == ended) return;
    s->ended = ended;
    lv_obj_set_style_bg_color(s->bottom, app_ui_color(ended ? WALL_LOW : FLOOR_LOW), LV_PART_MAIN);
}
static void render(app_context_t *ctx)
{
    state_t *s=ctx->state; basketball_view_t view; basketball_snapshot(&s->game, ctx->now_ms, &view);
    const basketball_view_t *game=&view;
    if (!s->plot) { build(s, ctx->root); s->prev_hoop_x = game->hoop_x; }
    char text[80];
    bool ended = game->phase == BASKETBALL_RESULT;
    sync_strips(s, ended);
    app_ui_hide(s->hud, ended);
    app_ui_hide(s->results, !ended);
    if (ended) {
        snprintf(text, sizeof text, "得分 %lu", (unsigned long)game->score);
        app_ui_text(s->result_score, text);
        unsigned accuracy = game->shots ? (unsigned)(game->made * 100 / game->shots) : 0;
        snprintf(text, sizeof text, "投中 %lu/%lu  命中率 %u%%", (unsigned long)game->made, (unsigned long)game->shots, accuracy);
        app_ui_text(s->result_line, text);
        draw_result(game, ctx->now_ms);
    } else {
        /* A ball reaching the rim starts the effects (sparkles, "+2", net swish, rim rattle). */
        if (game->reached_rim && !s->was_reached) {
            s->fx_ms = ctx->now_ms; s->fx_x = game->hoop_x; s->fx_hit = game->last_hit; s->fx_live = true;
        }
        s->was_reached = game->reached_rim;
        if (s->fx_live && ctx->now_ms - s->fx_ms >= FX_MS) s->fx_live = false;
        float target = fmaxf(-4.0f, fminf(4.0f, (s->prev_hoop_x - game->hoop_x) * 6.0f)); /* the net lags behind the hoop */
        s->sway += (target - s->sway) * 0.35f;
        s->prev_hoop_x = game->hoop_x;
        if (game->in_flight && !game->reached_rim) {
            if (s->trail_n == TRAIL) { memmove(s->trail_x, s->trail_x + 1, sizeof s->trail_x - sizeof s->trail_x[0]); memmove(s->trail_y, s->trail_y + 1, sizeof s->trail_y - sizeof s->trail_y[0]); --s->trail_n; }
            s->trail_x[s->trail_n] = (int16_t)lroundf(game->ball_x); s->trail_y[s->trail_n] = (int16_t)lroundf(game->ball_y); ++s->trail_n;
        } else s->trail_n = 0;
        snprintf(text, sizeof text, "时间 %02u:%02u", game->seconds_left / 60, game->seconds_left % 60);
        app_ui_text(s->clock_label, text);
        bool urgent = game->seconds_left <= 10;
        lv_obj_set_style_text_color(s->clock_label, app_ui_color(urgent && ((ctx->now_ms / 250U) & 1U) ? 0xFF6B7A : APP_COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_width(s->bar, (int)(154U * game->seconds_left / (BASKETBALL_DURATION_MS / 1000U)) + (game->seconds_left ? 2 : 0));
        lv_obj_set_style_bg_color(s->bar, app_ui_color(urgent ? 0xFF6B7A : game->seconds_left <= 30 ? 0xFDB927 : 0x47E6AE), LV_PART_MAIN);
        snprintf(text, sizeof text, "得分 %lu", (unsigned long)game->score);
        app_ui_text(s->score_label, text);
        snprintf(text, sizeof text, "进 %lu/%lu", (unsigned long)game->made, (unsigned long)game->shots);
        app_ui_text(s->shots_label, text);
        /* The outcome is decided at release but shown only when the ball reaches the moving rim. */
        app_ui_hold_line(s->hold, app_exit_hold_progress(ctx), false, EXIT_HOLD_MS);
        const bool exit_hint = game->seconds_left >= BASKETBALL_DURATION_MS / 1000U - 8U && (ctx->now_ms / 1500U) % 2U == 1U; /* said at the start */
        app_ui_text(s->hint, !game->in_flight ? (exit_hint ? "按住2秒退出" : "按键投篮") : !game->reached_rim ? "球飞行中"
            : game->last_hit ? "命中! +2" : "没进");
        lv_obj_set_style_text_color(s->hint, app_ui_color(game->reached_rim
            ? game->last_hit ? 0x47E6AE : 0xFF8A94 : 0xC9A8FF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s->hint_pill, app_ui_color(game->reached_rim ? game->last_hit ? 0x0E4A38 : 0x4A1620 : 0x2A1A5A), LV_PART_MAIN);
        uint64_t age = s->fx_live ? ctx->now_ms - s->fx_ms : FX_MS;
        bool floating = s->fx_live && s->fx_hit && age < FX_MS;
        app_ui_hide(s->float_label, !floating);
        if (floating) {
            int x = s->fx_x > 100.0f ? (int)lroundf(s->fx_x) - 34 : (int)lroundf(s->fx_x) + 17; /* beside the backboard, not over it */
            lv_obj_set_pos(s->float_label, x, 36 - (int)(age * 22U / FX_MS));
        }
        draw_scene(s, game, ctx->now_ms);
    }
    app_ui_plot_refresh(s->plot, &s->image);
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
    app_ui_rect(parent, x + 1, y + 2, 27, 25, 0x2A1F4A, 4);                                   /* arena */
    app_ui_rect(parent, x + 1, y + 21, 27, 6, 0xB5753A, 3);                                   /* floor */
    app_ui_rect(parent, x + 7, y + 3, 15, 8, 0xE4E6F0, 1);                                    /* backboard */
    app_ui_rect(parent, x + 8, y + 4, 13, 6, 0x7288C8, 0);
    app_ui_rect(parent, x + 10, y + 11, 9, 2, 0xFF8A2A, 0);                                   /* rim */
    app_ui_rect(parent, x + 11, y + 13, 7, 2, 0xC9CEDD, 0);                                   /* net */
    lv_obj_t *ball = app_ui_rect(parent, x + 9, y + 16, 11, 11, selected ? APP_COLOR_ACCENT : 0xF08A2C, 5);
    app_ui_rect(ball, 5, 0, 1, 11, 0x573122, 0); app_ui_rect(ball, 0, 5, 11, 1, 0x573122, 0);
}
const app_descriptor_t esphook_app_basketball = {.api_version=APP_API_VERSION, .id="basketball", .title="投篮",
    .refresh_ms=33, .state_size=sizeof(state_t), .on_enter=enter, .on_leave=leave, .on_event=event,
    .on_tick=tick, .render=render, .icon=icon, .command=command};
