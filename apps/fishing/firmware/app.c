#include "esphook/app_math.h"
#include "esphook/app.h"
#include "esphook/app_fish.h"
#include "esphook/app_gfx.h"
#include "esphook/app_haptic.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include "fishing_core.h"
#include "esphook/app_command.h"
#include "esphook/app_sync.h"
#include <stdio.h>
#include <string.h>

/* 钓鱼: first person on the shared 160x64 canvas, the bottom 16 px a message strip. You stand on the bank looking
 * across the water, the rod comes in from the bottom right and the float is what there is to watch: press to cast,
 * press when it goes under, then follow the ring around it (tap when it closes on the circle, or press and hold and
 * let go at the end of the bar). The fish is never drawn in the water, only the float that it drags around; it shows
 * up when it is landed. Pulls come with servo shakes when the servo has been enabled (hold a key for 0.5 s; it is
 * never enabled on its own). Game rules live in fishing_core.c. */
#define BAND_Y 64
#define HORIZON 20
#define NEAR_Y 47
#define FAR_Y 27
#define CENTER_X 80
#define REST_X 132 /* where the core puts the fish at the start of a fight, and so where the float lies */
#define REST_Y 44
#define GRIP_X 154
#define GRIP_Y 66
#define TIP_X 92
#define TIP_Y 15
#define SUN_X 34
#define SUN_Y 9
#define EXIT_HOLD_MS 2000U
#define HINT_SLOT_MS 2600U
#define TRAIL 3

typedef struct { int x, y, size; } spot_t;
typedef struct {
    fs_t core;
    haptic_t haptic;
    lv_obj_t *plot, *band, *hold, *msg, *score, *tag, *tag_text, *name, *weight, *toast;
    uint32_t last_ms, nibble_ms, shown_caught, shown_phase, shown_key, trail_ms[TRAIL];
    int16_t trail_x[TRAIL], trail_y[TRAIL], mark_x, mark_y;
    uint8_t seen_nibbles, down_in_fight;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "fishing state too large");
typedef struct {
    bool active;
    uint8_t phase, tier, done, need, lives, kind, state, servo;
    uint32_t caught, lost, casts, frames;
    int to;
} status_t;
static status_t s_status;

static const fish_style_t *style_of(unsigned tier)
{
    static const fish_style_t *const styles[FS_TIERS] = {&fish_crucian, &fish_bass, &fish_carp, &fish_tuna, &fish_arowana};
    return styles[tier < FS_TIERS ? tier : FS_TIERS - 1U];
}
static int sinp(unsigned now, unsigned div, unsigned phase) { return gfx_sin256(now / div + phase); }
/* The core's fish coordinates (x: distance from the angler, y: sideways) as a point on the water: far is high on
 * the screen, small and narrow, near is low, big and wide. */
static spot_t spot_of(int fx, int fy)
{
    const int far_t = app_clamp((fx - FS_FISH_X_MIN) * 256 / (FS_FISH_X_MAX - FS_FISH_X_MIN), 0, 256);
    const int half = 18 + 30 * (256 - far_t) / 256;
    const int side = app_clamp((fy - (FS_FISH_Y_MIN + FS_FISH_Y_MAX) / 2) * 256 / ((FS_FISH_Y_MAX - FS_FISH_Y_MIN) / 2), -256, 256);
    return (spot_t){CENTER_X + half * side / 256, NEAR_Y + (FAR_Y - NEAR_Y) * far_t / 256, far_t > 235 ? 0 : far_t > 110 ? 1 : 2};
}

