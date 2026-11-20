#include "esphook/app.h"
#include "esphook/app_haptic.h"
#include "esphook/app_gfx.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include "ski_game.h"
#include "esphook/app_best.h"
#include "esphook/app_command.h"
#include "esphook/app_sync.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Layout: an 8 px sky strip, the 64 row pixel canvas (screen y 8..71), an 8 px snow strip. HUD pills float over the
 * canvas. The rider's reference ground is canvas row GROUND_ROW; a full hop leaves ~14 rows of head room. */
#define CANVAS_Y 8
#define EXIT_HOLD_MS 3000U
#define HINT_SLOT_MS 2600U
#define GROUND_ROW 44
#define THEME_CYCLE_PX 7200.0
#define THEME_HOLD_PX 1800.0
#define THEME_BLEND_PX 700.0

typedef struct {
    ski_game_t game;
    haptic_t haptic;
    lv_obj_t *top, *plot, *bottom, *hold, *score_pill, *score, *gem_pill, *gem_count, *combo, *combo_text, *popup, *popup_text, *panel,
        *title, *line1, *line2;
    uint32_t sky_rgb, snow_rgb;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "ski state too large");
static ski_view_t s_status;

/* One colour set per time of day; a run drifts day -> sunset -> night -> dawn as the distance grows. */
typedef struct {
    uint32_t top, mid, low, sun, far_mtn, near_mtn, pine, snow_hi, snow, shade;
    unsigned stars; /* 0..256 */
} theme_t;
static const theme_t THEMES[4] = {
    {0x3F8FD8, 0x7EC4F2, 0xD3EDFF, 0xFFF4B8, 0x93B5DA, 0x6489B6, 0x2F6066, 0xFFFFFF, 0xEBF5FD, 0xBCD4EB, 0},   /* day */
    {0x3B2F6B, 0xC4627F, 0xFFB36B, 0xFFE3A6, 0x8E5E92, 0x5E4380, 0x2B2247, 0xFFEBDF, 0xF4D2D4, 0xB98EB4, 40},   /* sunset */
    {0x070C26, 0x182659, 0x3A5A9C, 0xEEF3FF, 0x2B3C72, 0x1D2A58, 0x0D1531, 0xCFDCF7, 0x97ADD8, 0x6B84B9, 256},   /* night */
    {0x2E3A7A, 0x8A6FB0, 0xF7B6A0, 0xFFDDB0, 0x7B75A9, 0x56538B, 0x25264F, 0xFFF2EB, 0xEBDAEB, 0xAAA1D0, 60},   /* dawn */
};
static theme_t theme_at(double distance)
{
    double p = fmod(distance, THEME_CYCLE_PX);
    unsigned seg = (unsigned)(p / THEME_HOLD_PX) & 3U;
    double within = p - (double)seg * THEME_HOLD_PX;
    double t = (within - (THEME_HOLD_PX - THEME_BLEND_PX)) / THEME_BLEND_PX;
    if (t <= 0.0) return THEMES[seg];
    unsigned k = (unsigned)(t * t * (3.0 - 2.0 * t) * 256.0);
    const theme_t *a = &THEMES[seg], *b = &THEMES[(seg + 1U) & 3U];
    return (theme_t){gfx_mix(a->top, b->top, k), gfx_mix(a->mid, b->mid, k), gfx_mix(a->low, b->low, k),
        gfx_mix(a->sun, b->sun, k), gfx_mix(a->far_mtn, b->far_mtn, k), gfx_mix(a->near_mtn, b->near_mtn, k),
        gfx_mix(a->pine, b->pine, k), gfx_mix(a->snow_hi, b->snow_hi, k), gfx_mix(a->snow, b->snow, k),
        gfx_mix(a->shade, b->shade, k), a->stars + (unsigned)(((int)b->stars - (int)a->stars) * (int)k / 256)};
}

