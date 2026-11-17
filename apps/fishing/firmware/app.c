#include "esphook/app.h"
#include "esphook/app_fish.h"
#include "esphook/app_gfx.h"
#include "esphook/app_haptic.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include "fishing_core.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

/* 钓鱼: a side view of a pier on the shared 160x64 canvas, the bottom 16 px a message strip. Press to cast, press when
 * the float goes under, then follow the ring: tap when it closes on the circle, or press and hold and let go at the
 * end of the bar. Pulls come with servo shakes when the servo has been enabled (hold a key for 0.5 s; it is never
 * enabled on its own). Game rules live in fishing_core.c. */
#define BAND_Y 64
#define WATER_Y 22
#define FLOAT_X 98
#define HAND_X 25
#define HAND_Y 12
#define TIP_X 60
#define TIP_Y 4
#define RING_X 31
#define RING_Y 43

typedef struct {
    fs_t core;
    haptic_t haptic;
    lv_obj_t *plot, *band, *line, *msg, *score, *tag, *tag_text, *name, *weight, *toast;
    lv_image_dsc_t image;
    uint32_t last_ms, nibble_ms, shown_caught, shown_phase, shown_key;
    uint8_t seen_nibbles;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "fishing state too large");
typedef struct {
    bool active;
    uint8_t phase, tier, done, need, lives, kind, state, servo;
    uint32_t caught, lost, casts, frames;
    int to;
} status_t;
static status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static const fish_style_t *style_of(unsigned tier)
{
    static const fish_style_t *const styles[FS_TIERS] = {&fish_crucian, &fish_bass, &fish_carp, &fish_tuna, &fish_arowana};
    return styles[tier < FS_TIERS ? tier : FS_TIERS - 1U];
}
static int absi_int(int v) { return v < 0 ? -v : v; }
static int lerp(int a, int b, int num, int den) { return a + (b - a) * num / den; }
static int sinp(unsigned now, unsigned div, unsigned phase) { return gfx_sin256(now / div + phase); }