/* ---- scenery ------------------------------------------------------------------------------------------------- */
static void draw_birds(uint16_t *px, unsigned now)
{
    for (unsigned k = 0; k < 2U; ++k) {
        const int x = (int)((now / (150U + k * 40U) + k * 90U) % 210U) - 25, y = 5 + (int)k * 6 + sinp(now, 70, k * 60U) / 128;
        const uint16_t c = gfx_rgb565(0x34495E);
        const int up = (int)((now / 170U + k) & 1U);
        gfx_plot(px, x - 2, y + up, c); gfx_plot(px, x - 1, y + 1 - up, c); gfx_plot(px, x, y + up + 1, c);
        gfx_plot(px, x + 1, y + 1 - up, c); gfx_plot(px, x + 2, y + up, c);
    }
}
static void draw_scenery(uint16_t *px, unsigned now)
{
    gfx_gradient(px, 0, HORIZON, 0x5EB4EE, 0xCFEFFA);
    gfx_sun(px, SUN_X, SUN_Y, 0xFFE58A, 0xAEDDF6, 0, false);
    gfx_cloud(px, (int)((now / 260U + 30U) % 200U) - 30, 4, 22, gfx_rgb565(0xFFFFFF), gfx_rgb565(0xD8E8F4));
    gfx_cloud(px, (int)((now / 420U + 120U) % 200U) - 30, 9, 15, gfx_rgb565(0xF4FAFF), gfx_rgb565(0xD0E2F0));
    draw_birds(px, now);
    gfx_ridge(px, 0, 34, HORIZON, 2, 6, 0x5A3, 0x6AA6A0, 0x6AA6A0); /* the far shore */
    gfx_ridge(px, 17, 22, HORIZON, 1, 3, 0x91, 0x4C8A7E, 0x4C8A7E);
    gfx_gradient(px, HORIZON, APP_CANVAS_H, 0x86D6EC, 0x0A4C7C);
    gfx_hline(px, 0, APP_CANVAS_W - 1, HORIZON, gfx_rgb565(0xE6FAFF));
    gfx_tint(px, 0, HORIZON + 1, APP_CANVAS_W, 3, 0x2F6E78, 70); /* the shore in the water */
    for (int y = HORIZON + 1; y < 46; y += 2) { /* the sun's glitter */
        const int w = 1 + (y - HORIZON) / 3, jitter = sinp(now, 90, (unsigned)y * 11U) * 2 / 256;
        gfx_tint(px, SUN_X - w + jitter, y, w * 2, 1, 0xFFF4C0, 70U + (unsigned)(gfx_sin256(now / 60U + (unsigned)y * 17U) + 256) / 8U);
    }
    static const uint8_t rows[7] = {22, 25, 29, 34, 40, 47, 55};
    for (unsigned k = 0; k < 7U; ++k) { /* ripples of light, longer and quicker the nearer they are */
        const int len = 3 + (int)k * 2;
        const unsigned speed = 130U - k * 12U;
        for (unsigned j = 0; j < 5U; ++j) {
            const uint32_t h = gfx_hash(k * 13U + j * 7U + 5U);
            const int x = (int)((h % 190U + now / speed) % (160U + (unsigned)len * 2U)) - len;
            gfx_tint(px, x, rows[k], len, 1, 0xFFFFFF, 36U + k * 6U);
            gfx_tint(px, x + 1, rows[k] + 1, len - 1, 1, 0x04304E, 44);
        }
    }
    for (unsigned i = 0; i < 14U; ++i) { /* sparkles far out */
        const uint32_t h = gfx_hash(i * 29U + 3U);
        if ((now / 90U + (h >> 8)) % 14U < 2U) gfx_tint_plot(px, (int)(h % 160U), HORIZON + 1 + (int)((h >> 16) % 9U), 0xFFFFFF, 230);
    }
}
static void draw_reeds(uint16_t *px, unsigned now) /* by your feet, in the bottom left corner */
{
    for (int i = 0; i < 8; ++i) {
        const uint32_t h = gfx_hash((uint32_t)i * 31U + 7U);
        const int x = 1 + i * 3 + (int)(h % 3U), height = 14 + (int)((h >> 8) % 16U);
        int top_x = x;
        for (int y = 66; y > 66 - height; --y) {
            const int rise = 66 - y, sway = sinp(now, 14, (unsigned)(i * 40 + rise * 5)) * (rise / 5) / 256;
            top_x = x + sway;
            gfx_plot(px, top_x, y, gfx_rgb565(rise % 5 < 3 ? 0x2F8A3E : 0x1F6A30));
            if (i % 2 == 0) gfx_plot(px, top_x + 1, y, gfx_rgb565(0x164E26));
        }
        if (i % 3 == 1) gfx_rect(px, top_x - 1, 66 - height - 4, 3, 5, gfx_rgb565(0x6A4224)); /* a cattail */
    }
}

/* ---- the float, the rod and the line --------------------------------------------------------------------------- */
/* `y` is the waterline (anything at or below `water_y` is under water and only shows through it); `sink` pushes the
 * float down by that many rows, and size 0..2 is far..near. Returns the row of the top of the antenna. */