static void draw_sky(uint16_t *px, const theme_t *t, unsigned now_ms)
{
    gfx_gradient(px, 0, 24, t->top, t->mid);
    gfx_gradient(px, 24, 48, t->mid, t->low);
    gfx_rect(px, 0, 48, APP_CANVAS_W, 16, gfx_rgb565(t->low));
    for (unsigned i = 0; i < 18U && t->stars > 0; ++i) { /* stars come out as it gets dark */
        uint32_t h = gfx_hash(i * 977U + 5U);
        if ((h & 0xFFU) >= t->stars) continue;
        int x = (int)((h >> 8) % 160U), y = (int)((h >> 16) % 34U);
        bool twinkle = ((now_ms / 400U + i) & 3U) == 0U;
        uint16_t c = gfx_rgb565(twinkle ? 0xFFFFFF : gfx_mix(t->top, 0xFFFFFF, 190));
        gfx_plot(px, x, y, c);
        if (twinkle) { gfx_plot(px, x - 1, y, gfx_rgb565(gfx_mix(t->top, 0xFFFFFF, 110))); gfx_plot(px, x + 1, y, gfx_rgb565(gfx_mix(t->top, 0xFFFFFF, 110))); }
    }
    /* Sun by day, moon by night: a soft halo around a disc; the moon gets a bite taken out of it. */
    gfx_sun(px, 118, 17, t->sun, t->mid, gfx_mix(t->mid, t->top, 128), t->stars > 140U);
}
static void draw_pines(uint16_t *px, int offset, uint32_t color)
{
    const uint16_t c = gfx_rgb565(color);
    for (int k = offset / 19 - 1; k <= (offset + APP_CANVAS_W) / 19 + 1; ++k) {
        uint32_t h = gfx_hash((uint32_t)k * 31U + 7U);
        int x = k * 19 + (int)(h % 7U) - offset, tall = 11 + (int)((h >> 8) % 6U);
        if (x < -8 || x > APP_CANVAS_W + 8) continue;
        gfx_peak(px, x, 50 - tall, 5, tall / 2 + 1, c);                /* upper tier */
        gfx_peak(px, x, 50 - tall + tall / 3, 9, tall - tall / 3, c);  /* lower tier */
        gfx_vline(px, x, 49, 51, c);
    }
}
static void draw_terrain(uint16_t *px, const ski_view_t *v, const theme_t *t, int base)
{
    int heights[41], rows[APP_CANVAS_W];
    uint8_t edge[APP_CANVAS_W]; /* 0 ground, 1 chasm floor, 2 left lip, 3 right lip */
    for (int k = 0; k < 41; ++k) heights[k] = (int)lroundf((ski_terrain(v->seed, (double)(base + 4 * k)) - v->cam_y) * 8.0f);
    for (int c = 0; c < APP_CANVAS_W; ++c) {
        int k = c / 4, f = c % 4, h8 = heights[k] + (heights[k + 1] - heights[k]) * f / 4;
        rows[c] = GROUND_ROW - ((h8 + 4) >> 3);
        edge[c] = 0;
    }
    for (unsigned i = 0; i < v->count; ++i) {
        const ski_thing_t *o = &v->things[i];
        if (o->kind != SKI_CHASM) continue;
        int x0 = (int)lroundf(SKI_RIDER_X + o->rel), x1 = x0 + o->w;
        for (int c = x0; c < x1; ++c)
            if (c >= 0 && c < APP_CANVAS_W) edge[c] = c == x0 ? 2 : c == x1 - 1 ? 3 : 1;
    }
    const uint16_t hi = gfx_rgb565(t->snow_hi), body = gfx_rgb565(t->snow), shade = gfx_rgb565(t->shade),
        void_top = gfx_rgb565(gfx_mix(t->shade, 0x101a30, 150)), void_deep = gfx_rgb565(0x0b1224);
    for (int c = 0; c < APP_CANVAS_W; ++c) {
        int top = rows[c] < 0 ? 0 : rows[c];
        if (edge[c] == 0) {
            for (int y = top; y < APP_CANVAS_H; ++y) {
                int depth = y - rows[c];
                uint32_t color = depth < 2 ? t->snow_hi : gfx_mix(t->snow, t->shade, (unsigned)(depth > 14 ? 256 : depth * 256 / 14));
                if (y > 50) color = gfx_mix(color, t->shade, (unsigned)((y - 50) * 256 / 13)); /* melts into the snow strip below */
                uint16_t px565 = depth < 2 ? hi : depth < 4 ? body : gfx_rgb565(color);
                if (depth > 2 && ((gfx_hash((uint32_t)(base + c) * 131U + (uint32_t)y) & 63U) == 0U)) px565 = hi; /* sparkles */
                px[y * APP_CANVAS_W + c] = px565;
            }
            continue;
        }
        /* Chasm: the walls are lit snow, the floor falls away into dark ice. */
        int rim = c > 0 ? rows[c - 1] : rows[c];
        if (edge[c] == 3) rim = rows[c];
        if (rim < 0) rim = 0;
        for (int y = rim; y < APP_CANVAS_H; ++y) {
            int depth = y - rim;
            uint16_t color = edge[c] == 2 ? (depth < 2 ? hi : shade) : edge[c] == 3 ? (depth < 2 ? hi : body)
                : (uint16_t)(depth < 6 ? void_top : void_deep);
            if (edge[c] == 1 && depth > 1 && (depth + c) % 7 == 0) color = shade; /* icicle glints */
            px[y * APP_CANVAS_W + c] = color;
        }
    }
}
static void draw_rock(uint16_t *px, const ski_thing_t *o, const ski_view_t *v, const theme_t *t)
{
    int x0 = (int)lroundf(SKI_RIDER_X + o->rel);
    int base = GROUND_ROW - (int)lroundf(o->y - v->cam_y) + 2, w = o->w, h = o->h + 2;
    const uint16_t dark = gfx_rgb565(gfx_mix(0x4A5468, t->pine, 60)), mid = gfx_rgb565(0x7B879C),
        light = gfx_rgb565(0xA9B6C9), snow = gfx_rgb565(t->snow_hi);
    for (int row = 0; row < h; ++row) {
        int span = (int)lroundf((float)w * sqrtf((float)(row + 1) / (float)h) + 0.4f), left = x0 + (w - span) / 2;
        if (span < 3) span = 3;
        int y = base - h + row;
        gfx_hline(px, left, left + span - 1, y, row == 0 ? snow : mid);
        gfx_plot(px, left, y, row == 0 ? snow : dark);
        gfx_plot(px, left + span - 1, y, dark);
        if (row > 0 && row < h - 1) gfx_plot(px, left + 1 + span / 4, y, light);
    }
    gfx_hline(px, x0 + 1, x0 + w - 2, base, dark);
}
static void draw_gem(uint16_t *px, const ski_thing_t *o, const ski_view_t *v, unsigned now_ms)
{
    static const char *const rows[] = {"..#..", ".#o#.", "#oWo#", ".#o#.", "..#.."};
    int x = (int)lroundf(SKI_RIDER_X + o->rel), y = GROUND_ROW - (int)lroundf(o->y - v->cam_y) - 2;
    bool glint = ((now_ms / 120U + (unsigned)x / 6U) & 7U) == 0U;
    const uint16_t colors[] = {gfx_rgb565(0x28B8E8), gfx_rgb565(0x7FE8FF), gfx_rgb565(glint ? 0xFFFFFF : 0xC8F6FF)};
    gfx_blit(px, 5, 5, rows, x, y, "#oW", colors, false);
}
/* Local rider coordinates: u runs along the board towards the nose, v up from the board's underside. The pivot is
 * the rider's centre of mass, which is what flies on a parabola, so flips turn about it. */