/* ---- scenery ------------------------------------------------------------------------------------------------- */
static void draw_scenery(uint16_t *px, unsigned now, bool ambient)
{
    gfx_gradient(px, 0, WATER_Y, 0x66BFF0, 0xD4F0FA);
    gfx_sun(px, 138, 9, 0xFFE58A, 0xAEDDF6, 0, false);
    gfx_cloud(px, (int)((now / 260U + 30U) % 200U) - 30, 5, 22, gfx_rgb565(0xFFFFFF), gfx_rgb565(0xD8E8F4));
    gfx_cloud(px, (int)((now / 420U + 120U) % 200U) - 30, 11, 15, gfx_rgb565(0xF4FAFF), gfx_rgb565(0xD0E2F0));
    gfx_ridge(px, 0, 41, WATER_Y, 2, 8, 0x5A3, 0x5E9C92, 0x5E9C92);
    gfx_gradient(px, WATER_Y, APP_CANVAS_H, 0x40C2DE, 0x062E58);
    gfx_hline(px, 0, APP_CANVAS_W - 1, WATER_Y, gfx_rgb565(0xD6F6FF));
    for (int i = 0; i < 3; ++i) { /* light in the water */
        const int x0 = 70 + i * 34 + sinp(now, 25, (unsigned)i * 80U) * 5 / 256;
        for (int y = WATER_Y + 1; y < 56; ++y) {
            const int w = 4 + (y - WATER_Y) / 5, a = 20 - (y - WATER_Y) / 2;
            if (a > 0) gfx_tint(px, x0 + (y - WATER_Y) / 3 - w / 2, y, w, 1, 0xDFF8FF, (unsigned)a);
        }
    }
    for (unsigned i = 0; i < 16U; ++i) { /* sparkles on the surface */
        const uint32_t h = gfx_hash(i * 29U + 3U);
        const unsigned blink = (now / 90U + (h >> 8)) % 14U;
        if (blink < 2U) gfx_tint_plot(px, (int)(h % 160U), WATER_Y + 1 + (int)((h >> 16) % 3U), 0xFFFFFF, 230);
    }
    gfx_gradient(px, 58, APP_CANVAS_H, 0xB9A56E, 0x6F5C38);
    gfx_hline(px, 0, APP_CANVAS_W - 1, 58, gfx_rgb565(0xE6D49A));
    for (unsigned i = 0; i < 16U; ++i) {
        const uint32_t h = gfx_hash(i * 17U + 61U);
        gfx_rect(px, (int)(h % 158U), 60 + (int)((h >> 9) % 4U), 1 + (int)((h >> 20) % 2U), 1,
                 gfx_rgb565((h >> 17) & 1U ? 0xE8DCA8 : 0x5A4A2A));
    }
    for (int k = 0; k < 3; ++k) { /* weed along the bottom */
        const int base = 66 + k * 37 + (k == 2 ? 18 : 0), height = 14 + k * 3;
        for (int y = 58; y > 58 - height; --y) {
            const int rise = 58 - y, x = base + sinp(now, 14, (unsigned)(rise * 7 + k * 60)) * (rise / 5 + 1) / 256;
            gfx_plot(px, x, y, gfx_rgb565(((rise / 4) & 1) ? 0x58C46A : 0x2A9248));
            gfx_plot(px, x + 1, y, gfx_rgb565(0x1F7A3A));
        }
    }
    for (unsigned k = 0; k < (ambient ? 3U : 0U); ++k) { /* small fish going about their day, deep down */
        const int x = (int)((now / (70U + k * 23U) + k * 77U) % 230U) - 35, y = 46 + (int)k * 5 + sinp(now, 40, k * 90U) / 90;
        fish_draw(px, x, y, 9 + (int)k * 2, 1, gfx_sin256(now / 25U + k * 50U) / 128, &fish_tetra);
        gfx_tint(px, x - 10, y - 5, 22, 10, 0x0A4A78, 70);
    }
}
static void draw_pier(uint16_t *px, int bend, bool pulling)
{
    gfx_rect(px, 4, WATER_Y + 1, 3, 20, gfx_rgb565(0x4E3018)); /* posts, fading into the water */
    gfx_rect(px, 28, WATER_Y + 1, 3, 20, gfx_rgb565(0x4E3018));
    gfx_tint(px, 0, WATER_Y + 2, 36, 22, 0x1C7FAE, 90);
    gfx_rect(px, 0, WATER_Y - 3, 36, 4, gfx_rgb565(0x8A5A2E)); /* planks */
    gfx_hline(px, 0, 35, WATER_Y - 3, gfx_rgb565(0xC08850));
    for (int x = 5; x < 36; x += 6) gfx_vline(px, x, WATER_Y - 2, WATER_Y, gfx_rgb565(0x5E3A1C));
    const int bx = 15 - (pulling ? 2 : 0); /* the angler leans back while a fish pulls */
    gfx_rect(px, bx - 4, WATER_Y - 6, 3, 3, gfx_rgb565(0x2F4A7A)); /* legs */
    gfx_rect(px, bx + 1, WATER_Y - 6, 3, 3, gfx_rgb565(0x2F4A7A));
    gfx_rect(px, bx - 3, WATER_Y - 14, 8, 8, gfx_rgb565(0xE9A21E)); /* jacket */
    gfx_rect(px, bx - 3, WATER_Y - 14, 2, 8, gfx_rgb565(0xFFC847));
    gfx_hline(px, bx - 3, bx + 4, WATER_Y - 7, gfx_rgb565(0xB07408));
    gfx_disc(px, bx + 1, WATER_Y - 17, 3, gfx_rgb565(0xF2C49C)); /* head */
    gfx_plot(px, bx + 3, WATER_Y - 17, gfx_rgb565(0x202830));
    gfx_ellipse(px, bx + 1, WATER_Y - 20, 6, 1, gfx_rgb565(0xF0D070)); /* straw hat */
    gfx_rect(px, bx - 2, WATER_Y - 23, 6, 3, gfx_rgb565(0xE4BE5A));
    gfx_hline(px, bx - 2, bx + 3, WATER_Y - 21, gfx_rgb565(0xC8322A));
    gfx_line2(px, bx + 3, WATER_Y - 11, HAND_X - 1, HAND_Y + 1, gfx_rgb565(0xE9A21E)); /* arm */
    gfx_disc(px, HAND_X, HAND_Y, 1, gfx_rgb565(0xF2C49C));
    /* the rod: a thick handle, a thin tip that bends under a pulling fish */
    const int mx = (HAND_X + TIP_X) / 2, my = (HAND_Y + TIP_Y) / 2 + bend / 3, ty = TIP_Y + bend;
    gfx_line2(px, HAND_X, HAND_Y, mx, my, gfx_rgb565(0x3A2A1A));
    gfx_line(px, mx, my, TIP_X, ty, gfx_rgb565(0x4A3822));
    gfx_line(px, HAND_X, HAND_Y - 1, mx, my - 1, gfx_rgb565(0x7A5A38));
    gfx_disc(px, HAND_X - 2, HAND_Y + 2, 2, gfx_rgb565(0xC8CED6)); /* the reel */
    gfx_plot(px, HAND_X - 2, HAND_Y + 2, gfx_rgb565(0x4A5058));
}
static void sag_line(uint16_t *px, int x0, int y0, int x1, int y1, int sag, uint16_t c)
{
    int lx = x0, ly = y0;
    for (int i = 1; i <= 8; ++i) {
        const int x = lerp(x0, x1, i, 8), y = lerp(y0, y1, i, 8) + sag * 4 * i * (8 - i) / 64;
        gfx_line(px, lx, ly, x, y, c);
        lx = x;
        ly = y;
    }
}
static void taut_line(uint16_t *px, int x0, int y0, int x1, int y1)
{
    if (y1 <= WATER_Y || y0 >= WATER_Y) {
        gfx_line(px, x0, y0, x1, y1, gfx_rgb565(y1 <= WATER_Y ? 0xEEF6FA : 0xA8D8EA));
        return;
    }
    const int xw = x0 + (x1 - x0) * (WATER_Y - y0) / (y1 - y0);
    gfx_line(px, x0, y0, xw, WATER_Y, gfx_rgb565(0xEEF6FA));
    gfx_line(px, xw, WATER_Y, x1, y1, gfx_rgb565(0xA8D8EA));
}
static void draw_float(uint16_t *px, int x, int y)
{
    static const struct { int dy, h; uint32_t c; } rows[] = {{-6, 2, 0x2A2A2A}, {-4, 2, 0xE0302A}, {-2, 2, 0xF6F6F6}, {0, 2, 0xE0302A}};
    for (unsigned i = 0; i < 4U; ++i)
        for (int r = 0; r < rows[i].h; ++r) {
            const int yy = y + rows[i].dy + r;
            if (yy > WATER_Y + 1) continue; /* below the surface */
            gfx_rect(px, x - (i ? 1 : 0), yy, i ? 3 : 1, 1, gfx_rgb565(rows[i].c));
        }
}
static void ripples(uint16_t *px, int x, unsigned age)
{
    if (age >= 650U) return;
    for (unsigned k = 0; k < 2U; ++k) {
        if (age < k * 120U) continue;
        const unsigned t = age - k * 120U, a = t < 520U ? 200U - t * 200U / 520U : 0U;
        const int r = 2 + (int)(t / 34U);
        for (int s = -1; s <= 1; s += 2) {
            gfx_tint_plot(px, x + s * r, WATER_Y + 1, 0xFFFFFF, a);
            gfx_tint_plot(px, x + s * (r - 1), WATER_Y + 2, 0xFFFFFF, a);
            gfx_tint_plot(px, x + s * (r + 2), WATER_Y + 1, 0xFFFFFF, a / 2U);
        }
    }
}
static void heart(uint16_t *px, int x, int y, bool full)
{
    static const char *rows[4] = {".X.X.", "XXXXX", ".XXX.", "..X.."};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 5; ++c)
            if (rows[r][c] == 'X') gfx_plot(px, x + c, y + r, gfx_rgb565(full ? (r == 0 && c == 1 ? 0xFFB0C4 : 0xFF4A7A) : 0x5A4660));
}