static int draw_float(uint16_t *px, int x, int y, int size, int sink, int water_y)
{
    static const uint8_t low[3] = {1, 2, 3}, mid[3] = {1, 2, 2}, high[3] = {1, 2, 3}, antenna[3] = {2, 3, 5}, width[3] = {2, 3, 5};
    const int bands = low[size] + mid[size] + high[size], total = bands + antenna[size];
    int row = y + sink - 1;
    for (int r = 0; r < total; ++r, --row) {
        uint32_t c;
        int w = width[size];
        if (r < low[size]) c = 0xE0302A;
        else if (r < low[size] + mid[size]) c = 0xF6F6F6;
        else if (r < bands) c = 0xE0302A;
        else { c = r == total - 1 ? 0xFFB23A : 0x2A2A2A; w = 1; }
        const int left = x - w / 2;
        if (row >= water_y) {
            for (int i = 0; i < w; ++i) gfx_tint_plot(px, left + i, row, c, 70);
        } else {
            gfx_rect(px, left, row, w, 1, gfx_rgb565(c));
            if (w > 1) gfx_plot(px, left, row, gfx_rgb565(gfx_mix(c, 0xFFFFFF, 90))); /* lit side */
        }
    }
    return y + sink - total;
}
static void ring_ellipse(uint16_t *px, int x, int y, int r, unsigned alpha)
{
    const unsigned steps = r < 8 ? 32U : 64U, stride = 256U / steps;
    for (unsigned i = 0; i < steps; ++i) {
        const int dx = r * gfx_sin256(i * stride + 64U) / 256, dy = r * gfx_sin256(i * stride) / 256 / 3;
        gfx_tint_plot(px, x + dx, y + dy, 0xFFFFFF, alpha);
        gfx_tint_plot(px, x + dx, y + dy + 1, 0x06355A, alpha / 3U); /* the lee side of the ring */
    }
}
/* Rings of light spreading from where the float sits; `age` is the time since whatever stirred the water. */
static void ripples(uint16_t *px, int x, int y, unsigned age, int size)
{
    if (age >= 900U) return;
    for (unsigned k = 0; k < 3U; ++k) {
        if (age < k * 150U) continue;
        const unsigned t = age - k * 150U, a = t < 700U ? 200U - t * 200U / 700U : 0U;
        if (a) ring_ellipse(px, x, y, 2 + size * 2 + (int)(t / 36U), a);
    }
}
static void sag_line(uint16_t *px, int x0, int y0, int x1, int y1, int sag, uint16_t c)
{
    int lx = x0, ly = y0;
    for (int i = 1; i <= 12; ++i) {
        const int x = app_lerp(x0, x1, i, 12), y = app_lerp(y0, y1, i, 12) + sag * 4 * i * (12 - i) / 144;
        gfx_line(px, lx, ly, x, y, c);
        lx = x;
        ly = y;
    }
}
/* The rod comes in from the bottom right; the tip drops under a pulling fish. Returns the tip. */
static void rod_tip(int bend, int *x, int *y) { *x = TIP_X - bend / 2; *y = TIP_Y + bend; }
static void draw_rod(uint16_t *px, int bend, int tip_x, int tip_y)
{
    const int mid_x = (GRIP_X + tip_x) / 2, mid_y = (GRIP_Y + tip_y) / 2 + bend / 3;
    const int cork_x = app_lerp(GRIP_X, mid_x, 2, 5), cork_y = app_lerp(GRIP_Y, mid_y, 2, 5);
    gfx_line2(px, GRIP_X, GRIP_Y, mid_x, mid_y, gfx_rgb565(0x2A2018)); /* the blank, thick below and thin above */
    gfx_line(px, GRIP_X - 1, GRIP_Y, mid_x - 1, mid_y - 1, gfx_rgb565(0x6A5A48));
    gfx_line(px, mid_x, mid_y, tip_x, tip_y, gfx_rgb565(0x3A2C20));
    gfx_line(px, mid_x - 1, mid_y - 1, tip_x - 1, tip_y - 1, gfx_rgb565(0x5E4C38));
    gfx_line2(px, GRIP_X, GRIP_Y, cork_x, cork_y, gfx_rgb565(0xB0864F)); /* the cork grip */
    gfx_line(px, GRIP_X + 2, GRIP_Y, cork_x + 2, cork_y, gfx_rgb565(0x8A6A3A));
    gfx_line(px, GRIP_X - 1, GRIP_Y - 1, cork_x - 1, cork_y - 1, gfx_rgb565(0xD6B070));
    for (int i = 1; i <= 3; ++i) gfx_plot(px, app_lerp(mid_x, tip_x, i, 4) - 1, app_lerp(mid_y, tip_y, i, 4) - 2, gfx_rgb565(0xC8CED6)); /* guides */
    gfx_disc(px, cork_x - 5, cork_y + 3, 3, gfx_rgb565(0xC8CED6)); /* the reel */
    gfx_plot(px, cork_x - 5, cork_y + 3, gfx_rgb565(0x4A5058));
    gfx_line(px, cork_x - 7, cork_y + 5, cork_x - 9, cork_y + 7, gfx_rgb565(0x4A5058)); /* the crank */
    gfx_disc(px, app_lerp(GRIP_X, cork_x, 1, 2) + 1, app_lerp(GRIP_Y, cork_y, 1, 2) - 1, 3, gfx_rgb565(0xF2C49C)); /* the hand */
    gfx_rect(px, 148, 61, 12, 4, gfx_rgb565(0xE9A21E)); /* the sleeve */
    gfx_hline(px, 148, 159, 61, gfx_rgb565(0xFFC847));
}
static void heart(uint16_t *px, int x, int y, bool full)
{
    static const char *rows[4] = {".X.X.", "XXXXX", ".XXX.", "..X.."};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 5; ++c)
            if (rows[r][c] == 'X') gfx_plot(px, x + c, y + r, gfx_rgb565(full ? (r == 0 && c == 1 ? 0xFFB0C4 : 0xFF4A7A) : 0x5A4660));
}

