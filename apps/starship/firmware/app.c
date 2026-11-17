#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_haptic.h"
#include "esphook/app_services.h"
#include "esphook/app_steer.h"
#include "esphook/app_ui.h"
#include "freertos/FreeRTOS.h"
#include "starship_core.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 星际战机: a first-person cockpit view on the shared 160x64 canvas, the bottom 16 px a message strip. The radar (or
 * the K2/K3 keys) slides the ship left and right, the cannon fires while the main key is held, and an enemy bolt or a
 * collision costs one of three shield points and gives the servo a short shake when it has been enabled (hold a key for
 * 0.5 s on the title or game over screen; it is never enabled on its own). Game rules live in starship_core.c. */
#define BAND_Y 64
#define CX 80
#define HY 25     /* the horizon: where everything at eye level converges */
#define FOC 90    /* focal length in world units per pixel at z = 1 */
#define DASH_Y 52
#define EXIT_HOLD_MS 2000U
#define HINT_SLOT_MS 2600U

typedef struct { int cam, ox, oy; } view_t;
typedef struct {
    ss_t core;
    steer_t steer;
    haptic_t haptic;
    lv_obj_t *plot, *band, *line, *hold, *msg, *score, *tag, *tag_text, *title, *sub;
    lv_image_dsc_t image;
    uint64_t last_ms;
    int prev_cam;
    uint32_t shown_key, shown_score, shown_best;
    uint8_t down_in_play;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "starship state too large");
_Static_assert(offsetof(state_t, core) == 0, "tests pose the scene through the core at the start of the state");
typedef struct {
    bool active;
    uint8_t phase, hp, src, servo, objs, foes;
    uint32_t score, best, kills, shots, frames;
    int steer, cam;
} status_t;
static status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static int absi(int v) { return v < 0 ? -v : v; }
static int vx(const view_t *v, int wx, int z) { return CX + v->ox + (wx - v->cam) * FOC / (z < 40 ? 40 : z); }
static int vy(const view_t *v, int wy, int z) { return HY + v->oy - wy * FOC / (z < 40 ? 40 : z); }
static uint16_t c565(uint32_t rgb) { return gfx_rgb565(rgb); }

/* ---- backdrop ------------------------------------------------------------------------------------------------ */
static void draw_planet(uint16_t *px, int cx, int cy, int r)
{
    static const uint32_t bands[] = {0xD8A06A, 0xB8774A, 0xE8C896, 0xA06440, 0xDDB17A, 0xC08455};
    for (int a = 0; a < 256; a += 3) { /* the ring, behind the globe */
        const int x = cx + gfx_sin256((unsigned)a + 64U) * (r * 2) / 256, y = cy + gfx_sin256((unsigned)a) * (r / 3) / 256 + 2;
        if (gfx_sin256((unsigned)a) < 0) gfx_plot(px, x, y, c565(0x8E7A5A));
    }
    for (int dy = -r; dy <= r; ++dy) {
        const int half = (int)gfx_isqrt((uint32_t)(r * r - dy * dy));
        const uint16_t c = c565(bands[(unsigned)(dy + r + 8) / 3U % 6U]);
        gfx_hline(px, cx - half, cx + half, cy + dy, c);
        gfx_tint(px, cx + half / 3, cy + dy, half - half / 3 + 1, 1, 0x02030C, 150); /* night side */
        gfx_tint_plot(px, cx - half, cy + dy, 0xFFE6B8, 120);                        /* sunlit edge */
    }
    for (int a = 0; a < 256; a += 2) { /* the ring, across the front of the globe */
        const int x = cx + gfx_sin256((unsigned)a + 64U) * (r * 2) / 256, y = cy + gfx_sin256((unsigned)a) * (r / 3) / 256 + 2;
        if (gfx_sin256((unsigned)a) >= 0) gfx_plot(px, x, y, c565(0xE6D2A0));
    }
}
static void draw_space(uint16_t *px, const view_t *v, unsigned now, int cam_prev, unsigned speed)
{
    gfx_gradient(px, 0, APP_CANVAS_H, 0x01020A, 0x0B1238);
    const int n = -v->cam / 28;
    gfx_tint_ellipse(px, 36 + n, 22, 44, 12, 0x7A2AA8, 26);
    gfx_tint_ellipse(px, 50 + n, 26, 26, 7, 0xE0509A, 22);
    gfx_tint_ellipse(px, 130 + n * 2, 40, 38, 9, 0x1C8AC8, 24);
    draw_planet(px, 104 - v->cam / 55, 17, 11);
    for (unsigned i = 0; i < 84U; ++i) { /* stars streaming past, drawn as streaks of where they were a moment ago */
        const uint32_t h = gfx_hash(i * 7919U + 11U);
        const int wx = (int)(h % 2800U) - 1400, wy = (int)((h >> 12) % 1500U) - 750;
        const int z = 1800 - (int)((now * speed / 2U + (h >> 8)) % 1700U);
        const int x1 = vx(v, wx, z), y1 = vy(v, wy, z);
        if (x1 < 0 || x1 >= APP_CANVAS_W || y1 < 0 || y1 >= DASH_Y) continue;
        const int z0 = z + (int)(20U * speed);
        const int x0 = CX + v->ox + (wx - cam_prev) * FOC / z0, y0 = vy(v, wy, z0);
        const unsigned near = (unsigned)(1800 - z) * 256U / 1700U;
        gfx_line(px, x0, y0, x1, y1, c565(gfx_mix(0x2A3668, 0xA8B8E8, near)));
        gfx_plot(px, x1, y1, c565(gfx_mix(0x7888C8, 0xFFFFFF, near)));
    }
}