typedef struct { float cx, cy, cs, sn; } frame_t;
static void to_screen(const frame_t *f, float u, float v, int *x, int *y)
{
    float vv = v - 5.0f;
    *x = (int)lroundf(f->cx + u * f->cs - vv * f->sn);
    *y = (int)lroundf(f->cy - (u * f->sn + vv * f->cs));
}
static void seg(uint16_t *px, const frame_t *f, float u0, float v0, float u1, float v1, uint16_t color, bool thick)
{
    int x0, y0, x1, y1;
    to_screen(f, u0, v0, &x0, &y0);
    to_screen(f, u1, v1, &x1, &y1);
    if (thick) gfx_line2(px, x0, y0, x1, y1, color);
    else gfx_line(px, x0, y0, x1, y1, color);
}
static void draw_rider(uint16_t *px, float cx, float cy, float degrees, unsigned now_ms, float speed, bool wiped)
{
    const float radians = degrees * 0.0174533f;
    const frame_t f = {cx, cy, cosf(radians), sinf(radians)};
    const uint16_t board = gfx_rgb565(0xE8503A), nose = gfx_rgb565(0xFF9A6B), pants = gfx_rgb565(0x1F2B4D),
        jacket = gfx_rgb565(0xF2B92B), sleeve = gfx_rgb565(0xFFD966), skin = gfx_rgb565(0xF3C6A0),
        hat = gfx_rgb565(0xD9453B), scarf = gfx_rgb565(0xE04B4B), goggle = gfx_rgb565(0x1B2236);
    float wave = sinf((float)now_ms * 0.02f) * 1.6f, trail = 3.0f + speed * 0.04f;
    seg(px, &f, -6.0f, 0.0f, 6.0f, 0.0f, board, false);
    seg(px, &f, -6.0f, 1.0f, 6.0f, 1.0f, board, false);
    seg(px, &f, 6.0f, 1.0f, 7.5f, 2.5f, nose, false);
    seg(px, &f, -6.0f, 1.0f, -7.0f, 2.0f, nose, false);
    seg(px, &f, -2.0f, 1.5f, -1.5f, 5.0f, pants, false);
    seg(px, &f, 2.0f, 1.5f, 1.5f, 5.0f, pants, false);
    seg(px, &f, 0.0f, 5.0f, 0.0f, 10.0f, jacket, true);
    seg(px, &f, 0.0f, 9.0f, wiped ? -4.0f : -3.5f, wiped ? 12.0f : 6.5f, sleeve, false);
    seg(px, &f, 1.0f, 9.0f, wiped ? 5.0f : 4.0f, wiped ? 12.0f : 8.0f, sleeve, false);
    int hx, hy;
    to_screen(&f, 0.5f, 12.5f, &hx, &hy);
    gfx_disc(px, hx, hy, 2, skin);
    int gx, gy, px2, py2;
    to_screen(&f, 1.6f, 12.8f, &gx, &gy);
    gfx_plot(px, gx, gy, goggle);
    to_screen(&f, 0.0f, 14.6f, &px2, &py2);
    gfx_plot(px, px2, py2, hat);
    seg(px, &f, -1.8f, 14.0f, 2.2f, 14.0f, hat, false);
    seg(px, &f, -0.5f, 10.0f, -trail, 9.0f + wave, scarf, false);
    seg(px, &f, -0.5f, 9.0f, -trail * 0.8f, 7.6f + wave, scarf, false);
}
static void draw_flakes(uint16_t *px, const theme_t *t, unsigned now_ms, unsigned scroll)
{
    const uint16_t c = gfx_rgb565(gfx_mix(t->snow_hi, t->mid, 40));
    for (unsigned i = 0; i < 16U; ++i) {
        uint32_t h = gfx_hash(i * 6151U + 1U);
        unsigned depth = 1U + (h >> 24) % 3U;
        int x = (int)(((h & 0xFFU) * 160U / 256U + 160U * 4U - (scroll * depth / 4U) % 160U - now_ms * depth / 90U) % 160U);
        int y = (int)(((h >> 8) % 64U + now_ms * (8U + depth * 6U) / 1000U) % 64U);
        gfx_plot(px, x, y, c);
    }
}
static void draw_scene(uint16_t *px, const ski_view_t *v, unsigned now_ms)
{
    const theme_t t = theme_at(v->x);
    const unsigned scroll = (unsigned)v->x;
    const int base = (int)floor(v->x) - (int)SKI_RIDER_X;
    draw_sky(px, &t, now_ms);
    gfx_ridge(px, (int)(base * 0.10), 26, 44, 8, 18, 0x1111U, t.far_mtn, gfx_mix(t.far_mtn, t.snow_hi, 150));
    gfx_ridge(px, (int)(base * 0.22), 18, 47, 3, 10, 0x2222U, t.near_mtn, gfx_mix(t.near_mtn, t.snow_hi, 90));
    draw_pines(px, (int)(base * 0.40), t.pine);
    draw_terrain(px, v, &t, base);
    for (unsigned i = 0; i < v->count; ++i) {
        if (v->things[i].rel > APP_CANVAS_W || v->things[i].rel < -(SKI_RIDER_X + 30.0f)) continue;
        if (v->things[i].kind == SKI_ROCK) draw_rock(px, &v->things[i], v, &t);
        else if (v->things[i].kind == SKI_GEM) draw_gem(px, &v->things[i], v, now_ms);
    }
    draw_flakes(px, &t, now_ms, scroll);
    float sy = (float)GROUND_ROW - (v->y - v->cam_y);
    if (v->phase == SKI_OVER) {
        /* Wipe-out: the whole rider tumbles forward and slides, then dizzy stars circle the head. */
        float age = (float)v->over_age_ms, slide = 10.0f * (1.0f - expf(-age / 260.0f));
        float bounce = fabsf(sinf(age / 120.0f)) * 9.0f * expf(-age / 380.0f);
        float turn = v->angle - 270.0f * (1.0f - expf(-age / 350.0f));
        draw_rider(px, SKI_RIDER_X + slide, sy - 5.0f - bounce, turn, now_ms, 20.0f, true);
        for (int i = 0; i < 3 && v->over_age_ms > 450U; ++i) {
            float a = (float)now_ms * 0.012f + (float)i * 2.09f;
            gfx_plot(px, (int)(SKI_RIDER_X + slide + cosf(a) * 7.0f), (int)(sy - 20.0f + sinf(a) * 2.0f), gfx_rgb565(0xFFE066));
        }
        const uint16_t puff = gfx_rgb565(t.snow_hi);
        for (unsigned i = 0; i < 9U; ++i) {
            uint32_t h = gfx_hash(i + 77U);
            float r = (float)(h % 9U + 3U) * (1.0f - expf(-age / 200.0f)), a = (float)(h >> 8 & 255U) * 0.0245f;
            if (v->over_age_ms < 700U) gfx_plot(px, (int)(SKI_RIDER_X + 4.0f + cosf(a) * r), (int)(sy - 4.0f - fabsf(sinf(a)) * r), puff);
        }
    } else {
        draw_rider(px, SKI_RIDER_X, sy - 5.0f, v->angle, now_ms, (float)v->speed_pps, false);
        if (v->grounded && v->phase == SKI_PLAY) { /* powder thrown up behind the board */
            const uint16_t spray = gfx_rgb565(t.snow_hi);
            for (unsigned i = 0; i < 7U; ++i) {
                unsigned age = (now_ms / 35U + i * 5U) % 12U;
                uint32_t h = gfx_hash(i * 19U + now_ms / 105U);
                gfx_plot(px, (int)SKI_RIDER_X - 6 - (int)age * 3 / 2, (int)sy - 1 - (int)(age * (2U + h % 3U) / 7U), spray);
            }
        }
        if (v->popup_points && v->popup_age_ms < 700U) { /* sparkles burst out of a landed flip */
            const uint16_t gold = gfx_rgb565(0xFFE066), white = gfx_rgb565(0xFFFFFF);
            for (unsigned i = 0; i < 12U; ++i) {
                float a = (float)i * 0.5236f, r = 4.0f + (float)v->popup_age_ms * 0.025f;
                gfx_plot(px, (int)(SKI_RIDER_X + cosf(a) * r), (int)(sy - 9.0f - sinf(a) * r * 0.8f), i & 1U ? gold : white);
            }
        }
    }
}
static void sync_strips(state_t *s, const theme_t *t)
{
    if (s->sky_rgb != t->top) { lv_obj_set_style_bg_color(s->top, app_ui_color(t->top), LV_PART_MAIN); s->sky_rgb = t->top; }
    if (s->snow_rgb != t->shade) { lv_obj_set_style_bg_color(s->bottom, app_ui_color(t->shade), LV_PART_MAIN); s->snow_rgb = t->shade; }
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    ski_start(&s->game, ctx->now_ms, app_random());
    haptic_init(&s->haptic);
    s->game.best = app_best_get(ctx); /* the record survives leaving the game and a reboot */
    /* Jumping and spinning are done with the key, and a press starts a run: three quick presses are never an exit.
     * Holding it for three seconds (a spin ends long before that) is. */
    app_set_triple_exit(ctx, false);
    app_set_exit_hold(ctx, EXIT_HOLD_MS);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const ski_phase_t before = s->game.phase;
    const bool airborne = !s->game.grounded;
    ski_tick(&s->game, ctx->now_ms);
    ski_view_t view;
    ski_snapshot(&s->game, &view);
    haptic_base(&s->haptic, view.phase == SKI_PLAY && !view.grounded ? 35 : 0);
    if (before == SKI_PLAY && view.phase == SKI_OVER) haptic_cue(ctx, &s->haptic, -5, 180, 520);
    else if (airborne && view.grounded && view.phase == SKI_PLAY)
        haptic_cue(ctx, &s->haptic, view.popup_age_ms < 100 ? -4 : -2, 150, 330);
    haptic_pump(ctx, &s->haptic);
    if (view.phase == SKI_OVER) (void)app_best_record(ctx, view.best);
    app_shared_copy(s_status, view);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->plot) app_ui_canvas_refresh(s->plot); /* The shared canvas goes to the next app. */
    app_critical_enter(); s_status.phase = (ski_phase_t)255; app_critical_exit();
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    switch (e->kind) {
    case APP_EVENT_DOWN: (void)ski_press(&s->game, e->ms); break;
    case APP_EVENT_UP: case APP_EVENT_CANCEL: ski_release(&s->game, e->ms); break;
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
    s->top = app_ui_rect(root, 0, 0, APP_WIDTH, CANVAS_Y, THEMES[0].top, 0);
    s->plot = app_ui_canvas(root, 0, CANVAS_Y, APP_CANVAS_W, APP_CANVAS_H);
    s->bottom = app_ui_rect(root, 0, CANVAS_Y + APP_CANVAS_H, APP_WIDTH, APP_HEIGHT - CANVAS_Y - APP_CANVAS_H, THEMES[0].shade, 0);
    s->sky_rgb = THEMES[0].top; s->snow_rgb = THEMES[0].shade;
    s->score_pill = pill(root, 100, 10, 57, 15, 0x0B1630, 130);
    s->score = app_ui_label(s->score_pill, &aihook_font_12, 3, 1, 51, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s->score, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s->gem_pill = pill(root, 121, 27, 36, 15, 0x0B1630, 130);
    app_ui_rect(s->gem_pill, 4, 4, 7, 7, 0x4FD8F5, 2);
    s->gem_count = app_ui_label(s->gem_pill, &aihook_font_12, 14, 1, 20, 14, LV_LABEL_LONG_MODE_CLIP);
    s->combo = pill(root, 4, 10, 62, 15, 0xC26A00, 200);
    s->combo_text = app_ui_label(s->combo, &aihook_font_12, 4, 1, 56, 14, LV_LABEL_LONG_MODE_CLIP);
    s->popup = pill(root, 8, 48, 100, 17, 0x0B1630, 190);
    s->popup_text = app_ui_label(s->popup, &aihook_font_12, 3, 2, 94, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->popup_text);
    lv_obj_set_style_text_color(s->popup_text, app_ui_color(0xFFE066), LV_PART_MAIN);
    s->hold = app_ui_hold_bar(root, 0, APP_COLOR_ACCENT); /* the hold towards the exit */
    app_ui_hide(s->hold, true);
    s->panel = pill(root, 58, 12, 100, 54, 0x0B1630, 205);
    s->title = app_ui_label(s->panel, &aihook_font_16, 0, 4, 100, 20, LV_LABEL_LONG_MODE_CLIP);
    s->line1 = app_ui_label(s->panel, &aihook_font_12, 0, 25, 100, 14, LV_LABEL_LONG_MODE_CLIP);
    s->line2 = app_ui_label(s->panel, &aihook_font_12, 0, 38, 100, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->title); app_ui_center(s->line1); app_ui_center(s->line2);
    lv_obj_set_style_text_color(s->line2, app_ui_color(APP_COLOR_MUTED), LV_PART_MAIN);
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    ski_view_t v;
    ski_snapshot(&s->game, &v);
    if (!s->plot) build(s, ctx->root);
    char text[64];
    const theme_t t = theme_at(v.x);
    sync_strips(s, &t);
    snprintf(text, sizeof text, "%05u", v.score);
    app_ui_text(s->score, text);
    lv_obj_set_style_text_color(s->score, app_ui_color(v.new_best ? APP_COLOR_ACCENT : APP_COLOR_TEXT), LV_PART_MAIN);
    snprintf(text, sizeof text, "x%u", v.gems);
    app_ui_text(s->gem_count, text);
    bool play = v.phase == SKI_PLAY;
    app_ui_hold_line(s->hold, app_exit_hold_progress(ctx), false, EXIT_HOLD_MS);
    const bool exit_hint = (ctx->now_ms / HINT_SLOT_MS) % 2U == 1U; /* the hint of the way out alternates */
    app_ui_hide(s->score_pill, !play);
    app_ui_hide(s->gem_pill, !play || v.gems == 0);
    app_ui_hide(s->combo, !play || v.combo < 2);
    if (play && v.combo >= 2) {
        snprintf(text, sizeof text, "连击 x%u", v.combo);
        app_ui_text(s->combo_text, text);
    }
    app_ui_hide(s->popup, !(play && v.popup_points && v.popup_age_ms < 1500U));
    if (play && v.popup_points) {
        if (v.popup_perfect) snprintf(text, sizeof text, "完美翻转 +%u", v.popup_points);
        else snprintf(text, sizeof text, "翻转 x%u +%u", v.popup_flips, v.popup_points);
        app_ui_text(s->popup_text, text);
    }
    app_ui_hide(s->panel, play);
    if (v.phase == SKI_READY) {
        app_ui_text(s->title, "滑雪大冒险");
        lv_obj_set_style_text_color(s->title, app_ui_color(APP_COLOR_ACCENT), LV_PART_MAIN);
        app_ui_text(s->line1, "按键跳 按住翻转");
        snprintf(text, sizeof text, "最高 %05u", v.best);
        app_ui_text(s->line2, exit_hint ? "按住3秒退出" : text);
    } else if (v.phase == SKI_OVER) {
        app_ui_text(s->title, v.new_best ? "新纪录!" : v.crash == SKI_CRASH_ROCK ? "撞到石头" : v.crash == SKI_CRASH_FALL ? "掉进冰缝" : "落地失败");
        lv_obj_set_style_text_color(s->title, app_ui_color(v.new_best ? APP_COLOR_ACCENT : 0xFF8A7A), LV_PART_MAIN);
        snprintf(text, sizeof text, "得分 %u  翻转 %u", v.score, v.flips);
        app_ui_text(s->line1, text);
        snprintf(text, sizeof text, "最高 %05u", v.best);
        app_ui_text(s->line2, !v.can_restart ? text : exit_hint ? "按住3秒退出" : "按键重来");
    }
    draw_scene(app_ui_pixels(), &v, (unsigned)ctx->now_ms);
    app_ui_canvas_refresh(s->plot);
}
static bool command(const char *line)
{
    if (app_command_is(line, "robot ski start")) {
        (void)app_command_reply(app_command_open("ski") ? "OK ski queued" : "ERR ski queue full");
        return true;
    }
    if (app_command_is(line, "robot ski stop")) { (void)app_command_menu(); return true; }
    if (!app_command_is(line, "robot ski status")) return false;
    ski_view_t v;
    app_shared_copy(v, s_status);
    (void)app_command_replyf("phase=%u score=%u best=%u flips=%u gems=%u speed=%u", (unsigned)v.phase, v.score, v.best, v.flips,
            v.gems, v.speed_pps);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    /* A night slope with a moon, a pine and a rider in the air. */
    app_ui_rect(parent, x + 1, y + 2, 27, 25, 0x1B2A5C, 4);
    app_ui_rect(parent, x + 19, y + 5, 6, 6, 0xEEF3FF, 3);
    app_ui_rect(parent, x + 21, y + 4, 5, 5, 0x1B2A5C, 3);
    app_ui_rect(parent, x + 3, y + 12, 5, 1, 0x2F6066, 0);  /* pine */
    app_ui_rect(parent, x + 4, y + 10, 3, 2, 0x2F6066, 0);
    app_ui_rect(parent, x + 5, y + 8, 1, 2, 0x2F6066, 0);
    for (int i = 0; i < 13; ++i) { /* the slope falls to the right */
        int top = y + 17 + i / 2;
        app_ui_rect(parent, x + 1 + i * 2, top, 2, y + 27 - top + 1, i & 1 ? 0xDCE9F8 : 0xFFFFFF, 0);
    }
    app_ui_rect(parent, x + 8, y + 12, 10, 2, 0xE8503A, 1);                         /* board */
    app_ui_rect(parent, x + 11, y + 6, 3, 6, selected ? APP_COLOR_ACCENT : 0xF2B92B, 1); /* body */
    app_ui_rect(parent, x + 11, y + 3, 4, 4, 0xF3C6A0, 2);                          /* head */
}
const app_descriptor_t esphook_app_ski = {.api_version = APP_API_VERSION, .id = "ski", .title = "滑雪冒险",
    .flags = APP_SERVO_DEFAULT_HAPTIC,
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command, .menu_group = APP_MENU_GAMES};