/* ---- the prompt ring, centred on the float ---------------------------------------------------------------------- */
static void draw_prompt(uint16_t *px, const fs_t *f, unsigned now, int cx, int cy)
{
    if (f->p.state != FP_APPROACH && f->p.state != FP_HELD) return;
    gfx_tint_ellipse(px, cx, cy, 22, 18, 0x021830, 46);
    const bool hold = f->p.kind == FP_HOLD, held = f->p.state == FP_HELD;
    const int to = fs_to_target(f), slack = (int)f->p.win_ms + (held ? 40 : 0);
    const bool inside = app_abs(to) <= slack, perfect = inside && app_abs(to) * 100 <= slack * 40;
    const uint32_t lit = perfect ? 0x7CFFA8 : inside ? 0x4CE07C : 0xFFD84A;
    gfx_ring(px, cx, cy, 8, 0xFFFFFF, 230); /* the target circle */
    gfx_ring(px, cx, cy, 7, 0xFFFFFF, 120);
    if (inside) gfx_tint_ellipse(px, cx, cy, 7, 7, lit, 90);
    if (!held) {
        const int r = 8 + fs_ring(f) * 14 / 256; /* closes in on the target */
        gfx_ring(px, cx, cy, r, lit, 255);
        gfx_ring(px, cx, cy, r + 1, lit, 110);
    }
    if (!hold) return;
    /* press and hold: the bar fills from the beat to the moment to let go (the green end) */
    const int w = 40, x0 = app_clamp(cx - w / 2, 1, APP_CANVAS_W - w - 2), y = cy + 15;
    const int zone = (int)(f->p.win_ms + 40U) * 2 * w / (int)(f->p.hold_ms + f->p.win_ms * 2U + 80U);
    gfx_rect(px, x0 - 1, y - 1, w + 2, 6, gfx_rgb565(0x0A1A2C));
    gfx_rect(px, x0, y, w, 4, gfx_rgb565(0x2A4058));
    gfx_rect(px, x0 + w - zone, y, zone, 4, gfx_rgb565(0x2E8C54));
    const int fill = held ? fs_hold_fill(f) * (w - zone / 2) / 256 : 0;
    gfx_rect(px, x0, y, fill, 4, gfx_rgb565(held && inside ? 0x7CFFA8 : 0xFFD84A));
    gfx_rect(px, x0 + w - zone / 2, y - 1, 1, 6, 0xFFFF);
    if (held) gfx_ring(px, cx, cy, 4 + (int)((now / 80U) & 1U), 0x7CFFA8, 200);
}