/* ---- things in the field ------------------------------------------------------------------------------------- */
static void draw_rock(uint16_t *px, int x, int y, int rad, const ss_obj_t *o)
{
    static const uint32_t tones[4] = {0x7A6C5C, 0x626C7C, 0x8A604A, 0x6C7064};
    const uint32_t base = tones[o->seed & 3U];
    if (rad < 2) { gfx_rect(px, x, y, 2, 2, c565(gfx_mix(base, 0xFFFFFF, 60))); return; }
    if (rad > 70) rad = 70;
    gfx_disc(px, x, y, rad, c565(gfx_mix(base, 0x000000, 150)));
    gfx_disc(px, x, y, rad - 1, c565(base));
    if (rad >= 3) gfx_disc(px, x - rad / 4, y - rad / 4, rad * 2 / 3, c565(gfx_mix(base, 0xFFFFFF, 55)));
    if (rad >= 4) gfx_tint_ellipse(px, x + rad / 4, y + rad / 4, rad * 13 / 20, rad * 13 / 20, 0x00020C, 96);
    if (rad >= 5) {
        for (unsigned k = 0; k < 3U; ++k) { /* craters on the visible half of a slowly turning rock */
            const unsigned phase = (unsigned)o->seed * 41U + k * 85U + (unsigned)o->age * 2U;
            const int front = gfx_sin256(phase + 64U);
            if (front < 40) continue;
            const int cx = x + gfx_sin256(phase) * rad * 6 / 10 / 256, cy = y + (((int)(o->seed >> k) & 7) - 3) * rad / 9;
            gfx_ellipse(px, cx, cy, 1 + rad * front / 256 / 4, 1 + rad / 5, c565(gfx_mix(base, 0x000000, 170)));
            gfx_plot(px, cx, cy + rad / 5 + 1, c565(gfx_mix(base, 0xFFFFFF, 90)));
        }
    }
    if (o->flash) gfx_tint_ellipse(px, x, y, rad, rad, 0xFFFFFF, 40U + (unsigned)o->flash * 24U);
}
static void draw_enemy(uint16_t *px, int x, int y, int u, const ss_obj_t *o, unsigned now)
{
    if (u < 3) {
        gfx_rect(px, x - u, y - 1, u * 2 + 1, 2, c565(0x9AA4B8));
        gfx_plot(px, x, y, c565(0xFF3A3A));
        return;
    }
    if (u > 70) u = 70;
    const bool charge = o->state == SE_HOLD && o->fire < 14U;
    const uint16_t steel = c565(0x5E6A84), shade = c565(0x333C52), light = c565(0x9AA8C6);
    for (int side = -1; side <= 1; side += 2) {
        const int bx = x + side * u * 3 / 10, tx = x + side * u;
        gfx_tri(px, bx, y - u / 8, tx, y - u * 45 / 100, tx, y - u / 8, steel);   /* upper blade */
        gfx_tri(px, bx, y + u / 8, tx, y + u * 3 / 10, tx, y + u / 10, shade);    /* lower blade */
        gfx_line(px, bx, y - u / 8, tx, y - u * 45 / 100, light);
        gfx_line(px, bx, y + u / 8, tx, y + u * 3 / 10, c565(0x1C2234));
        gfx_vline(px, tx, y - u * 45 / 100, y + u * 3 / 10, c565(0x1C2234));
        const int cy = y - u * 3 / 10, cr = u >= 9 ? 2 : 1; /* the cannon at the wing tip */
        gfx_disc(px, tx, cy, cr, charge ? c565(0xFFF0A0) : c565(0xC8322A));
        if (charge) gfx_tint_ellipse(px, tx, cy, cr * 3, cr * 3, 0xFFC040, 120);
    }
    gfx_ellipse(px, x, y, u * 36 / 100, u * 3 / 10, steel);
    gfx_tint_ellipse(px, x - u / 10, y - u / 10, u * 24 / 100, u / 6, 0xFFFFFF, 50);
    gfx_tint_ellipse(px, x + u / 8, y + u / 8, u * 28 / 100, u * 20 / 100, 0x000010, 90);
    gfx_peak(px, x, y - u * 5 / 10, u / 3 + 1, u * 2 / 10 + 1, shade);        /* dorsal fin */
    const unsigned pulse = 150U + (unsigned)(gfx_sin256(now / 6U + o->seed) + 256) / 6U;
    gfx_tint_ellipse(px, x, y, u / 5 + 1, u / 6 + 1, 0xFF2A2A, pulse);       /* the eye */
    gfx_plot(px, x, y, c565(0xFFD0C0));
    if (o->flash) gfx_tint_ellipse(px, x, y, u, u * 6 / 10, 0xFFFFFF, 40U + (unsigned)o->flash * 22U);
}
static void draw_bolt(uint16_t *px, int x, int y, int z, unsigned now)
{
    const int rad = 34 * FOC / z, r = rad < 1 ? 1 : rad > 9 ? 9 : rad;
    gfx_tint_ellipse(px, x, y, r * 3 / 2 + 1, r * 3 / 2 + 1, 0xFF4A1A, 90);
    gfx_disc(px, x, y, r, c565(0xFF8A2A));
    gfx_disc(px, x, y, r > 1 ? r - 1 : 0, c565(((now / 60U) & 1U) ? 0xFFD060 : 0xFFB040));
    gfx_plot(px, x, y, c565(0xFFFFFF));
}
static void draw_shot(uint16_t *px, const view_t *v, const ss_shot_t *b)
{
    const int tz = b->z - 70 < SS_ZN - 40 ? SS_ZN - 40 : b->z - 70;
    const int x1 = vx(v, b->x, b->z), y1 = vy(v, -45, b->z), x0 = vx(v, b->x, tz), y0 = vy(v, -45, tz);
    gfx_tint_ellipse(px, x1, y1, 3, 2, 0x40F0FF, 100);
    gfx_line2(px, x0, y0, x1, y1, c565(0x40D8F0));
    gfx_line(px, x0, y0, x1, y1, c565(0xBFFAFF));
    gfx_plot(px, x1, y1, c565(0xFFFFFF));
}
static void draw_explosion(uint16_t *px, int x, int y, int rad, unsigned age, unsigned span, unsigned seed)
{
    static const uint32_t layer[4] = {0xFF3A10, 0xFF7A20, 0xFFC040, 0xFFF4C8};
    const unsigned t = age * 256U / span, fade = t < 130U ? 256U : t > 250U ? 6U : 256U - (t - 130U) * 250U / 120U;
    const int r = 2 + rad * (int)(80U + t) / 256;
    for (int i = 0; i < 4; ++i) { /* a hot core inside cooler flames */
        const int ri = r * (4 - i) / 4 + (i == 3 ? 0 : 1);
        gfx_tint_ellipse(px, x, y, ri, ri, layer[i], (i == 0 ? 130U : 190U) * fade / 256U);
    }
    if (t < 110U) gfx_disc(px, x, y, r / 3, c565(0xFFFFFF));
    if (t > 90U) gfx_ring(px, x, y, r, 0xFF9A40, 200U * fade / 256U);
    for (unsigned k = 0; k < 9U; ++k) { /* sparks */
        const uint32_t h = gfx_hash(seed * 13U + k);
        const unsigned phase = (h & 255U);
        const int d = r * (int)(120U + (h >> 8) % 90U) / 160 + (int)t / 40;
        gfx_plot(px, x + gfx_sin256(phase + 64U) * d / 256, y + gfx_sin256(phase) * d / 256, c565(t < 150U ? 0xFFE880 : 0xFF8A30));
    }
}

