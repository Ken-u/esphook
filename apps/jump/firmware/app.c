#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include "freertos/FreeRTOS.h"
#include "jump_core.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 跳一跳: the whole 160 x 64 canvas is an isometric floor of coloured boxes with a little pawn on one of them; the strip
 * below it holds the best score and the streak. Holding the key squashes the pawn, letting go sends it to the next box.
 * The game itself is in jump_core.c: this file draws it and turns DOWN and UP into jp_press() and jp_release(). Jumps
 * are timed holds, never a tap race, so the global triple press stays the way out. */
#define BAND_Y 64
#define UNIT_X16 3 /* a floor unit is 1.5 px across and 0.75 px up, as the usual two-to-one isometric view */
#define BOX_T 13   /* how thick a box is, in pixels */
#define THEMES 4U

typedef struct {
    jp_t core;
    lv_obj_t *plot, *band, *score, *best, *combo, *hint, *panel, *title, *sub, *pop;
    lv_image_dsc_t image;
    uint64_t last_ms;
    int32_t cam_x16, cam_y16; /* where the floor origin is on the screen, in sixteenths of a pixel */
    uint32_t shown_key, drop_ms;
    uint8_t seen_cur, snap;
    char shown_pop[16];
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "jump state too large");
_Static_assert(offsetof(state_t, core) == 0, "tests pose the game through the core at the start of the state");
typedef struct { bool active; uint8_t phase, cur, combo; uint32_t score, best, jumps, perfects, frames; } status_t;
static status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint16_t c565(uint32_t rgb) { return gfx_rgb565(rgb); }
static int fdiv16(int v) { return (v >= 0 ? v : v - 15) / 16; }
static int scr_x(const state_t *s, int a16, int b16) { return fdiv16((int)s->cam_x16 + (a16 - b16) * UNIT_X16 / 2); }
static int scr_y(const state_t *s, int a16, int b16) { return fdiv16((int)s->cam_y16 - (a16 + b16) * UNIT_X16 / 4); }

/* ---- the camera ------------------------------------------------------------------------------------------------ */
/* The pawn's box and the next one stay in view: the camera settles on the point between them. */
static void cam_target(const state_t *s, int *tx16, int *ty16)
{
    const jp_t *g = &s->core;
    const jp_plat_t *c = jp_plat(g, 0), *n = jp_plat(g, 1);
    const bool title = g->phase == JP_TITLE || !n->hw;
    const int ma = title ? c->a * 16 : (c->a + n->a) * 8, mb = title ? c->b * 16 : (c->b + n->b) * 8;
    *tx16 = 80 * 16 - (ma - mb) * UNIT_X16 / 2;
    *ty16 = (title ? 54 : 38) * 16 + (ma + mb) * UNIT_X16 / 4;
}

/* ---- drawing --------------------------------------------------------------------------------------------------- */
static const uint32_t k_box[8] = {0xF4F1EA, 0xFF8FA6, 0xFFC447, 0x72D68B, 0x66BDF2, 0xB39BFF, 0xFF9A66, 0x45D3BE};
static const struct { uint32_t top, bottom; } k_theme[THEMES] = {
    {0xA9DCFF, 0xFFF1D2}, {0xFFB9D6, 0xFFEFC4}, {0xB7EBC8, 0xF4FFD6}, {0xC1B8FF, 0xFFDDEE}};