/* ---- the scene by phase ------------------------------------------------------------------------------------------- */
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
/* The wake of a float that is being dragged: rings left behind where it has been. */
static void wake(uint16_t *px, state_t *s, int x, int y, unsigned now, bool moving)
{
    if (moving && (s->trail_ms[0] == 0U || now - s->trail_ms[0] >= 140U || app_abs(x - s->trail_x[0]) + app_abs(y - s->trail_y[0]) >= 4)) {
        for (int i = TRAIL - 1; i > 0; --i) { s->trail_x[i] = s->trail_x[i - 1]; s->trail_y[i] = s->trail_y[i - 1]; s->trail_ms[i] = s->trail_ms[i - 1]; }
        s->trail_x[0] = (int16_t)x; s->trail_y[0] = (int16_t)y; s->trail_ms[0] = now;
    }
    for (int i = 0; i < TRAIL; ++i)
        if (s->trail_ms[i] && now - s->trail_ms[i] < 700U) {
            const unsigned age = now - s->trail_ms[i];
            ring_ellipse(px, s->trail_x[i], s->trail_y[i], 2 + (int)(age / 50U), 150U - age * 150U / 700U);
        }
}
static void draw_scene(uint16_t *px, state_t *s, unsigned now)
{
    const fs_t *f = &s->core;
    if (f->phase == FS_RESULT) { draw_card(px, f, now); return; }
    draw_scenery(px, now);
    draw_reeds(px, now);
    const bool fighting = f->phase == FS_FIGHT, pulling = fighting && (f->p.state == FP_APPROACH || f->p.state == FP_HELD);
    int bend = 0;
    if (fighting) bend = 2 + (pulling ? 3 + sinp(now, 12, 0) / 128 : 0) + (int)f->combo / 4;
    else if (f->phase == FS_BITE) bend = 3;
    else if (f->phase == FS_CAST) bend = f->phase_ms < FS_CAST_MS / 3U ? -4 : 1;
    int tip_x, tip_y;
    rod_tip(bend, &tip_x, &tip_y);
    const spot_t rest = spot_of(REST_X, REST_Y);
    const uint16_t line_c = gfx_rgb565(0xEEF6FA);
    spot_t at = rest;
    switch (f->phase) {
    case FS_IDLE: { /* the float hangs from the tip and sways */
        const int x = tip_x + 1 + sinp(now, 38, 0) * 3 / 256, y = tip_y + 21 + sinp(now, 50, 40) / 140;
        const int top = draw_float(px, x, y, 1, 0, 999);
        gfx_line(px, tip_x, tip_y, x, top, line_c);
        at = (spot_t){x, y, 1};
        break;
    }
    case FS_LOST: { /* the line has parted: the float is left where it was, the loose end hangs from the tip */
        at = spot_of(fs_fish_x(f), fs_fish_y(f));
        draw_float(px, at.x, at.y, at.size, 0, at.y);
        ripples(px, at.x, at.y, f->phase_ms, at.size);
        sag_line(px, tip_x, tip_y, tip_x - 5, tip_y + 20, 3, line_c);
        if (f->phase_ms < 160U) gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0xFF3020, 70U - f->phase_ms / 3U);
        break;
    }
    case FS_CAST: {
        const int t = (int)f->phase_ms, n = (int)FS_CAST_MS;
        const int x = app_lerp(tip_x, rest.x, t, n), y = app_lerp(tip_y + 6, rest.y, t, n) - 20 * 4 * t * (n - t) / (n * n);
        const int top = draw_float(px, x, y, 1, 0, 999);
        sag_line(px, tip_x, tip_y, x, top, 2, line_c);
        at = (spot_t){x, y, 1};
        break;
    }
    case FS_WAIT:
    case FS_BITE: {
        const unsigned nib = s->nibble_ms ? (unsigned)(now - s->nibble_ms) : 9999U;
        int sink = 0, bob = sinp(now, 18, 0) / 200;
        if (f->phase == FS_WAIT) { if (nib < 320U) sink = (int)(nib < 160U ? nib : 320U - nib) / 40; }
        else { sink = 2 + (int)(f->phase_ms < 120U ? f->phase_ms / 40U : 3U); bob = (int)((now / 70U) & 1U); } /* it goes under and shivers */
        ripples(px, rest.x, rest.y, f->phase == FS_WAIT ? f->phase_ms : f->phase_ms % 520U, rest.size); /* the splash of the cast / the frenzy of a bite */
        if (f->phase == FS_WAIT) ripples(px, rest.x, rest.y, nib, rest.size);
        const int top = draw_float(px, rest.x, rest.y + bob, rest.size, sink, rest.y);
        if (f->phase == FS_BITE) gfx_line(px, tip_x, tip_y, rest.x, rest.y - 2, line_c);
        else sag_line(px, tip_x, tip_y, rest.x, top, 3, line_c);
        if (f->phase == FS_BITE && ((now / 90U) & 1U)) { /* a "!" over the float */
            gfx_disc(px, rest.x, rest.y - 19, 5, gfx_rgb565(0xE0302A));
            gfx_rect(px, rest.x - 1, rest.y - 22, 2, 5, 0xFFFF);
            gfx_rect(px, rest.x - 1, rest.y - 16, 2, 1, 0xFFFF);
        }
        break;
    }
    case FS_FIGHT: {
        at = spot_of(fs_fish_x(f), fs_fish_y(f));
        const int jerk = pulling ? sinp(now, 4, 0) / 170 : 0, sink = pulling ? 2 + (sinp(now, 6, 20) + 256) / 110 : 0;
        const int bob = pulling ? 0 : sinp(now, 22, 0) / 200;
        wake(px, s, at.x, at.y, now, true);
        ripples(px, at.x + jerk, at.y, pulling ? (now / 3U) % 420U : 9999U, at.size);
        const int top = draw_float(px, at.x + jerk, at.y + bob, at.size, sink, at.y);
        if (pulling) gfx_line(px, tip_x, tip_y, at.x + jerk, top, line_c); /* taut */
        else sag_line(px, tip_x, tip_y, at.x, top, 2, line_c);
        break;
    }
    case FS_LAND: { /* the float skims in, then the fish clears the water and swings up to the rod */
        const int t = (int)f->phase_ms, n = (int)FS_LAND_MS, split = n * 55 / 100;
        const spot_t from = spot_of(fs_fish_x(f), fs_fish_y(f));
        const int end_x = tip_x - 8, end_y = NEAR_Y + 10;
        if (t < split) {
            const int x = app_lerp(from.x, end_x, t, split), y = app_lerp(from.y, end_y, t, split);
            wake(px, s, x, y, now, true);
            const int top = draw_float(px, x, y, t * 3 > split ? 2 : from.size, 0, y);
            gfx_line(px, tip_x, tip_y, x, top, line_c);
            at = (spot_t){x, y, 2};
        } else {
            const int u = t - split, m = n - split;
            const int len = fs_tier(f->tier)->len, x = app_lerp(end_x, tip_x - 3, u, m), y = app_lerp(end_y, tip_y + 14, u, m) - 24 * 4 * u * (m - u) / (m * m);
            ripples(px, end_x, end_y, (unsigned)u, 2);
            fish_draw(px, x, y, len, -1, gfx_sin256(now / 4U) / 40, style_of(f->tier));
            gfx_line(px, tip_x, tip_y, x - len / 2, y, line_c);
            at = (spot_t){x, y, 2};
        }
        break;
    }
    default: break;
    }
    draw_rod(px, bend, tip_x, tip_y);
    s->mark_x = (int16_t)at.x; s->mark_y = (int16_t)at.y;
    if (fighting) {
        draw_prompt(px, f, now, app_clamp(at.x, 22, APP_CANVAS_W - 23), app_clamp(at.y - 4, 24, 41));
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
    s->plot = app_ui_canvas(root, 0, 0, APP_CANVAS_W, APP_CANVAS_H);
    s->band = app_ui_band(root, 0x0B1E34, 0x2E6E96);
    s->msg = app_ui_label(s->band, &aihook_font_12, 3, 2, 126, 14, LV_LABEL_LONG_MODE_CLIP);
    s->score = app_ui_label(s->band, &aihook_font_12, 128, 2, 32, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->msg, app_ui_color(0xDDEBF7), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->score, app_ui_color(0xFFD84A), LV_PART_MAIN);
    s->hold = app_ui_hold_bar(root, BAND_Y, APP_COLOR_ACCENT); /* the hold towards the exit */
    app_ui_hide(s->hold, true);
    s->tag = app_ui_rect(root, 130, 13, 29, 14, 0x2E8B57, 5);
    s->tag_text = app_ui_label(s->tag, &aihook_font_12, 0, 1, 29, 12, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->tag_text);
    app_ui_text(s->tag_text, "舵机");
    s->name = app_ui_label(root, &aihook_font_16, 6, 4, 80, 20, LV_LABEL_LONG_MODE_CLIP);
    s->weight = app_ui_label(root, &aihook_font_12, 6, 24, 80, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->name, app_ui_color(0xFFE08A), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->weight, app_ui_color(0xDDEBF7), LV_PART_MAIN);
    s->toast = app_ui_label(root, &aihook_font_16, 0, 0, 48, 20, LV_LABEL_LONG_MODE_CLIP);
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
    app_critical_enter();
    s_status.active = active; s_status.phase = f->phase; s_status.tier = f->tier; s_status.done = f->done;
    s_status.need = f->need; s_status.lives = f->lives; s_status.kind = f->p.kind; s_status.state = f->p.state;
    s_status.servo = (uint8_t)hs; s_status.caught = f->caught; s_status.lost = f->lost; s_status.casts = f->casts;
    s_status.to = to == INT32_MAX ? 99999 : to;
    if (frame) ++s_status.frames;
    app_critical_exit();
}
static void drain_shakes(state_t *s, uint64_t now)
{
    app_servo_t sv;
    app_servo_read(&sv);
    int amp;
    unsigned half, dur;
    const bool pull_right = s->core.phase == FS_FIGHT && s->core.gy > s->core.fy;
    while (fs_take_shake(&s->core, &amp, &half, &dur)) {
        /* Short strokes reverse quickly within the calibrated speed. The first tug follows the fish. */
        if (sv.available && sv.armed)
            haptic_shake_direction(&s->haptic, now, app_clamp(amp, 1, 4), half, dur, sv.speed_dps, pull_right);
    }
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    fs_init(&s->core, app_random() ^ (uint32_t)ctx->now_ms, (uint32_t)ctx->now_ms);
    haptic_init(&s->haptic);
    s->last_ms = (uint32_t)ctx->now_ms;
    app_set_triple_exit(ctx, false); /* a float is struck by pressing at the right moment: three quick presses are never an exit */
    app_critical_enter(); s_status = (status_t){0}; app_critical_exit();
    publish(s, true, false);
}
/* The way out is a two second hold while nothing is on the line, and only for a press that began there: a hold that
 * started with a bite or a pull belongs to the fight. */