/* ---- cockpit ------------------------------------------------------------------------------------------------- */
static void heart(uint16_t *px, int x, int y, bool full)
{
    static const char *rows[4] = {".X.X.", "XXXXX", ".XXX.", "..X.."};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 5; ++c)
            if (rows[r][c] == 'X') gfx_plot(px, x + c, y + r, c565(full ? (r == 0 && c == 1 ? 0xFFB0C4 : 0xFF4A7A) : 0x3A2A48));
}
static void draw_cockpit(uint16_t *px, const state_t *s, unsigned now)
{
    const ss_t *g = &s->core;
    gfx_rect(px, 0, 0, APP_CANVAS_W, 2, c565(0x10142A)); /* the frame of the canopy */
    gfx_hline(px, 0, APP_CANVAS_W - 1, 2, c565(0x2E3860));
    for (int side = 0; side < 2; ++side) {
        const int x = side ? APP_CANVAS_W - 1 : 0, dir = side ? -1 : 1;
        gfx_tri(px, x, 0, x + dir * 7, 0, x + dir * 18, DASH_Y, c565(0x0C1022));
        gfx_tri(px, x, 0, x + dir * 18, DASH_Y, x, DASH_Y, c565(0x0C1022));
        gfx_line(px, x + dir * 7, 0, x + dir * 18, DASH_Y, c565(0x3A4678));
        gfx_line(px, x + dir * 8, 0, x + dir * 19, DASH_Y, c565(0x1C2444));
    }
    gfx_ellipse(px, CX, 59, 74, 8, c565(0x46558A)); /* the dashboard, a lit edge over a dark body */
    gfx_ellipse(px, CX, 60, 73, 8, c565(0x121830));
    gfx_rect(px, 0, 60, APP_CANVAS_W, 4, c565(0x0C1022));
    for (int side = -1; side <= 1; side += 2) { /* the cannons, with a muzzle flash right after a shot */
        const int gx = CX + side * 50;
        gfx_rect(px, gx - 1, 48, 3, 6, c565(0x6A7AA8));
        gfx_vline(px, gx, 48, 53, c565(0xB4C4F0));
        if (g->phase == SS_PLAY && g->cooldown >= SS_FIRE_STEPS - 3U) {
            gfx_tint_ellipse(px, gx, 47, 4, 3, 0x40F0FF, 200);
            gfx_disc(px, gx, 47, 1, c565(0xFFFFFF));
        }
    }
    /* shield: three hearts, the newest loss blinks while the ship is untouchable */
    for (int i = 0; i < SS_HP; ++i) {
        const bool full = i < g->hp;
        if (g->invuln && full && i == g->hp - 1 && ((now / 90U) & 1U)) continue;
        heart(px, 12 + i * 7, 57, full);
    }
    /* scanner: where everything is relative to the ship, near things low */
    gfx_rect(px, CX - 16, 54, 33, 9, c565(0x2A3A68));
    gfx_rect(px, CX - 15, 55, 31, 7, c565(0x030A14));
    gfx_vline(px, CX, 55, 61, c565(0x12403A));
    gfx_hline(px, CX - 15, CX + 15, 58, c565(0x0C2A28));
    for (unsigned i = 0; i < SS_OBJ; ++i) {
        const ss_obj_t *o = &g->obj[i];
        if (!o->alive) continue;
        const int dx = (o->x - g->cam_x) * 14 / 1300, dy = 61 - o->z * 6 / SS_ZF;
        gfx_rect(px, CX + dx - 1, dy, 2, 1, c565(o->kind == SO_ENEMY ? 0xFF4A4A : 0x9AA4B8));
    }
    for (unsigned i = 0; i < SS_BOLT; ++i) {
        const ss_bolt_t *b = &g->bolt[i];
        if (b->alive) gfx_plot(px, CX + (b->x - g->cam_x) * 14 / 1300, 61 - b->z * 6 / SS_ZF, c565(0xFFB040));
    }
    gfx_rect(px, CX - 1, 61, 3, 1, c565(0x7CFFA8));
    /* input source lights: radar, keys, servo */
    const haptic_state_t hs = haptic_state();
    gfx_rect(px, 124, 58, 5, 3, c565(s->steer.source == STEER_RADAR ? 0x40E0FF : 0x16303A));
    gfx_rect(px, 131, 58, 5, 3, c565(s->steer.source == STEER_KEYS ? 0xFFC040 : 0x3A2C12));
    gfx_rect(px, 138, 58, 5, 3, c565(hs == HAPTIC_ARMED ? 0x5CFF8A : 0x143018));
}
static void draw_crosshair(uint16_t *px, const ss_t *g, unsigned now)
{
    bool locked = false;
    for (unsigned i = 0; i < SS_OBJ; ++i) locked |= g->obj[i].alive && absi(g->obj[i].x - g->cam_x) <= g->obj[i].r + 20;
    const uint16_t c = c565(locked ? 0xFF5A4A : 0x7CFFA8);
    gfx_hline(px, CX - 6, CX - 3, HY, c);
    gfx_hline(px, CX + 3, CX + 6, HY, c);
    gfx_vline(px, CX, HY - 6, HY - 3, c);
    gfx_vline(px, CX, HY + 3, HY + 6, c);
    gfx_plot(px, CX, HY, c);
    if (locked && ((now / 120U) & 1U)) gfx_ring(px, CX, HY, 8, 0xFF5A4A, 180);
}
static void draw_warnings(uint16_t *px, const view_t *v, const ss_t *g, unsigned now)
{
    if ((now / 100U) & 1U) return;
    for (unsigned i = 0; i < SS_BOLT; ++i) { /* a bolt that is about to hit: blinking arrow at the top */
        const ss_bolt_t *b = &g->bolt[i];
        if (!b->alive || b->z > 700 || absi(b->x - g->cam_x) > SS_BOLT_HALF + 140) continue;
        int x = vx(v, b->x, b->z);
        x = x < 6 ? 6 : x > APP_CANVAS_W - 7 ? APP_CANVAS_W - 7 : x;
        gfx_tri(px, x - 4, 4, x + 4, 4, x, 9, c565(0xFF3A2A));
    }
}