/* ---- the prompt ring ----------------------------------------------------------------------------------------- */
static void draw_prompt(uint16_t *px, const fs_t *f, unsigned now)
{
    if (f->p.state != FP_APPROACH && f->p.state != FP_HELD) return;
    gfx_tint_ellipse(px, RING_X, RING_Y, 24, 21, 0x021830, 80);
    const bool hold = f->p.kind == FP_HOLD, held = f->p.state == FP_HELD;
    const int to = fs_to_target(f), slack = (int)f->p.win_ms + (held ? 40 : 0);
    const bool inside = absi_int(to) <= slack, perfect = inside && absi_int(to) * 100 <= slack * 40;
    const uint32_t lit = perfect ? 0x7CFFA8 : inside ? 0x4CE07C : 0xFFD84A;
    gfx_ring(px, RING_X, RING_Y, 8, 0xFFFFFF, 230); /* the target circle */
    gfx_ring(px, RING_X, RING_Y, 7, 0xFFFFFF, 120);
    if (inside) gfx_tint_ellipse(px, RING_X, RING_Y, 7, 7, lit, 90);
    if (!held) {
        const int r = 8 + fs_ring(f) * 14 / 256; /* closes in on the target */
        gfx_ring(px, RING_X, RING_Y, r, lit, 255);
        gfx_ring(px, RING_X, RING_Y, r + 1, lit, 110);
    }
    if (!hold) { gfx_disc(px, RING_X, RING_Y, 2, gfx_rgb565(lit)); return; }
    /* press and hold: the bar fills from the beat to the moment to let go (the green end) */
    const int x0 = RING_X - 20, w = 40, y = RING_Y + 15;
    const int zone = (int)(f->p.win_ms + 40U) * 2 * w / (int)(f->p.hold_ms + f->p.win_ms * 2U + 80U);
    gfx_rect(px, x0 - 1, y - 1, w + 2, 6, gfx_rgb565(0x0A1A2C));
    gfx_rect(px, x0, y, w, 4, gfx_rgb565(0x2A4058));
    gfx_rect(px, x0 + w - zone, y, zone, 4, gfx_rgb565(0x2E8C54));
    const int fill = held ? fs_hold_fill(f) * (w - zone / 2) / 256 : 0;
    gfx_rect(px, x0, y, fill, 4, gfx_rgb565(held && inside ? 0x7CFFA8 : 0xFFD84A));
    gfx_rect(px, x0 + w - zone / 2, y - 1, 1, 6, 0xFFFF);
    gfx_disc(px, RING_X, RING_Y, 2, gfx_rgb565(held ? 0x7CFFA8 : lit));
    if (held) gfx_ring(px, RING_X, RING_Y, 4 + (int)((now / 80U) & 1U), 0x7CFFA8, 200);
}