static void sync_exit(app_context_t *ctx, const state_t *s)
{
    const uint8_t p = s->core.phase;
    const bool calm = p == FS_IDLE || p == FS_CAST || p == FS_WAIT || p == FS_RESULT || p == FS_LOST;
    app_set_exit_hold(ctx, calm ? EXIT_HOLD_MS : 0U);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    fs_update(&s->core, (uint32_t)ctx->now_ms);
    sync_exit(ctx, s);
    if (s->core.nibbles != s->seen_nibbles) { /* the float twitches for each nibble */
        if (s->core.nibbles > s->seen_nibbles) s->nibble_ms = (uint32_t)ctx->now_ms;
        s->seen_nibbles = s->core.nibbles;
    }
    const int middle = (FS_FISH_Y_MIN + FS_FISH_Y_MAX) * 8;
    const int span = FS_FISH_Y_MAX * 16 - middle;
    /* Y is sideways on the water. Lead the eased float a little to cover mechanical travel time. */
    const int lateral = (s->core.fy + s->core.gy) / 2;
    const int fish_angle_x10 = app_clamp((middle - lateral) * 60 / span, -60, 60);
    haptic_base(&s->haptic, s->core.phase == FS_FIGHT ? fish_angle_x10 : 0);
    drain_shakes(s, ctx->now_ms);
    haptic_pump(ctx, &s->haptic);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    haptic_stop(&s->haptic); /* the framework disarms the servo it leased to this app */
    if (s->plot) app_ui_canvas_refresh(s->plot); /* The shared canvas goes to the next app. */
    app_critical_enter(); s_status.active = false; app_critical_exit();
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    const uint32_t now = (uint32_t)e->ms;
    if (e->kind == APP_EVENT_DOWN) {
        s->down_in_fight = s->core.phase == FS_BITE || s->core.phase == FS_FIGHT || s->core.phase == FS_LAND;
        fs_press(&s->core, now);
    }
    else if (e->kind == APP_EVENT_UP) fs_release(&s->core, now);
    else if (e->kind == APP_EVENT_CONFIRM && s->core.phase != FS_FIGHT && s->core.phase != FS_BITE)
        (void)haptic_arm(ctx, &s->haptic); /* manual enable from a quiet screen */
    drain_shakes(s, e->ms);
    sync_exit(ctx, s);
    publish(s, true, false);
}
static void sync_hud(state_t *s, uint64_t now, unsigned hold_ms)
{
    const fs_t *f = &s->core;
    const haptic_state_t hs = haptic_state();
    const char *msg = "";
    switch (f->phase) {
    case FS_IDLE: msg = hs == HAPTIC_READY ? "按键抛竿 长按启用舵机" : "按键抛竿"; break;
    case FS_CAST: msg = "抛竿…"; break;
    case FS_WAIT: msg = "盯住鱼漂 等它下沉"; break;
    case FS_BITE: msg = "鱼漂下沉! 快按!"; break;
    case FS_FIGHT:
        if (f->p.state == FP_HELD) msg = "按住… 到绿区松手";
        else if (f->p.state == FP_APPROACH) msg = f->p.kind == FP_HOLD ? "按住 到条尾松手" : "圈到点 点按!";
        else msg = "鱼漂乱窜…";
        break;
    case FS_LAND: msg = "收线!"; break;
    case FS_RESULT: msg = "按键再钓一次"; break;
    default: msg = "鱼跑了… 断线了"; break;
    }
    const bool calm = f->phase == FS_IDLE || f->phase == FS_WAIT || f->phase == FS_RESULT; /* the hint of the way out alternates */
    const bool exit_slot = calm && (now / HINT_SLOT_MS) % 2U == 1U;
    if (exit_slot) msg = "按住2秒退出";
    const uint32_t key = (uint32_t)f->phase << 16 | (uint32_t)f->p.state << 8 | f->p.kind << 4 | (uint32_t)hs | (exit_slot ? 0x80U : 0U);
    if (key != s->shown_key) { app_ui_text(s->msg, msg); s->shown_key = key; }
    if (s->shown_caught != f->caught) {
        char text[16];
        snprintf(text, sizeof text, "钓%lu", (unsigned long)f->caught);
        app_ui_text(s->score, text);
        s->shown_caught = f->caught;
    }
    app_ui_hold_line(s->hold, hold_ms, hs == HAPTIC_READY && (f->phase == FS_IDLE || f->phase == FS_RESULT), EXIT_HOLD_MS);
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
        lv_obj_set_pos(s->toast, app_clamp(s->mark_x - 24, 0, APP_WIDTH - 48), app_clamp(s->mark_y - 34, 13, 40));
        static const char *const words[] = {"", "完美!", "好!", "太早", "太晚"};
        app_ui_text(s->toast, words[f->last_result]);
        lv_obj_set_style_text_color(s->toast, app_ui_color(f->last_result <= FR_GOOD ? (f->last_result == FR_PERFECT ? 0x7CFFA8 : 0xFFE08A) : 0xFF6A5A), LV_PART_MAIN);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    draw_scene(app_ui_pixels(), s, (unsigned)ctx->now_ms);
    sync_hud(s, ctx->now_ms, app_exit_hold_progress(ctx));
    app_ui_canvas_refresh(s->plot);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot fishing status")) return false;
    status_t v;
    app_shared_copy(v, s_status);
    (void)app_command_replyf("active=%u phase=%u tier=%u done=%u need=%u lives=%u kind=%u state=%u to=%d caught=%lu lost=%lu casts=%lu servo=%u frames=%lu",
            v.active ? 1U : 0U, v.phase, v.tier, v.done, v.need, v.lives, v.kind, v.state, v.to, (unsigned long)v.caught,
            (unsigned long)v.lost, (unsigned long)v.casts, v.servo, (unsigned long)v.frames);
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
    .flags = APP_SERVO_DEFAULT_HAPTIC,
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command, .menu_group = APP_MENU_GAMES};