/* ---- the scene ----------------------------------------------------------------------------------------------- */
static void draw_scene(uint16_t *px, state_t *s, unsigned now)
{
    ss_t *g = &s->core;
    view_t v = {.cam = g->cam_x, .ox = 0, .oy = 0};
    if (g->phase == SS_PLAY && g->hit_age_ms < 320U) { /* the hull shakes when it is hit */
        const int a = 1 + (int)(320U - g->hit_age_ms) * 3 / 320;
        const uint32_t h = gfx_hash(now / 33U);
        v.ox = (int)(h % (uint32_t)(a * 2 + 1)) - a;
        v.oy = (int)((h >> 8) % (uint32_t)(a * 2 + 1)) - a;
    }
    draw_space(px, &v, now, s->prev_cam, g->phase == SS_PLAY || g->phase == SS_DYING ? 3U : 5U);
    uint8_t order[SS_OBJ];
    unsigned n = 0;
    for (unsigned i = 0; i < SS_OBJ; ++i)
        if (g->obj[i].alive) order[n++] = (uint8_t)i;
    for (unsigned i = 1; i < n; ++i) { /* far to near */
        const uint8_t k = order[i];
        unsigned j = i;
        for (; j > 0 && g->obj[order[j - 1]].z < g->obj[k].z; --j) order[j] = order[j - 1];
        order[j] = k;
    }
    for (unsigned i = 0; i < n; ++i) {
        const ss_obj_t *o = &g->obj[order[i]];
        /* enemies hover above the line of fire and come down to it as they close in */
        const int lift = o->z > 800 ? 700 : o->z - SS_ZN, y_off = o->kind == SO_ENEMY ? (50 + gfx_sin256(now / 14U + o->seed) / 20) * lift / 700 : ((int)(o->seed >> 4) - 8) * 6;
        const int x = vx(&v, o->x, o->z), y = vy(&v, y_off, o->z), rad = o->r * FOC / (o->z < 40 ? 40 : o->z);
        if (x + rad < 0 || x - rad >= APP_CANVAS_W) continue;
        if (o->kind == SO_ENEMY) draw_enemy(px, x, y, rad, o, now);
        else draw_rock(px, x, y, rad, o);
    }
    for (unsigned i = 0; i < SS_BOLT; ++i) {
        const ss_bolt_t *b = &g->bolt[i];
        if (b->alive) draw_bolt(px, vx(&v, b->x, b->z), vy(&v, 20, b->z), b->z, now);
    }
    for (unsigned i = 0; i < SS_SHOT; ++i)
        if (g->shot[i].alive) draw_shot(px, &v, &g->shot[i]);
    for (unsigned i = 0; i < SS_FX; ++i) {
        const ss_fx_t *f = &g->fx[i];
        if (!f->alive) continue;
        if (f->kind == SX_SHIP) {
            const unsigned t = f->age * 256U / 50U;
            draw_explosion(px, CX, 36, 30 + (int)f->size, f->age, 50, i);
            if (t < 140U) gfx_tint(px, 0, 0, APP_CANVAS_W, DASH_Y, 0xFFF0C0, 200U - t);
            continue;
        }
        if (f->z <= SS_ZN + 30) continue; /* a hit on the ship itself: the red flash below */
        const int z = f->z, x = vx(&v, f->x, z), y = vy(&v, f->kind == SX_ROCK ? 0 : 20, z), rad = f->size * 4 * FOC / (z < 40 ? 40 : z);
        if (f->kind == SX_HIT && f->age > 6U) continue;
        draw_explosion(px, x, y, f->kind == SX_HIT ? 2 + rad / 2 : rad, f->age, f->kind == SX_HIT ? 7U : 18U, i);
    }
    draw_warnings(px, &v, g, now);
    if (g->phase == SS_PLAY || g->phase == SS_DYING) draw_crosshair(px, g, now);
    if (g->hit_age_ms < 420U && g->phase != SS_TITLE) { /* a red flush round the edges when the ship is hit */
        const unsigned a = (420U - g->hit_age_ms) * 150U / 420U;
        gfx_tint(px, 0, 0, APP_CANVAS_W, 3, 0xFF1A1A, a);
        gfx_tint(px, 0, 3, 5, DASH_Y, 0xFF1A1A, a);
        gfx_tint(px, APP_CANVAS_W - 5, 3, 5, DASH_Y, 0xFF1A1A, a);
        gfx_tint(px, 0, 3, APP_CANVAS_W, DASH_Y, 0xFF2A1A, a / 3U);
    }
    draw_cockpit(px, s, now);
    if (g->phase == SS_TITLE || g->phase == SS_OVER) {
        gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0x01030C, g->phase == SS_OVER ? 150U : 80U);
        gfx_tint(px, 14, 6, 132, 38, 0x061024, 170);
        gfx_hline(px, 14, 145, 6, c565(0x4A78C8));
        gfx_hline(px, 14, 145, 43, c565(0x4A78C8));
        gfx_vline(px, 14, 6, 43, c565(0x2A4888));
        gfx_vline(px, 145, 6, 43, c565(0x2A4888));
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
    s->band = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, APP_HEIGHT - BAND_Y, 0x0A1024, 0);
    s->line = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, 1, 0x2E4A8E, 0);
    s->msg = app_ui_label(s->band, &aihook_font_12, 3, 2, 126, 14, LV_LABEL_LONG_MODE_CLIP);
    s->score = app_ui_label(s->band, &aihook_font_12, 128, 2, 32, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->msg, app_ui_color(0xCFE0F8), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->score, app_ui_color(0xFFD84A), LV_PART_MAIN);
    s->hold = app_ui_rect(root, 0, BAND_Y, 0, 2, APP_COLOR_ACCENT, 0); /* the hold towards the exit */
    app_ui_hide(s->hold, true);
    s->tag = app_ui_rect(root, 128, 5, 29, 14, 0x2E8B57, 5);
    s->tag_text = app_ui_label(s->tag, &aihook_font_12, 0, 1, 29, 12, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->tag_text);
    app_ui_text(s->tag_text, "舵机");
    s->title = app_ui_label(root, &aihook_font_16, 14, 9, 132, 20, LV_LABEL_LONG_MODE_CLIP);
    s->sub = app_ui_label(root, &aihook_font_12, 14, 28, 132, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->title);
    app_ui_center(s->sub);
    lv_obj_set_style_text_color(s->title, app_ui_color(0xBFE6FF), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->sub, app_ui_color(0xFFD84A), LV_PART_MAIN);
    app_ui_hide(s->tag, true);
    app_ui_hide(s->title, true);
    app_ui_hide(s->sub, true);
    s->shown_key = s->shown_score = s->shown_best = 0xFFFFFFFFU;
}
static void publish(const state_t *s, bool active, bool frame)
{
    const ss_t *g = &s->core;
    const haptic_state_t hs = haptic_state();
    portENTER_CRITICAL(&s_lock);
    s_status.active = active; s_status.phase = g->phase; s_status.hp = g->hp; s_status.src = s->steer.source;
    s_status.servo = (uint8_t)hs; s_status.score = g->score; s_status.best = g->best; s_status.kills = g->kills;
    s_status.shots = g->shots_fired; s_status.steer = s->steer.steer; s_status.cam = g->cam_x;
    s_status.objs = (uint8_t)ss_live_objects(g); s_status.foes = 0;
    for (unsigned i = 0; i < SS_OBJ; ++i) s_status.foes = (uint8_t)(s_status.foes + (g->obj[i].alive && g->obj[i].kind == SO_ENEMY));
    if (frame) ++s_status.frames;
    portEXIT_CRITICAL(&s_lock);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    ss_init(&s->core, app_random() ^ (uint32_t)ctx->now_ms);
    steer_init(&s->steer);
    haptic_init(&s->haptic);
    s->last_ms = ctx->now_ms;
    app_set_triple_exit(ctx, false); /* a cannon is fired by mashing the key: three quick presses are never an exit */
    portENTER_CRITICAL(&s_lock); s_status = (status_t){0}; portEXIT_CRITICAL(&s_lock);
    publish(s, true, false);
}
static bool idle_screen(const state_t *s)
{
    return s->core.phase == SS_TITLE || (s->core.phase == SS_OVER && s->core.over_age_ms > 800U);
}
/* The way out is a two second hold on the title or game over screen, and only for a press that began there: a gun
 * held down through a long fight must not leave the moment the ship is lost. */
