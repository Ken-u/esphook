#include "esphook/app.h"
#include "esphook/app_haptic.h"
#include "esphook/app_gfx.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include "esphook/app_best.h"
#include "esphook/app_command.h"
#include "esphook/app_sync.h"
#include "miner_core.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 黄金矿工: the whole 160 x 64 canvas is the mine under a thin strip of sky; the strip below it holds the money, the
 * level and the clock. One key fires the claw, and fires dynamite at a load that is too heavy. The game itself is in
 * miner_core.c: this file draws it and turns a press into mn_press(). */
#define BAND_Y 64
#define EXIT_HOLD_MS 2000U
#define SKIN 0xF2C29B

typedef struct {
    mn_t core;
    haptic_t haptic;
    lv_obj_t *plot, *band, *hold, *money, *level, *clock, *hint, *panel, *title, *sub, *pop;
    uint64_t last_ms;
    uint32_t shown_key;
    char shown_pop[12];
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "miner state too large");
_Static_assert(offsetof(state_t, core) == 0, "tests pose the mine through the core at the start of the state");
typedef struct { bool active; uint8_t phase, claw, level, dynamite; uint32_t score, goal, best, caught, time, frames; } status_t;
static status_t s_status;


/* ---- the mine -------------------------------------------------------------------------------------------------- */
static void draw_mine(uint16_t *px, unsigned now)
{
    gfx_gradient(px, 0, MN_ORIGIN_Y - 1, 0x6EC1EE, 0xD2F0FF); /* sky */
    gfx_cloud(px, (int)((now / 220U) % 210U) - 40, 1, 22, gfx_rgb565(0xFFFFFF), gfx_rgb565(0xBFD9EC));
    gfx_cloud(px, 150 - (int)((now / 330U) % 210U), 3, 14, gfx_rgb565(0xF2F9FF), gfx_rgb565(0xBFD9EC));
    gfx_gradient(px, MN_ORIGIN_Y - 2, APP_CANVAS_H, 0xA0713C, 0x3A2212); /* the earth, darker with depth */
    for (unsigned i = 0; i < 90U; ++i) { /* speckles and little stones */
        const uint32_t h = gfx_hash(i * 2654435U + 11U);
        const int x = (int)(h % MN_W), y = MN_ORIGIN_Y + 3 + (int)((h >> 8) % (unsigned)(APP_CANVAS_H - MN_ORIGIN_Y - 3));
        gfx_tint_plot(px, x, y, (h & 0x10000U) ? 0x20120A : 0xE0B070, 60U + (h >> 20) % 50U);
        if ((h & 0x1F0000U) == 0U) gfx_tint(px, x, y, 2, 1, 0x20120A, 80);
    }
    gfx_rect(px, 0, MN_ORIGIN_Y - 3, MN_W, 2, gfx_rgb565(0x5CBB4B)); /* grass on top */
    gfx_rect(px, 0, MN_ORIGIN_Y - 1, MN_W, 1, gfx_rgb565(0x2F7D2C));
    for (int x = 2; x < MN_W; x += 5) gfx_plot(px, x, MN_ORIGIN_Y - 4, gfx_rgb565(0x5CBB4B));
    gfx_tint(px, 0, MN_ORIGIN_Y, MN_W, 3, 0x000000, 70); /* the shade at the edge of the earth */
}
static void draw_miner(uint16_t *px, const mn_t *g, unsigned now)
{
    /* the winch on the grass, and a miner cranking it: the crank turns while the rope runs */
    const int cx = MN_ORIGIN_X, top = 2;
    gfx_rect(px, cx - 6, top, 13, 7, gfx_rgb565(0x6B4423));
    gfx_rect(px, cx - 6, top, 13, 1, gfx_rgb565(0x9A6A38));
    gfx_rect(px, cx - 6, top + 6, 13, 1, gfx_rgb565(0x3D2410));
    gfx_disc(px, cx, top + 3, 3, gfx_rgb565(0x2E2E36));
    gfx_disc(px, cx, top + 3, 2, gfx_rgb565(0x8A8E9A));
    gfx_vline(px, cx, top + 7, MN_ORIGIN_Y - 2, gfx_rgb565(0xD8D4C8)); /* the rope goes down through a hole in the grass */
    const unsigned turn = g->claw == MN_SWING ? 0U : (unsigned)(g->len16 / 20) * 3U;
    const int hx = cx + gfx_sin256(turn + 64U) * 3 / 256, hy = top + 3 + gfx_sin256(turn) * 3 / 256;
    const int bx = cx - 13, feet = MN_ORIGIN_Y - 4; /* the miner stands on the grass */
    const int bob = g->claw == MN_SWING ? 0 : (int)((now / 90U) & 1U);
    gfx_rect(px, bx, feet - 1, 2, 2, gfx_rgb565(0x2D4F9B));                   /* trousers */
    gfx_rect(px, bx + 2, feet - 1, 2, 2, gfx_rgb565(0x2D4F9B));
    gfx_rect(px, bx - 1, feet - 4 - bob, 6, 3, gfx_rgb565(0xD23A32));         /* shirt */
    gfx_rect(px, bx, feet - 6 - bob, 4, 2, gfx_rgb565(SKIN));                 /* face */
    gfx_plot(px, bx + 2, feet - 5 - bob, gfx_rgb565(0x2A1C14));
    gfx_rect(px, bx - 1, feet - 8 - bob, 6, 2, gfx_rgb565(0xF2C62A));         /* helmet with a lamp */
    gfx_plot(px, bx + 5, feet - 7 - bob, gfx_rgb565(0xFFF2A0));
    gfx_line(px, bx + 4, feet - 3 - bob, hx, hy, gfx_rgb565(SKIN));          /* the arm to the crank */
    gfx_plot(px, hx, hy, gfx_rgb565(0xE8E8F0));
}
static void draw_rope_claw(uint16_t *px, const mn_t *g, unsigned now)
{
    int tx, ty, dx, dy;
    mn_tip(g, &tx, &ty);
    mn_dir(g, &dx, &dy);
    gfx_line(px, MN_ORIGIN_X, MN_ORIGIN_Y - 1, tx, ty, gfx_rgb565(0xD8D4C8));
    gfx_line(px, MN_ORIGIN_X + 1, MN_ORIGIN_Y, tx + 1, ty, gfx_rgb565(0x7A766C)); /* the rope has a shadow */
    const int px_x = -dy, px_y = dx; /* perpendicular, in 1/256 */
    const bool open = g->claw != MN_RETRACT || g->hooked < 0;
    const int w = open ? 5 : 2;
    const int hubx = tx - dx * 4 / 256, huby = ty - dy * 4 / 256;
    gfx_disc(px, hubx, huby, 2, gfx_rgb565(0x9AA0AE));
    gfx_plot(px, hubx - 1, huby - 1, gfx_rgb565(0xFFFFFF));
    for (int side = -1; side <= 1; side += 2) {
        const int ex = tx + side * px_x * w / 256, ey = ty + side * px_y * w / 256;
        const int mx = hubx + side * px_x * (w + 1) / 256 + dx * 2 / 256, my = huby + side * px_y * (w + 1) / 256 + dy * 2 / 256;
        gfx_line(px, hubx, huby, mx, my, gfx_rgb565(0xC9CEDA));
        gfx_line(px, mx, my, ex + dx * 2 / 256, ey + dy * 2 / 256, gfx_rgb565(0xE8ECF4));
        gfx_plot(px, ex + dx * 2 / 256, ey + dy * 2 / 256, gfx_rgb565(0xFFFFFF));
    }
    (void)now;
}
static void sparkle(uint16_t *px, int x, int y, unsigned phase)
{
    if (phase > 6U) return;
    const unsigned size = phase < 3U ? phase : 6U - phase;
    gfx_tint_plot(px, x, y, 0xFFFFFF, 220);
    for (unsigned k = 1; k <= size; ++k) {
        gfx_tint_plot(px, x - (int)k, y, 0xFFFFFF, 180 / k); gfx_tint_plot(px, x + (int)k, y, 0xFFFFFF, 180 / k);
        gfx_tint_plot(px, x, y - (int)k, 0xFFFFFF, 180 / k); gfx_tint_plot(px, x, y + (int)k, 0xFFFFFF, 180 / k);
    }
}
static void draw_item(uint16_t *px, const mn_item_t *it, unsigned index, unsigned now)
{
    const int x = it->x, y = it->y, r = mn_info(it->kind)->radius;
    const uint32_t seed = gfx_hash(index * 31U + (uint32_t)it->kind);
    switch (it->kind) {
    case MN_GOLD_S: case MN_GOLD_M: case MN_GOLD_L:
        gfx_disc(px, x, y, r, gfx_rgb565(0x8E5F0C));
        for (unsigned k = 0; k < 3U; ++k) { /* a lumpy nugget */
            const unsigned a = (seed >> (k * 5U)) & 31U * 8U;
            gfx_disc(px, x + gfx_sin256(a + 64U) * (r - 1) / 256, y + gfx_sin256(a) * (r - 1) / 256, (r + 2) / 3 + 1, gfx_rgb565(0x8E5F0C));
        }
        gfx_disc(px, x, y, r - 1 > 1 ? r - 1 : 1, gfx_rgb565(0xE9A81B));
        gfx_disc(px, x - 1, y - 1, r - 2 > 1 ? r - 2 : 1, gfx_rgb565(0xFBCB2E));
        gfx_disc(px, x - r / 3, y - r / 3, r / 3 > 1 ? r / 3 : 1, gfx_rgb565(0xFFF0A0));
        gfx_tint(px, x + r / 3, y + r / 3, 2, 1, 0x4A2E06, 150);
        sparkle(px, x - r / 3, y - r / 3, (now / 70U + index * 13U) % 40U);
        break;
    case MN_STONE_S: case MN_STONE_L:
        gfx_disc(px, x, y, r, gfx_rgb565(0x464A58));
        gfx_disc(px, x - 1, y - 1, r - 1, gfx_rgb565(0x7C8294));
        gfx_disc(px, x - r / 3, y - r / 3, r / 3 > 1 ? r / 3 : 1, gfx_rgb565(0xB5BAC8));
        gfx_line(px, x - r / 2 + 1, y + 1, x + 1, y + r / 2, gfx_rgb565(0x353844));
        gfx_line(px, x + 1, y - r / 2, x + r / 2, y - 1, gfx_rgb565(0x353844));
        break;
    case MN_DIAMOND:
        gfx_tri(px, x, y - 4, x - 4, y, x + 4, y, gfx_rgb565(0xA8F4FF));
        gfx_tri(px, x - 4, y, x + 4, y, x, y + 5, gfx_rgb565(0x2EB4E6));
        gfx_tri(px, x - 1, y - 3, x - 3, y, x, y, gfx_rgb565(0xFFFFFF));
        gfx_hline(px, x - 4, x + 4, y, gfx_rgb565(0x6FD6F2));
        sparkle(px, x + 3, y - 3, (now / 60U + index * 7U) % 22U);
        break;
    case MN_BAG:
        gfx_disc(px, x, y + 1, r, gfx_rgb565(0x5A3A1C));
        gfx_disc(px, x - 1, y, r - 1, gfx_rgb565(0xA9733C));
        gfx_rect(px, x - 2, y - r - 1, 5, 3, gfx_rgb565(0x5A3A1C));
        gfx_rect(px, x - 3, y - r + 1, 7, 1, gfx_rgb565(0xC8372D)); /* the cord */
        gfx_rect(px, x - 1, y - 1, 2, 2, gfx_rgb565(0xFFE070));
        gfx_plot(px, x, y + 2, gfx_rgb565(0xFFE070));
        break;
    default: /* the barrel */
        gfx_rect(px, x - 5, y - 5, 11, 11, gfx_rgb565(0x7A1410));
        gfx_rect(px, x - 4, y - 4, 9, 9, gfx_rgb565(0xD5352B));
        gfx_rect(px, x - 4, y - 1, 9, 3, gfx_rgb565(0xF0E2D0));
        gfx_rect(px, x - 3, y, 1, 1, gfx_rgb565(0x3A2A24)); gfx_rect(px, x - 1, y - 1, 1, 3, gfx_rgb565(0x3A2A24)); gfx_rect(px, x + 1, y - 1, 1, 3, gfx_rgb565(0x3A2A24)); gfx_rect(px, x + 3, y, 1, 1, gfx_rgb565(0x3A2A24));
        gfx_hline(px, x - 4, x + 4, y - 4, gfx_rgb565(0xFF7A66));
        gfx_line(px, x, y - 5, x + 2, y - 8, gfx_rgb565(0xB89060));
        gfx_tint_plot(px, x + 2, y - 8, (now / 80U) & 1U ? 0xFFE060 : 0xFF8020, 255);
        break;
    }
}
static void draw_blast(uint16_t *px, const mn_t *g)
{
    if (g->boom_ms >= 520U) return;
    const unsigned t = g->boom_ms;
    const int r = 3 + (int)(t < 260U ? t * (MN_BLAST_RADIUS - 3) / 260U : MN_BLAST_RADIUS - 3);
    const unsigned fade = t < 200U ? 256U : 256U - (t - 200U) * 256U / 320U;
    gfx_tint_ellipse(px, g->boom_x, g->boom_y, r + 3, r + 2, 0x403838, fade / 2U);
    gfx_tint_ellipse(px, g->boom_x, g->boom_y, r, r * 9 / 10, 0xFF7A1A, fade);
    gfx_tint_ellipse(px, g->boom_x, g->boom_y, r * 2 / 3, r * 2 / 3, 0xFFD040, fade);
    gfx_tint_ellipse(px, g->boom_x, g->boom_y, r / 3, r / 3, 0xFFFFFF, fade);
    for (unsigned k = 0; k < 8U && t < 360U; ++k) { /* flying bits */
        const unsigned a = k * 32U + 11U;
        const int rr = r + (int)(t / 20U);
        gfx_plot(px, g->boom_x + gfx_sin256(a + 64U) * rr / 256, g->boom_y + gfx_sin256(a) * rr / 256, gfx_rgb565(0xFFC040));
    }
}
static void draw_dynamite(uint16_t *px, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        const int x = 148 + (int)i * 5;
        gfx_rect(px, x, MN_ORIGIN_Y - 9, 3, 6, gfx_rgb565(0xD5352B));
        gfx_rect(px, x, MN_ORIGIN_Y - 7, 3, 1, gfx_rgb565(0xF0E2D0));
        gfx_plot(px, x + 1, MN_ORIGIN_Y - 10, gfx_rgb565(0xB89060));
    }
}
static void draw_scene(uint16_t *px, const mn_t *g, unsigned now)
{
    draw_mine(px, now);
    for (unsigned i = 0; i < g->count; ++i)
        if (g->items[i].alive && (int)i != g->hooked) draw_item(px, &g->items[i], i, now);
    draw_miner(px, g, now);
    draw_rope_claw(px, g, now);
    if (g->hooked >= 0) draw_item(px, &g->items[g->hooked], (unsigned)g->hooked, now);
    draw_blast(px, g);
    draw_dynamite(px, g->dynamite);
}