static void draw_sky(uint16_t *px, const state_t *s)
{
    const unsigned k = s->core.score / 8U, t = (s->core.score % 8U) * 32U;
    const uint32_t top = gfx_mix(k_theme[k % THEMES].top, k_theme[(k + 1U) % THEMES].top, t);
    const uint32_t bottom = gfx_mix(k_theme[k % THEMES].bottom, k_theme[(k + 1U) % THEMES].bottom, t);
    gfx_gradient(px, 0, APP_CANVAS_H, top, bottom);
    /* big soft shapes far away: they drift against the camera, a quarter as fast */
    for (unsigned i = 0; i < 7U; ++i) {
        const uint32_t h = gfx_hash(i * 977U + 5U);
        const int span = 260;
        int x = ((int)(h % (unsigned)span) + (int)(s->cam_x16 / 64)) % span;
        if (x < 0) x += span;
        x -= 50;
        const int y = 4 + (int)((h >> 9) % 52U) + (int)((s->cam_y16 / 64) % 7);
        const int r = 5 + (int)((h >> 17) % 9U);
        if (h & 0x100000U) gfx_tint_ellipse(px, x, y, r + 3, r + 3, 0xFFFFFF, 70);
        else gfx_tint(px, x - r, y - r, r * 2, r * 2, 0xFFFFFF, 42);
    }
}
static void draw_box(uint16_t *px, int cx, int cy, int hw, unsigned colour)
{
    const int w = hw * UNIT_X16 / 2, h = hw * UNIT_X16 / 4 > 3 ? hw * UNIT_X16 / 4 : 3;
    if (cx + w < 0 || cx - w >= APP_CANVAS_W || cy - h >= APP_CANVAS_H || cy + h + BOX_T < 0) return;
    const uint32_t base = k_box[colour & 7U];
    const uint16_t top = c565(base), left = c565(gfx_mix(base, 0x20104A, 96)), right = c565(gfx_mix(base, 0x20104A, 150));
    gfx_tri(px, cx - w, cy, cx, cy + h, cx - w, cy + BOX_T, left);
    gfx_tri(px, cx, cy + h, cx, cy + h + BOX_T, cx - w, cy + BOX_T, left);
    gfx_tri(px, cx + w, cy, cx, cy + h, cx + w, cy + BOX_T, right);
    gfx_tri(px, cx, cy + h, cx, cy + h + BOX_T, cx + w, cy + BOX_T, right);
    gfx_tri(px, cx, cy - h, cx + w, cy, cx - w, cy, top);
    gfx_tri(px, cx - w, cy, cx + w, cy, cx, cy + h, top);
    gfx_line(px, cx - w, cy, cx, cy - h, c565(gfx_mix(base, 0xFFFFFF, 120)));
    gfx_line(px, cx, cy - h, cx + w, cy, c565(gfx_mix(base, 0xFFFFFF, 70)));
    gfx_vline(px, cx, cy + h, cy + h + BOX_T, c565(gfx_mix(base, 0x20104A, 190)));
    const uint16_t mark = c565(gfx_mix(base, 0xFFFFFF, 110)), dark = c565(gfx_mix(base, 0x20104A, 44));
    switch ((colour >> 1) & 3U) { /* a pattern on the lid, so the boxes do not all look alike */
    case 1: gfx_tri(px, cx, cy - h / 2, cx + w / 2, cy, cx - w / 2, cy, mark); gfx_tri(px, cx - w / 2, cy, cx + w / 2, cy, cx, cy + h / 2, mark); break;
    case 2: gfx_ellipse(px, cx, cy, w * 2 / 3, h * 2 / 3, dark); gfx_ellipse(px, cx, cy - 1, w / 2, h / 2, mark); break;
    case 3:
        for (int i = -1; i <= 1; ++i) { gfx_plot(px, cx + i * w / 2, cy, dark); gfx_plot(px, cx + i * w / 4, cy - h / 3, dark); gfx_plot(px, cx + i * w / 4, cy + h / 3, dark); }
        break;
    default: break;
    }
}
static void draw_pawn(uint16_t *px, int x, int y, unsigned squash, int lean)
{
    const int hb = 9 - (int)(squash * 4U / 256U), bw = 4 + (int)(squash * 2U / 256U);
    gfx_ellipse(px, x, y, bw, 2, c565(0x2B2370));
    gfx_tri(px, x + lean, y - hb, x - bw, y, x + bw, y, c565(0x4E43C4));
    gfx_tri(px, x + lean, y - hb, x + 1, y, x + bw, y, c565(0x3A31A0));
    gfx_line(px, x + lean - 1, y - hb + 2, x - bw + 2, y - 1, c565(0x7C72EE));
    gfx_disc(px, x + lean, y - hb - 2, 3, c565(0x5A4FD8));
    gfx_disc(px, x + lean + 1, y - hb - 1, 2, c565(0x4437B8));
    gfx_plot(px, x + lean - 1, y - hb - 3, c565(0xE4E0FF));
    gfx_plot(px, x + lean - 2, y - hb - 2, c565(0xB6AEFF));
}
static void pawn_screen(const state_t *s, int *x, int *feet, int *lift)
{
    int a16, b16, l16;
    jp_pawn(&s->core, &a16, &b16, &l16);
    *x = scr_x(s, a16, b16);
    *feet = scr_y(s, a16, b16);
    *lift = fdiv16(l16);
}
static void draw_scene(uint16_t *px, const state_t *s)
{
    const jp_t *g = &s->core;
    draw_sky(px, s);
    int pa16, pb16, pl16;
    jp_pawn(g, &pa16, &pb16, &pl16);
    int pawn_x, feet, lift;
    pawn_screen(s, &pawn_x, &feet, &lift);
    const unsigned squash = jp_squash(g);
    const bool landing = g->phase == JP_LAND && g->result >= JP_GOOD;
    bool pawn_done = false;
    for (int which = 2; which >= -2; --which) {
        const jp_plat_t *p = jp_plat(g, which);
        if (!p->hw) continue;
        if (!pawn_done && pa16 + pb16 > (p->a + p->b) * 16 + p->hw * 32) { /* beyond this box's far corner: behind it */
            draw_pawn(px, pawn_x, feet - lift, squash, 0);
            pawn_done = true;
        }
        int cx = scr_x(s, p->a * 16, p->b * 16), cy = scr_y(s, p->a * 16, p->b * 16);
        if (which == 2 && s->drop_ms < 420U) { /* a new box drops in from above */
            const int rest = 420 - (int)s->drop_ms;
            cy -= rest * rest * 90 / (420 * 420);
        }
        if (which == (landing ? 1 : 0) && (g->phase == JP_CHARGE || g->phase == JP_LAND)) cy += (int)(squash * 3U / 256U); /* the box gives */
        draw_box(px, cx, cy, p->hw, p->color);
    }
    const bool on_box = g->phase == JP_READY || g->phase == JP_CHARGE || g->phase == JP_LAND;
    if (on_box) gfx_tint_ellipse(px, pawn_x, feet + 1, 5, 2, 0x20104A, 70);
    if (!pawn_done) {
        const int lean = g->phase == JP_FLY ? ((g->ua - g->ub) >= 0 ? 2 : -2) : 0;
        draw_pawn(px, pawn_x, feet - lift, squash, lean);
    }
    if (g->phase == JP_LAND && g->result >= JP_GOOD) {
        const unsigned t = g->phase_ms;
        const unsigned fade = t < JP_LAND_MS ? 200U - t * 200U / JP_LAND_MS : 0U;
        gfx_tint_ellipse(px, pawn_x, feet, 5 + (int)(t / 22U), 2 + (int)(t / 44U), 0xFFFFFF, fade / 2U);
        if (g->result == JP_PERFECT) {
            for (unsigned k = 0; k < 8U; ++k) {
                const unsigned a = k * 32U + 9U;
                const int rr = 6 + (int)(t / 12U);
                const int sx = pawn_x + gfx_sin256(a + 64U) * rr / 256, sy = feet - 3 + gfx_sin256(a) * rr / 512;
                gfx_tint_plot(px, sx, sy, 0xFFD23C, fade + 40U);
                gfx_tint_plot(px, sx + 1, sy, 0xFFFFFF, fade / 2U);
                gfx_tint_plot(px, sx, sy - 1, 0xFFFFFF, fade / 2U);
            }
        }
    }
    if (g->phase == JP_CHARGE) { /* the strength so far */
        const int full = 42, filled = (int)(g->charge_ms * (unsigned)full / JP_MAX_CHARGE_MS);
        gfx_rect(px, 113, 3, full + 2, 6, c565(0x2B2370));
        gfx_rect(px, 114, 4, full, 4, c565(0xE9E4FF));
        for (int x = 0; x < filled; ++x) gfx_vline(px, 114 + x, 4, 7, c565(gfx_mix(0x4ADE80, 0xFF5A4A, (unsigned)(x * 256 / full))));
    }
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
    s->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->image.header.cf = LV_COLOR_FORMAT_RGB565;
    s->image.header.w = APP_CANVAS_W; s->image.header.h = APP_CANVAS_H; s->image.header.stride = APP_CANVAS_W * 2;
    s->image.data_size = APP_CANVAS_W * APP_CANVAS_H * 2; s->image.data = (const uint8_t *)app_ui_pixels();
    s->plot = lv_image_create(root);
    lv_obj_set_pos(s->plot, 0, 0);
    lv_image_set_src(s->plot, &s->image);
    s->score = app_ui_label(root, &aihook_font_16, 5, 1, 70, 20, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->score, app_ui_color(0x3A2F96), LV_PART_MAIN);
    s->pop = app_ui_label(root, &aihook_font_12, 0, 0, 64, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->pop);
    app_ui_hide(s->pop, true);
    s->panel = pill(root, 22, 2, 116, 37, 0x1E1848, 215);
    s->title = app_ui_label(s->panel, &aihook_font_16, 0, 2, 116, 20, LV_LABEL_LONG_MODE_CLIP);
    s->sub = app_ui_label(s->panel, &aihook_font_12, 0, 21, 116, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->title); app_ui_center(s->sub);
    lv_obj_set_style_text_color(s->title, app_ui_color(0xFFD84A), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->sub, app_ui_color(0xE4E0FF), LV_PART_MAIN);
    s->band = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, APP_HEIGHT - BAND_Y, 0x1E1848, 0);
    s->best = app_ui_label(s->band, &aihook_font_12, 3, 2, 76, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->best, app_ui_color(0xE4E0FF), LV_PART_MAIN);
    s->combo = app_ui_label(s->band, &aihook_font_12, 80, 2, 77, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s->combo, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s->combo, app_ui_color(0xFFD84A), LV_PART_MAIN);
    s->hint = app_ui_label(s->band, &aihook_font_12, 0, 2, APP_WIDTH, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->hint);
    lv_obj_set_style_text_color(s->hint, app_ui_color(0xE4E0FF), LV_PART_MAIN);
    s->shown_key = 0xFFFFFFFFU;
}
static void publish(const state_t *s, bool active, bool frame)
{
    const jp_t *g = &s->core;
    portENTER_CRITICAL(&s_lock);
    s_status.active = active; s_status.phase = g->phase; s_status.cur = g->cur; s_status.combo = g->combo;
    s_status.score = g->score; s_status.best = g->best; s_status.jumps = g->jumps; s_status.perfects = g->perfects;
    if (frame) ++s_status.frames;
    portEXIT_CRITICAL(&s_lock);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    jp_init(&s->core, app_random() ^ (uint32_t)ctx->now_ms);
    s->last_ms = ctx->now_ms;
    s->snap = 1; s->drop_ms = 4000U; s->seen_cur = s->core.cur;
    int tx, ty;
    cam_target(s, &tx, &ty);
    s->cam_x16 = tx; s->cam_y16 = ty;
    portENTER_CRITICAL(&s_lock); s_status = (status_t){0}; portEXIT_CRITICAL(&s_lock);
    publish(s, true, false);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->plot) app_ui_plot_refresh(s->plot, &s->image); /* The shared canvas goes to the next app. */
    portENTER_CRITICAL(&s_lock); s_status.active = false; portEXIT_CRITICAL(&s_lock);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t now = ctx->now_ms;
    uint32_t dt = now > s->last_ms ? (uint32_t)(now - s->last_ms) : 0U;
    s->last_ms = now;
    if (dt > 100U) dt = 100U; /* a stall must not eat a jump */
    const unsigned before = s->core.phase;
    jp_advance(&s->core, dt);
    if (s->core.cur != s->seen_cur) { s->seen_cur = s->core.cur; s->drop_ms = 0; }
    else if (s->drop_ms < 4000U) s->drop_ms += dt;
    int tx, ty;
    cam_target(s, &tx, &ty);
    if (s->snap) { s->cam_x16 = tx; s->cam_y16 = ty; s->snap = 0; }
    else {
        s->cam_x16 += (tx - (int)s->cam_x16) * (int)dt / 240;
        s->cam_y16 += (ty - (int)s->cam_y16) * (int)dt / 240;
    }
    if (before != s->core.phase) publish(s, true, false);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->key == 0) {
        if (e->kind == APP_EVENT_DOWN) {
            const unsigned phase = s->core.phase;
            jp_press(&s->core);
            if (phase == JP_TITLE || phase == JP_OVER) {
                if (s->core.phase == JP_READY) s->snap = 1;
                s->seen_cur = s->core.cur; s->drop_ms = 4000U;
            }
        } else if (e->kind == APP_EVENT_UP || e->kind == APP_EVENT_CANCEL) jp_release(&s->core); /* a lost USB hold still jumps */
    }
    publish(s, true, false);
}
static void sync_hud(state_t *s, const app_context_t *ctx)
{
    const jp_t *g = &s->core;
    const bool calm = g->phase == JP_TITLE || g->phase == JP_OVER;
    const bool first = !calm && g->jumps == 0U && g->combo == 0U;
    char text[40];
    const unsigned slot = (unsigned)((ctx->now_ms / 2400U) & 1U);
    uint32_t key = gfx_hash((uint32_t)g->phase * 7919U + g->score * 31U + g->best * 131U + g->combo * 17U + (first ? 3U : 0U) + (g->new_best ? 9U : 0U));
    key = gfx_hash(key ^ slot);
    app_ui_hide(s->panel, !calm);
    app_ui_hide(s->score, calm);
    app_ui_hide(s->best, calm || first);
    app_ui_hide(s->combo, calm || first);
    app_ui_hide(s->hint, !(calm || first));
    if (key != s->shown_key) {
        s->shown_key = key;
        if (g->phase == JP_TITLE) {
            app_ui_text(s->title, "跳一跳");
            snprintf(text, sizeof text, "最高 %u 分", (unsigned)g->best);
            app_ui_text(s->sub, text);
            app_ui_text(s->hint, slot ? "三击退出" : "按键开始");
        } else if (g->phase == JP_OVER) {
            app_ui_text(s->title, g->new_best ? "新纪录!" : "游戏结束");
            snprintf(text, sizeof text, "%u 分  最高 %u", (unsigned)g->score, (unsigned)g->best);
            app_ui_text(s->sub, text);
            app_ui_text(s->hint, slot ? "三击退出" : "按键再来");
        } else {
            snprintf(text, sizeof text, "%u", (unsigned)g->score);
            app_ui_text(s->score, text);
            snprintf(text, sizeof text, "最高 %u", (unsigned)g->best);
            app_ui_text(s->best, text);
            if (g->combo >= 2U) snprintf(text, sizeof text, "连击 x%u", g->combo);
            else text[0] = 0;
            app_ui_text(s->combo, text);
            app_ui_text(s->hint, "按住蓄力 松开起跳");
        }
        lv_obj_set_style_text_color(s->title, app_ui_color(g->phase == JP_OVER && !g->new_best ? 0xFF9A8A : 0xFFD84A), LV_PART_MAIN);
    }
    /* what the last landing was worth floats up above the pawn */
    const bool pop = g->popup_ms < 900U && !calm;
    app_ui_hide(s->pop, !pop);
    if (pop) {
        if (g->result == JP_PERFECT) snprintf(text, sizeof text, "完美+%d", g->popup);
        else snprintf(text, sizeof text, "+%d", g->popup);
        if (strcmp(text, s->shown_pop)) { snprintf(s->shown_pop, sizeof s->shown_pop, "%s", text); app_ui_text(s->pop, text); }
        int x, feet, lift;
        pawn_screen(s, &x, &feet, &lift);
        const int base_y = scr_y(s, (int)g->pa16, (int)g->pb16) - 30 - (int)(g->popup_ms / 40U);
        (void)feet; (void)lift;
        lv_obj_set_pos(s->pop, x - 32 < 0 ? 0 : x - 32 > 96 ? 96 : x - 32, base_y < 0 ? 0 : base_y);
        lv_obj_set_style_text_color(s->pop, app_ui_color(g->result == JP_PERFECT ? 0xE8590C : 0x3A2F96), LV_PART_MAIN);
        lv_obj_set_style_text_opa(s->pop, (lv_opa_t)(g->popup_ms < 600U ? 255U : 255U - (g->popup_ms - 600U) * 255U / 300U), LV_PART_MAIN);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    sync_hud(s, ctx);
    draw_scene(app_ui_pixels(), s);
    app_ui_plot_refresh(s->plot, &s->image);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (strcmp(line, "robot jump status")) return false;
    status_t v;
    portENTER_CRITICAL(&s_lock); v = s_status; portEXIT_CRITICAL(&s_lock);
    char reply[160];
    snprintf(reply, sizeof reply, "active=%u phase=%u cur=%u combo=%u score=%lu best=%lu jumps=%lu perfects=%lu frames=%lu",
             v.active ? 1U : 0U, v.phase, v.cur, v.combo, (unsigned long)v.score, (unsigned long)v.best,
             (unsigned long)v.jumps, (unsigned long)v.perfects, (unsigned long)v.frames);
    (void)app_transport_write(reply);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x, y + 1, 28, 26, selected ? 0xA9DCFF : 0x6C93B3, 4);        /* the sky */
    app_ui_rect(parent, x + 1, y + 17, 13, 3, selected ? 0xFF8FA6 : 0xB36B7C, 1);   /* the box the pawn left */
    app_ui_rect(parent, x + 1, y + 20, 13, 6, selected ? 0xC8607A : 0x8A4A5A, 1);
    app_ui_rect(parent, x + 14, y + 12, 13, 3, selected ? 0x72D68B : 0x4E9A60, 1);  /* the one it jumps to */
    app_ui_rect(parent, x + 14, y + 15, 13, 11, selected ? 0x3FA462 : 0x2F7547, 1);
    app_ui_rect(parent, x + 18, y + 6, 5, 6, 0x4E43C4, 2);                           /* the pawn */
    app_ui_rect(parent, x + 18, y + 2, 5, 5, 0x5A4FD8, 3);
    app_ui_rect(parent, x + 19, y + 3, 1, 1, 0xE4E0FF, 0);
}
const app_descriptor_t esphook_app_jump = {.api_version = APP_API_VERSION, .id = "jump", .title = "跳一跳",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