static void sync_exit(app_context_t *ctx, const state_t *s)
{
    app_set_exit_hold(ctx, idle_screen(s) && !s->down_in_play ? EXIT_HOLD_MS : 0U);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t now = ctx->now_ms;
    app_radar_t radar;
    app_radar_read(&radar);
    if (radar.available && radar.online) steer_radar(&s->steer, now, true, radar.frame.targets);
    ss_steer(&s->core, steer_tick(&s->steer, now));
    sync_exit(ctx, s);
    s->prev_cam = s->core.cam_x;
    ss_advance(&s->core, now > s->last_ms ? (uint32_t)(now - s->last_ms) : 0U);
    s->last_ms = now;
    app_servo_t sv;
    app_servo_read(&sv);
    while (ss_take_hit(&s->core)) /* a light shake for every point of shield lost, a longer one for the last */
        if (sv.available && sv.armed) haptic_shake(&s->haptic, now, s->core.hp ? 4 : 7, 120, s->core.hp ? 480 : 900, sv.speed_dps);
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
    ss_t *g = &s->core;
    const bool side = e->key == 1U || e->key == 2U; /* K2 steers left, K3 right */
    switch (e->kind) {
    case APP_EVENT_DOWN:
        if (side) steer_key(&s->steer, e->key, true);
        else {
            s->down_in_play = g->phase == SS_PLAY || g->phase == SS_DYING;
            ss_fire(g, true);
        }
        break;
    case APP_EVENT_UP:
        if (side) steer_key(&s->steer, e->key, false);
        else ss_fire(g, false);
        break;
    case APP_EVENT_SHORT:
        /* A press that began in the game is a gun, not a "start". */
        if (!side && !s->down_in_play && idle_screen(s)) {
            ss_start(g);
            steer_recentre(&s->steer);
        }
        break;
    case APP_EVENT_CONFIRM:
        /* A deliberate half-second hold on the title or game over screen is the only thing that enables the servo. */
        if (!side && !s->down_in_play && (g->phase == SS_TITLE || g->phase == SS_OVER)) (void)haptic_arm(ctx, &s->haptic);
        break;
    case APP_EVENT_CANCEL:
        steer_release_keys(&s->steer);
        ss_fire(g, false);
        break;
    default: break;
    }
    sync_exit(ctx, s);
    publish(s, true, false);
}
static void sync_hud(state_t *s, uint64_t now, unsigned hold_ms)
{
    const ss_t *g = &s->core;
    const haptic_state_t hs = haptic_state();
    const char *msg = "";
    switch (g->phase) {
    case SS_TITLE: msg = hs == HAPTIC_READY ? "按键出击 长按启用舵机" : "按键出击 雷达转向"; break;
    case SS_PLAY: msg = hs == HAPTIC_ARMED ? "按住开火 舵机已启用" : "按住开火 雷达转向"; break;
    case SS_DYING: msg = "战机被击毁!"; break;
    default: msg = g->over_age_ms > 800U ? "按键再战" : "战机损毁"; break;
    }
    const bool idle = idle_screen(s), exit_slot = idle && (now / HINT_SLOT_MS) % 2U == 1U; /* the hint of the way out alternates */
    if (exit_slot) msg = "按住2秒退出";
    const uint32_t key = (uint32_t)g->phase << 8 | (uint32_t)hs << 4 | (idle ? 1U : 0U) | (exit_slot ? 2U : 0U);
    if (key != s->shown_key) { app_ui_text(s->msg, msg); s->shown_key = key; }
    if (s->shown_score != g->score) {
        char text[16];
        snprintf(text, sizeof text, "%lu", (unsigned long)g->score);
        app_ui_text(s->score, text);
        s->shown_score = g->score;
    }
    app_ui_hold_line(s->hold, hold_ms, hs == HAPTIC_READY, EXIT_HOLD_MS);
    app_ui_hide(s->tag, hs != HAPTIC_ARMED);
    const bool card = g->phase == SS_TITLE || g->phase == SS_OVER;
    app_ui_hide(s->title, !card);
    app_ui_hide(s->sub, !card);
    if (card && (s->shown_best != g->best + g->kills * 100003U + g->phase)) {
        char text[48];
        app_ui_text(s->title, g->phase == SS_TITLE ? "星际战机" : "战机损毁");
        if (g->phase == SS_TITLE) snprintf(text, sizeof text, "最高 %lu", (unsigned long)g->best);
        else snprintf(text, sizeof text, "击落 %lu 最高 %lu", (unsigned long)g->kills, (unsigned long)g->best);
        app_ui_text(s->sub, text);
        s->shown_best = g->best + g->kills * 100003U + g->phase;
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    sync_hud(s, ctx->now_ms, app_exit_hold_progress(ctx));
    draw_scene(app_ui_pixels(), s, (unsigned)ctx->now_ms);
    app_ui_plot_refresh(s->plot, &s->image);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (strcmp(line, "robot starship status")) return false;
    status_t v;
    portENTER_CRITICAL(&s_lock); v = s_status; portEXIT_CRITICAL(&s_lock);
    char reply[256];
    snprintf(reply, sizeof reply,
             "active=%u phase=%u hp=%u score=%lu best=%lu kills=%lu shots=%lu steer=%d cam=%d objs=%u foes=%u src=%u servo=%u frames=%lu",
             v.active ? 1U : 0U, v.phase, v.hp, (unsigned long)v.score, (unsigned long)v.best, (unsigned long)v.kills,
             (unsigned long)v.shots, v.steer, v.cam, v.objs, v.foes, v.src, v.servo, (unsigned long)v.frames);
    (void)app_transport_write(reply);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x + 1, y + 1, 26, 26, selected ? 0x1A2A60 : 0x0E1840, 5);        /* space */
    app_ui_rect(parent, x + 17, y + 4, 8, 8, 0xC8844E, 4);                                /* planet */
    app_ui_rect(parent, x + 15, y + 8, 12, 1, 0xE6D2A0, 0);
    app_ui_rect(parent, x + 4, y + 5, 1, 1, 0xFFFFFF, 0);                                 /* stars */
    app_ui_rect(parent, x + 9, y + 3, 1, 1, 0xCFE0FF, 0);
    app_ui_rect(parent, x + 5, y + 12, 1, 1, 0xCFE0FF, 0);
    app_ui_rect(parent, x + 12, y + 9, 5, 3, 0xE0403A, 2);                                /* enemy */
    app_ui_rect(parent, x + 10, y + 10, 9, 1, 0x9AA8C6, 0);
    app_ui_rect(parent, x + 13, y + 13, 1, 5, 0x6EF0FF, 0);                               /* laser */
    app_ui_rect(parent, x + 11, y + 19, 5, 6, 0xDCE4F0, 2);                               /* ship */
    app_ui_rect(parent, x + 5, y + 21, 17, 2, 0x8FA2D0, 0);
    app_ui_rect(parent, x + 5, y + 19, 2, 5, 0xFF7A3A, 0);
    app_ui_rect(parent, x + 20, y + 19, 2, 5, 0xFF7A3A, 0);
    app_ui_rect(parent, x + 13, y + 24, 1, 2, 0xFFC040, 0);                               /* engine glow */
}
const app_descriptor_t esphook_app_starship = {.api_version = APP_API_VERSION, .id = "starship", .title = "星际战机",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