/* ---- app ------------------------------------------------------------------------------------------------------- */
static lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb, unsigned opa)
{
    lv_obj_t *p = app_ui_rect(parent, x, y, w, h, rgb, 5);
    lv_obj_set_style_bg_opa(p, (lv_opa_t)opa, LV_PART_MAIN);
    return p;
}
static void build(state_t *s, lv_obj_t *root)
{
    s->plot = app_ui_canvas(root, 0, 0, APP_CANVAS_W, APP_CANVAS_H);
    s->pop = app_ui_label(root, &aihook_font_12, 90, 12, 60, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->pop, app_ui_color(0xFFE04A), LV_PART_MAIN);
    app_ui_hide(s->pop, true);
    s->panel = pill(root, 22, 7, 116, 40, 0x1A1008, 215);
    s->title = app_ui_label(s->panel, &aihook_font_16, 0, 3, 116, 20, LV_LABEL_LONG_MODE_CLIP);
    s->sub = app_ui_label(s->panel, &aihook_font_12, 0, 23, 116, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->title); app_ui_center(s->sub);
    lv_obj_set_style_text_color(s->title, app_ui_color(0xFFD040), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->sub, app_ui_color(0xE8D8B8), LV_PART_MAIN);
    s->band = app_ui_band(root, 0x1A1008, 0x8A6420);
    s->money = app_ui_label(s->band, &aihook_font_12, 3, 2, 78, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->money, app_ui_color(0xFFD040), LV_PART_MAIN);
    s->level = app_ui_label(s->band, &aihook_font_12, 82, 2, 36, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->level, app_ui_color(0xE8D8B8), LV_PART_MAIN);
    s->clock = app_ui_label(s->band, &aihook_font_12, 120, 2, 38, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s->clock, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s->hint = app_ui_label(s->band, &aihook_font_12, 0, 2, APP_WIDTH, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->hint);
    lv_obj_set_style_text_color(s->hint, app_ui_color(0xE8D8B8), LV_PART_MAIN);
    s->hold = app_ui_hold_bar(root, BAND_Y, APP_COLOR_ACCENT);
    app_ui_hide(s->hold, true);
    s->shown_key = 0xFFFFFFFFU;
}
static void publish(const state_t *s, bool active, bool frame)
{
    const mn_t *g = &s->core;
    app_critical_enter();
    s_status.active = active; s_status.phase = g->phase; s_status.claw = g->claw; s_status.level = g->level;
    s_status.dynamite = g->dynamite; s_status.score = g->score; s_status.goal = g->goal; s_status.best = g->best;
    s_status.caught = g->caught; s_status.time = mn_seconds(g);
    if (frame) ++s_status.frames;
    app_critical_exit();
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    mn_init(&s->core, app_random() ^ (uint32_t)ctx->now_ms);
    haptic_init(&s->haptic);
    s->core.best = app_best_get(ctx); /* the record survives leaving the game and a reboot */
    s->last_ms = ctx->now_ms;
    /* The claw is fired by a tap: three quick taps are never an exit, holding the key for two seconds is. */
    app_set_triple_exit(ctx, false);
    app_set_exit_hold(ctx, EXIT_HOLD_MS);
    app_critical_enter(); s_status = (status_t){0}; app_critical_exit();
    publish(s, true, false);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->plot) app_ui_canvas_refresh(s->plot); /* The shared canvas goes to the next app. */
    app_critical_enter(); s_status.active = false; app_critical_exit();
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t now = ctx->now_ms;
    uint32_t dt = now > s->last_ms ? (uint32_t)(now - s->last_ms) : 0U;
    s->last_ms = now;
    if (dt > 100U) dt = 100U; /* a stall must not throw the claw across the mine */
    const unsigned before = s->core.phase;
    mn_advance(&s->core, dt);
    int dx, dy;
    mn_dir(&s->core, &dx, &dy);
    haptic_pan(&s->haptic, s->core.phase == MN_PLAY ? dx * 65 / 256 : 0);
    haptic_pump(ctx, &s->haptic);
    if (s->core.phase == MN_CLEAR || s->core.phase == MN_OVER) (void)app_best_record(ctx, s->core.best);
    if (before != s->core.phase) publish(s, true, false);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->kind == APP_EVENT_DOWN && e->key == 0) mn_press(&s->core);
    publish(s, true, false);
}
static void sync_hud(state_t *s, const app_context_t *ctx)
{
    const mn_t *g = &s->core;
    const bool play = g->phase == MN_PLAY;
    char text[40];
    uint32_t key = (uint32_t)g->phase << 28 | (uint32_t)g->level << 22 | (uint32_t)(play ? mn_seconds(g) : 0U) << 14 |
                   (uint32_t)(g->score & 0x3FFFU);
    key ^= gfx_hash(g->goal * 5U + g->best + g->new_best);
    app_ui_hide(s->money, !play); app_ui_hide(s->level, !play); app_ui_hide(s->clock, !play);
    app_ui_hide(s->panel, play);
    app_ui_hide(s->hint, play);
    const unsigned slot = (unsigned)((ctx->now_ms / 2400U) & 1U);
    key ^= slot << 13;
    if (key != s->shown_key) {
        s->shown_key = key;
        switch (g->phase) {
        case MN_TITLE:
            app_ui_text(s->title, "黄金矿工");
            snprintf(text, sizeof text, "最高 $%u", (unsigned)g->best);
            app_ui_text(s->sub, text);
            app_ui_text(s->hint, slot ? "按住2秒退出" : "按键出发");
            break;
        case MN_INTRO:
            snprintf(text, sizeof text, "第%u关", g->level);
            app_ui_text(s->title, text);
            snprintf(text, sizeof text, "目标 $%u", (unsigned)g->goal);
            app_ui_text(s->sub, text);
            app_ui_text(s->hint, "炸药可炸掉太重的东西");
            break;
        case MN_CLEAR:
            app_ui_text(s->title, "过关!");
            snprintf(text, sizeof text, "已有 $%u", (unsigned)g->score);
            app_ui_text(s->sub, text);
            app_ui_text(s->hint, "按键继续");
            break;
        case MN_OVER:
            app_ui_text(s->title, g->new_best ? "新纪录!" : "时间到");
            snprintf(text, sizeof text, "$%u 最高 $%u", (unsigned)g->score, (unsigned)g->best);
            app_ui_text(s->sub, text);
            app_ui_text(s->hint, slot ? "按住2秒退出" : "按键再来");
            break;
        default:
            snprintf(text, sizeof text, "$%u/%u", (unsigned)g->score, (unsigned)g->goal);
            app_ui_text(s->money, text);
            snprintf(text, sizeof text, "第%u关", g->level);
            app_ui_text(s->level, text);
            snprintf(text, sizeof text, "%us", mn_seconds(g));
            app_ui_text(s->clock, text);
            break;
        }
        lv_obj_set_style_text_color(s->title, app_ui_color(g->phase == MN_OVER && !g->new_best ? 0xFF8A7A : 0xFFD040), LV_PART_MAIN);
    }
    if (play) {
        lv_obj_set_style_text_color(s->clock, app_ui_color(mn_seconds(g) <= 10U && (ctx->now_ms / 250U) % 2U ? 0xFF6A5A : 0xE8D8B8), LV_PART_MAIN);
        const bool counted = g->score >= g->level_base + g->goal;
        lv_obj_set_style_text_color(s->money, app_ui_color(counted ? 0x7CFF7C : 0xFFD040), LV_PART_MAIN);
    }
    /* what the last load was worth floats up from the winch */
    const bool pop = g->pop_ms < 1000U && g->phase == MN_PLAY;
    app_ui_hide(s->pop, !pop);
    if (pop) {
        if (g->pop_kind) snprintf(text, sizeof text, "炸药+1");
        else snprintf(text, sizeof text, "+%d", g->pop_value);
        if (strcmp(text, s->shown_pop)) { snprintf(s->shown_pop, sizeof s->shown_pop, "%.*s", (int)sizeof s->shown_pop - 1, text); app_ui_text(s->pop, text); }
        lv_obj_set_pos(s->pop, 90, 14 - (int)(g->pop_ms / 80U));
        lv_obj_set_style_text_opa(s->pop, (lv_opa_t)(g->pop_ms < 700U ? 255U : 255U - (g->pop_ms - 700U) * 255U / 300U), LV_PART_MAIN);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    sync_hud(s, ctx);
    draw_scene(app_ui_pixels(), &s->core, (unsigned)ctx->now_ms);
    app_ui_canvas_refresh(s->plot);
    app_ui_hold_line(s->hold, app_exit_hold_progress(ctx), false, EXIT_HOLD_MS);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot miner status")) return false;
    status_t v;
    app_shared_copy(v, s_status);
    (void)app_command_replyf("active=%u phase=%u claw=%u level=%u dyn=%u score=%lu goal=%lu best=%lu caught=%lu time=%lu frames=%lu",
            v.active ? 1U : 0U, v.phase, v.claw, v.level, v.dynamite, (unsigned long)v.score, (unsigned long)v.goal,
            (unsigned long)v.best, (unsigned long)v.caught, (unsigned long)v.time, (unsigned long)v.frames);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x, y + 20, 28, 8, selected ? 0x7A4E26 : 0x5E3C1E, 3);   /* the earth */
    app_ui_rect(parent, x, y + 1, 28, 4, selected ? 0x7EC8F0 : 0x5E94B8, 2);    /* the sky */
    app_ui_rect(parent, x + 13, y + 3, 2, 10, 0xD8D4C8, 0);                      /* rope */
    app_ui_rect(parent, x + 10, y + 12, 8, 3, 0xBFC4D2, 1);                      /* claw */
    app_ui_rect(parent, x + 9, y + 14, 2, 6, 0xE8ECF4, 0);
    app_ui_rect(parent, x + 17, y + 14, 2, 6, 0xE8ECF4, 0);
    app_ui_rect(parent, x + 8, y + 17, 12, 9, 0xF7C928, 4);                      /* the nugget */
    app_ui_rect(parent, x + 10, y + 19, 4, 3, 0xFFF3A0, 1);
}
const app_descriptor_t esphook_app_miner = {.api_version = APP_API_VERSION, .id = "miner", .title = "黄金矿工",
    .flags = APP_SERVO_DEFAULT_HAPTIC,
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command, .menu_group = APP_MENU_GAMES};