/* ---- the scene by phase --------------------------------------------------------------------------------------- */
static void fish_at(uint16_t *px, const fs_t *f, int cx, int cy, int dir, int wag)
{
    fish_draw(px, cx, cy, fs_tier(f->tier)->len, dir, wag, style_of(f->tier));
}
static void draw_card(uint16_t *px, const fs_t *f, unsigned now)
{
    gfx_gradient(px, 0, APP_CANVAS_H, 0x0D2B55, 0x1A6A98);
    for (int i = 0; i < 3; ++i) gfx_tint_ellipse(px, 104, 34, 62 - i * 16, 34 - i * 8, 0xFFE9A0, 14 + (unsigned)i * 6U);
    for (unsigned i = 0; i < 18U; ++i) { /* sparkles */
        const uint32_t h = gfx_hash(i * 41U + 9U);
        const unsigned blink = (now / 70U + (h >> 6)) % 16U;
        const int x = (int)(h % 158U), y = (int)((h >> 9) % 60U);
        if (blink < 3U) {
            gfx_tint_plot(px, x, y, 0xFFFFFF, 240);
            gfx_tint_plot(px, x - 1, y, 0xFFF0B0, 130); gfx_tint_plot(px, x + 1, y, 0xFFF0B0, 130);
            gfx_tint_plot(px, x, y - 1, 0xFFF0B0, 130); gfx_tint_plot(px, x, y + 1, 0xFFF0B0, 130);
        }
    }
    const fs_tier_t *t = fs_tier(f->tier);
    const int len = t->len * 2; /* the catch, held up for a look */
    const int bob = sinp(now, 20, 0) / 100;
    gfx_tint_ellipse(px, 106, 54, len / 2 + 6, 4, 0x000000, 34);
    gfx_tint_ellipse(px, 106, 54, len / 2 + 1, 2, 0x000000, 44);
    fish_draw(px, 106, 34 + bob, len, 1, gfx_sin256(now / 14U) / 110, style_of(f->tier));
}
static void draw_scene(uint16_t *px, state_t *s, unsigned now)
{
    const fs_t *f = &s->core;
    if (f->phase == FS_RESULT) { draw_card(px, f, now); return; }
    draw_scenery(px, now, f->phase == FS_IDLE || f->phase == FS_CAST || f->phase == FS_WAIT || f->phase == FS_LOST);
    const bool fighting = f->phase == FS_FIGHT, pulling = fighting && (f->p.state == FP_APPROACH || f->p.state == FP_HELD);
    int bend = 0;
    if (fighting) bend = 2 + (pulling ? 3 + sinp(now, 12, 0) / 128 : 0) + (int)f->combo / 4;
    else if (f->phase == FS_BITE) bend = 2;
    int tip_x = TIP_X, tip_y = TIP_Y + bend;
    switch (f->phase) {
    case FS_IDLE:
    case FS_LOST:
        sag_line(px, tip_x, tip_y, TIP_X + 1, 17, 0, gfx_rgb565(0xEEF6FA)); /* the line hangs from the tip */
        gfx_rect(px, TIP_X, 17, 2, 3, gfx_rgb565(0xC8D0D8));
        if (f->phase == FS_LOST) {
            const int away = (int)f->phase_ms / 5;
            fish_draw(px, fs_fish_x(f) + away, fs_fish_y(f), fs_tier(f->tier)->len, 1, gfx_sin256(now / 6U) / 64, style_of(f->tier));
            gfx_tint(px, 0, WATER_Y + 1, 160, 40, 0x0A4A78, f->phase_ms < 900U ? 0U : 80U);
            ripples(px, FLOAT_X + 20, f->phase_ms);
        }
        break;
    case FS_CAST: {
        const int t = (int)f->phase_ms, n = (int)FS_CAST_MS;
        const int x = lerp(tip_x, FLOAT_X, t, n), y = lerp(tip_y, WATER_Y - 1, t, n) - 18 * 4 * t * (n - t) / (n * n);
        sag_line(px, tip_x, tip_y, x, y, 2, gfx_rgb565(0xEEF6FA));
        draw_float(px, x, y + 5);
        break;
    }
    case FS_WAIT:
    case FS_BITE: {
        int dip = 0;
        const unsigned nib = (unsigned)(now - s->nibble_ms);
        if (f->phase == FS_WAIT) dip = s->nibble_ms && nib < 320U ? (int)(nib < 160U ? nib : 320U - nib) / 40 : 0;
        else dip = 2 + (int)(f->phase_ms < 120U ? f->phase_ms / 30U : 4U);
        const int fy = WATER_Y - 1 + (f->phase == FS_WAIT ? sinp(now, 18, 0) / 200 : 0) + dip;
        if (f->phase == FS_BITE) { /* the fish comes up to the bait: this one is what you will fight */
            const int near = lerp(36, 4, (int)f->phase_ms, (int)f->bite_ms);
            fish_at(px, f, FLOAT_X + near + fs_tier(f->tier)->len / 2, WATER_Y + 22, -1, gfx_sin256(now / 8U) / 100);
        }
        sag_line(px, tip_x, tip_y, FLOAT_X, WATER_Y - 5 + dip, 3, gfx_rgb565(0xEEF6FA));
        gfx_vline(px, FLOAT_X, WATER_Y + 2, WATER_Y + 22, gfx_rgb565(0x9CCADC));
        gfx_plot(px, FLOAT_X, WATER_Y + 23, gfx_rgb565(0xFFE070));
        gfx_plot(px, FLOAT_X + 1, WATER_Y + 24, gfx_rgb565(0xFFB0C0)); /* the bait */
        draw_float(px, FLOAT_X, fy);
        ripples(px, FLOAT_X, f->phase == FS_WAIT ? (s->nibble_ms ? nib : 9999U) : f->phase_ms);
        if (f->phase == FS_BITE && ((now / 90U) & 1U)) { /* a "!" over the float */
            gfx_disc(px, FLOAT_X, 8, 5, gfx_rgb565(0xE0302A));
            gfx_rect(px, FLOAT_X - 1, 5, 2, 5, 0xFFFF);
            gfx_rect(px, FLOAT_X - 1, 11, 2, 1, 0xFFFF);
        }
        break;
    }
    case FS_FIGHT:
    case FS_LAND: {
        const int dir = -1, len = fs_tier(f->tier)->len;
        int cx = fs_fish_x(f), cy = fs_fish_y(f);
        int wag = gfx_sin256(now / (pulling ? 5U : 9U)) / (pulling ? 64 : 96);
        if (f->phase == FS_LAND) { /* up and out of the water to the pier */
            const int t = (int)f->phase_ms, n = (int)FS_LAND_MS;
            const int x0 = fs_fish_x(f), y0 = fs_fish_y(f);
            cx = lerp(x0, 40, t, n);
            cy = lerp(y0, 6, t, n) - 26 * 4 * t * (n - t) / (n * n);
            wag = gfx_sin256(now / 4U) / 40;
            ripples(px, x0 - len / 2, (unsigned)t);
        } else if (pulling) cy += sinp(now, 6, 0) / 90; /* thrashing */
        const int mx = cx + dir * (len / 2), my = cy;
        taut_line(px, tip_x, tip_y, mx, my);
        fish_at(px, f, cx, cy, dir, wag);
        break;
    }
    default: break;
    }
    draw_pier(px, bend, pulling);
    if (fighting) {
        draw_prompt(px, f, now);
        gfx_tint(px, 99, 1, 60, 11, 0x000000, 100); /* lives and progress */
        for (unsigned i = 0; i < FS_LIVES; ++i) heart(px, 102 + (int)i * 7, 4, i < f->lives);
        const int w = 32, x0 = 125;
        gfx_rect(px, x0 - 1, 3, w + 2, 7, gfx_rgb565(0x0A1A2C));
        gfx_rect(px, x0, 4, w, 5, gfx_rgb565(0x2A4058));
        gfx_rect(px, x0, 4, w * (int)f->done / (int)f->need, 5, gfx_rgb565(0x4CE07C));
        for (unsigned i = 1; i < f->need; ++i) gfx_vline(px, x0 + w * (int)i / (int)f->need, 4, 8, gfx_rgb565(0x0A1A2C));
    }
}

/* ---- app ----------------------------------------------------------------------------------------------------- */
static void build(state_t *s, lv_obj_t *root)
{
    s->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->image.header.cf = LV_COLOR_FORMAT_RGB565;
    s->image.header.w = APP_CANVAS_W; s->image.header.h = APP_CANVAS_H; s->image.header.stride = APP_CANVAS_W * 2;
    s->image.data_size = APP_CANVAS_W * APP_CANVAS_H * 2; s->image.data = (const uint8_t *)app_ui_pixels();
    s->plot = lv_image_create(root);
    lv_obj_set_pos(s->plot, 0, 0);
    lv_image_set_src(s->plot, &s->image);
    s->band = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, APP_HEIGHT - BAND_Y, 0x0B1E34, 0);
    s->line = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, 1, 0x2E6E96, 0);
    s->msg = app_ui_label(s->band, &aihook_font_12, 3, 2, 126, 14, LV_LABEL_LONG_MODE_CLIP);
    s->score = app_ui_label(s->band, &aihook_font_12, 128, 2, 32, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->msg, app_ui_color(0xDDEBF7), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->score, app_ui_color(0xFFD84A), LV_PART_MAIN);
    s->tag = app_ui_rect(root, 130, 13, 29, 14, 0x2E8B57, 5);
    s->tag_text = app_ui_label(s->tag, &aihook_font_12, 0, 1, 29, 12, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->tag_text);
    app_ui_text(s->tag_text, "舵机");
    s->name = app_ui_label(root, &aihook_font_16, 6, 4, 80, 20, LV_LABEL_LONG_MODE_CLIP);
    s->weight = app_ui_label(root, &aihook_font_12, 6, 24, 80, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->name, app_ui_color(0xFFE08A), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->weight, app_ui_color(0xDDEBF7), LV_PART_MAIN);
    s->toast = app_ui_label(root, &aihook_font_16, RING_X - 24, RING_Y - 9, 48, 20, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->toast);
    app_ui_hide(s->name, true);
    app_ui_hide(s->weight, true);
    app_ui_hide(s->toast, true);
    app_ui_hide(s->tag, true);
    s->shown_phase = 0xFFFFFFFFU;
    s->shown_caught = 0xFFFFFFFFU;
    s->shown_key = 0xFFFFFFFFU;
}
static void publish(const state_t *s, bool active, bool frame)
{
    const fs_t *f = &s->core;
    const int to = fs_to_target(f);
    const haptic_state_t hs = haptic_state();
    portENTER_CRITICAL(&s_lock);
    s_status.active = active; s_status.phase = f->phase; s_status.tier = f->tier; s_status.done = f->done;
    s_status.need = f->need; s_status.lives = f->lives; s_status.kind = f->p.kind; s_status.state = f->p.state;
    s_status.servo = (uint8_t)hs; s_status.caught = f->caught; s_status.lost = f->lost; s_status.casts = f->casts;
    s_status.to = to == INT32_MAX ? 99999 : to;
    if (frame) ++s_status.frames;
    portEXIT_CRITICAL(&s_lock);
}
static void drain_shakes(state_t *s, uint64_t now)
{
    app_servo_t sv;
    app_servo_read(&sv);
    int amp;
    unsigned half, dur;
    while (fs_take_shake(&s->core, &amp, &half, &dur))
        if (sv.available && sv.armed) haptic_shake(&s->haptic, now, amp, half, dur, sv.speed_dps);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    fs_init(&s->core, app_random() ^ (uint32_t)ctx->now_ms, (uint32_t)ctx->now_ms);
    haptic_init(&s->haptic);
    s->last_ms = (uint32_t)ctx->now_ms;
    portENTER_CRITICAL(&s_lock); s_status = (status_t){0}; portEXIT_CRITICAL(&s_lock);
    publish(s, true, false);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    fs_update(&s->core, (uint32_t)ctx->now_ms);
    if (s->core.nibbles != s->seen_nibbles) { /* the float twitches for each nibble */
        if (s->core.nibbles > s->seen_nibbles) s->nibble_ms = (uint32_t)ctx->now_ms;
        s->seen_nibbles = s->core.nibbles;
    }
    drain_shakes(s, ctx->now_ms);
    haptic_pump(ctx, &s->haptic);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    haptic_stop(&s->haptic); /* the framework disarms the servo it leased to this app */
    if (s->plot) app_ui_plot_refresh(s->plot, &s->image); /* The shared canvas goes to the next app. */
    portENTER_CRITICAL(&s_lock); s_status.active = false; portEXIT_CRITICAL(&s_lock);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    const uint32_t now = (uint32_t)e->ms;
    if (e->kind == APP_EVENT_DOWN) fs_press(&s->core, now);
    else if (e->kind == APP_EVENT_UP) fs_release(&s->core, now);
    else if (e->kind == APP_EVENT_CONFIRM && s->core.phase != FS_FIGHT && s->core.phase != FS_BITE)
        (void)haptic_arm(ctx, &s->haptic); /* a deliberate half-second hold is the only thing that enables the servo */
    drain_shakes(s, e->ms);
    publish(s, true, false);
}
static void sync_hud(state_t *s)
{
    const fs_t *f = &s->core;
    const haptic_state_t hs = haptic_state();
    const char *msg = "";
    switch (f->phase) {
    case FS_IDLE: msg = hs == HAPTIC_READY ? "按键抛竿 长按启用舵机" : "按键抛竿 三击退出"; break;
    case FS_CAST: msg = "抛竿…"; break;
    case FS_WAIT: msg = "等待鱼儿上钩…"; break;
    case FS_BITE: msg = "咬钩了! 快按!"; break;
    case FS_FIGHT:
        if (f->p.state == FP_HELD) msg = "按住… 到绿区松手";
        else if (f->p.state == FP_APPROACH) msg = f->p.kind == FP_HOLD ? "按住 到条尾松手" : "圈到点 点按!";
        else msg = "鱼在挣扎…";
        break;
    case FS_LAND: msg = "收线!"; break;
    case FS_RESULT: msg = "按键再钓一次"; break;
    default: msg = "鱼跑了… 断线了"; break;
    }
    const uint32_t key = (uint32_t)f->phase << 16 | (uint32_t)f->p.state << 8 | f->p.kind << 4 | (uint32_t)hs;
    if (key != s->shown_key) { app_ui_text(s->msg, msg); s->shown_key = key; }
    if (s->shown_caught != f->caught) {
        char text[16];
        snprintf(text, sizeof text, "钓%lu", (unsigned long)f->caught);
        app_ui_text(s->score, text);
        s->shown_caught = f->caught;
    }
    app_ui_hide(s->tag, hs != HAPTIC_ARMED);
    const bool card = f->phase == FS_RESULT;
    app_ui_hide(s->name, !card);
    app_ui_hide(s->weight, !card);
    if (card && s->shown_phase != (uint32_t)f->phase) {
        char text[24];
        app_ui_text(s->name, fs_tier(f->tier)->name);
        snprintf(text, sizeof text, "%lu.%02lukg%s", (unsigned long)(f->weight_g / 1000U), (unsigned long)(f->weight_g % 1000U / 10U),
                 f->caught > 1U && f->weight_g >= f->best_g ? " 新" : "");
        app_ui_text(s->weight, text);
    }
    s->shown_phase = f->phase;
    bool toast = (f->phase == FS_FIGHT || f->phase == FS_LAND || f->phase == FS_LOST) && f->result_age_ms < 650U &&
                 f->last_result != FR_NONE && !(f->phase == FS_FIGHT && (f->p.state == FP_APPROACH || f->p.state == FP_HELD));
    app_ui_hide(s->toast, !toast);
    if (toast) {
        static const char *const words[] = {"", "完美!", "好!", "太早", "太晚"};
        app_ui_text(s->toast, words[f->last_result]);
        lv_obj_set_style_text_color(s->toast, app_ui_color(f->last_result <= FR_GOOD ? (f->last_result == FR_PERFECT ? 0x7CFFA8 : 0xFFE08A) : 0xFF6A5A), LV_PART_MAIN);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    sync_hud(s);
    draw_scene(app_ui_pixels(), s, (unsigned)ctx->now_ms);
    app_ui_plot_refresh(s->plot, &s->image);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (strcmp(line, "robot fishing status")) return false;
    status_t v;
    portENTER_CRITICAL(&s_lock); v = s_status; portEXIT_CRITICAL(&s_lock);
    char reply[256];
    snprintf(reply, sizeof reply,
             "active=%u phase=%u tier=%u done=%u need=%u lives=%u kind=%u state=%u to=%d caught=%lu lost=%lu casts=%lu servo=%u frames=%lu",
             v.active ? 1U : 0U, v.phase, v.tier, v.done, v.need, v.lives, v.kind, v.state, v.to, (unsigned long)v.caught,
             (unsigned long)v.lost, (unsigned long)v.casts, v.servo, (unsigned long)v.frames);
    (void)app_transport_write(reply);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x + 1, y + 3, 26, 22, selected ? 0x4CC8E4 : 0x2E9CC4, 4);   /* sky and water */
    app_ui_rect(parent, x + 1, y + 13, 26, 12, selected ? 0x2A86B8 : 0x1E6C9C, 4);
    app_ui_rect(parent, x + 1, y + 13, 26, 2, 0xD6F6FF, 0);
    app_ui_rect(parent, x + 2, y + 10, 8, 3, 0x8A5A2E, 0);                            /* pier */
    app_ui_rect(parent, x + 10, y + 2, 2, 12, 0x4A3822, 0);                           /* rod */
    app_ui_rect(parent, x + 11, y + 2, 11, 2, 0x4A3822, 0);
    app_ui_rect(parent, x + 21, y + 3, 1, 8, 0xEEF6FA, 0);                            /* line */
    app_ui_rect(parent, x + 19, y + 10, 5, 5, 0xE0302A, 3);                           /* float */
    app_ui_rect(parent, x + 8, y + 17, 9, 5, 0xFFB03A, 3);                            /* fish */
    app_ui_rect(parent, x + 15, y + 18, 5, 4, 0xFF8A1E, 1);
    app_ui_rect(parent, x + 10, y + 18, 2, 2, 0x101820, 1);
}
const app_descriptor_t esphook_app_fishing = {.api_version = APP_API_VERSION, .id = "fishing", .title = "钓鱼",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
