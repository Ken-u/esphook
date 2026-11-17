#include "esphook/app_math.h"
#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_haptic.h"
#include "esphook/app_services.h"
#include "esphook/app_steer.h"
#include "esphook/app_ui.h"
#include "esphook/app_best.h"
#include "esphook/app_command.h"
#include "esphook/app_sync.h"
#include "zombie_core.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 僵尸围城: a first-person view of a graveyard at night on the shared 160x64 canvas, the bottom 16 px a message strip.
 * The radar (or the K2/K3 keys) turns the view, the main key fires the gun at the crosshair, and a zombie that gets
 * to you costs one of three lives and gives the servo a short shake when it has been enabled (hold a key for 0.5 s on
 * the title or game over screen; it is never enabled on its own). The rules live in zombie_core.c; this file draws
 * the world with the same bearing and distance the core uses. */
#define BAND_Y 64
#define CX 80
#define HY 24     /* the horizon */
#define EYE 40    /* the eye is this high: things on the ground sit lower on the screen the nearer they are */
#define EXIT_HOLD_MS 2000U
#define HINT_SLOT_MS 2600U
#define PROPS 24U

typedef struct { int16_t z; uint8_t zombie, index; } item_t; /* what is in the picture: a prop (0), a zombie of the game (1), a title one (2) */

typedef struct {
    zb_t core;
    steer_t steer;
    haptic_t haptic;
    lv_obj_t *plot, *band, *hold, *msg, *score, *tag, *tag_text, *title, *sub;
    uint64_t last_ms;
    uint32_t shown_key, shown_score, shown_info;
    uint8_t down_in_play;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "zombie state too large");
_Static_assert(offsetof(state_t, core) == 0, "tests pose the scene through the core at the start of the state");
typedef struct {
    bool active;
    uint8_t phase, hp, wave, ammo, alive, src, servo;
    uint32_t score, best, kills, shots, hits, frames, remaining;
    int steer, cam;
} status_t;
static status_t s_status;

static uint16_t c565(uint32_t rgb) { return gfx_rgb565(rgb); }
static int screen_x(const zb_t *g, int bearing, int shift) { return CX + (bearing - g->cam_x) / ZB_UNITS_PX * 256 / shift; }
static int feet_y(int z) { return HY + EYE * ZB_FOC / (z < 40 ? 40 : z); }
/* Things far off fade into the mist. */
static uint32_t misty(uint32_t rgb, int z) { return gfx_mix(rgb, 0x2A2040, (unsigned)app_clamp((z - 250) * 190 / 1250, 0, 190)); }

/* ---- the zombies ------------------------------------------------------------------------------------------------ */
/* A colour as it is on this zombie right now: darker as it falls, white for a moment when it is hit, and misty by distance. */
static uint16_t paint(uint32_t rgb, int z, int fall, unsigned flash)
{
    return c565(misty(gfx_mix(gfx_mix(rgb, 0x1A1830, (unsigned)(fall * 100 / 256)), 0xFFFFFF, flash), z));
}
static void draw_zombie(uint16_t *px, int cx, int z, const zb_z_t *zz, unsigned now)
{
    static const uint32_t skin[ZK_KINDS] = {0x86A860, 0x93AE7C, 0x7C8C66};
    static const uint32_t shirts[4] = {0x7A4B3A, 0x3F5F86, 0x6A6A6A, 0x86403A};
    const zb_info_t *info = zb_info(zz->kind);
    const int feet = feet_y(z);
    const int full = info->height * ZB_FOC / z;
    const int t = zz->dying_ms ? (int)(ZB_DYING_MS - zz->dying_ms) * 256 / (int)ZB_DYING_MS : 0;
    const int h = (full < 3 ? 3 : full) * (256 - t * 3 / 4) / 256;
    if (cx + h < 0 || cx - h >= APP_CANVAS_W || feet - h >= APP_CANVAS_H || h < 2) return;
    const unsigned flash = zz->flash ? 170U : 0U;
#define PAINT(rgb) paint((rgb), z, t, flash)
    const int w = (h * (zz->kind == ZK_BRUTE ? 62 : zz->kind == ZK_RUNNER ? 34 : 44) / 100) * (256 + t / 2) / 256;
    const int stride = zz->kind == ZK_RUNNER ? 110 : zz->kind == ZK_BRUTE ? 260 : 180;
    const bool step = (((now / (unsigned)stride) + zz->seed) & 1U) != 0U && !zz->dying_ms;
    const int leg_h = h * 40 / 100 > 1 ? h * 40 / 100 : 1, torso_h = h * 32 / 100 > 1 ? h * 32 / 100 : 1;
    const int head_r = h * 13 / 100 > 1 ? h * 13 / 100 : 1;
    const int y_legs = feet - leg_h, y_torso = y_legs - torso_h;
    const int shear = t * h / 420 * (zz->seed & 1U ? 1 : -1); /* a falling one topples to a side */
    const int lean = zz->kind == ZK_RUNNER && !zz->dying_ms ? (step ? 1 : -1) * (h / 14) : 0;
    const uint32_t shirt = zz->kind == ZK_BRUTE ? 0x5C2A2A : zz->kind == ZK_RUNNER ? 0x3E6E4A : shirts[zz->seed & 3U];
    const uint32_t pants = zz->kind == ZK_BRUTE ? 0x2A2A30 : 0x34343E;
    gfx_tint_ellipse(px, cx, feet, w * 6 / 10 + 1, h / 16 + 1, 0x000000, 90);
    if (h < 7) { /* a few pixels: a head and a body */
        gfx_rect(px, cx - w / 2 + shear, y_torso, w < 2 ? 2 : w, torso_h + leg_h, PAINT(shirt));
        gfx_rect(px, cx - 1 + shear + lean, y_torso - head_r - 1, 2, head_r + 1, PAINT(skin[zz->kind]));
        return;
    }
    const int lw = w * 38 / 100 > 1 ? w * 38 / 100 : 1, drop = h / 14 > 0 ? h / 14 : 1;
    gfx_rect(px, cx - w * 45 / 100, y_legs + (step ? drop : 0), lw, leg_h - (step ? drop : 0), PAINT(pants)); /* the gait */
    gfx_rect(px, cx + w * 45 / 100 - lw + 1, y_legs + (step ? 0 : drop), lw, leg_h - (step ? 0 : drop), PAINT(pants));
    gfx_rect(px, cx - w / 2 + shear / 2, y_torso, w + 1, torso_h, PAINT(shirt));
    gfx_rect(px, cx + w / 6 + shear / 2, y_torso, w / 3 + 1, torso_h, PAINT(gfx_mix(shirt, 0x000000, 90)));
    if (zz->seed & 4U && h >= 14) gfx_rect(px, cx - w / 4 + shear / 2, y_torso + torso_h / 4, w / 4 + 1, torso_h / 3, PAINT(0x7A1010)); /* a stain */
    if (zz->kind == ZK_BRUTE) gfx_rect(px, cx - w / 2 - 1 + shear / 2, y_torso, w + 3, h / 12 + 1, PAINT(0x3A2020)); /* heavy shoulders */
    const int ah = h / 12 > 1 ? h / 12 : 1, alen = w * 55 / 100 + 1, shoulder = y_torso + torso_h / 6 + (step ? 1 : 0);
    gfx_rect(px, cx - w / 2 - alen + shear / 2, shoulder, alen, ah, PAINT(skin[zz->kind])); /* the arms reach for you */
    gfx_rect(px, cx + w / 2 + 1 + shear / 2, shoulder, alen, ah, PAINT(skin[zz->kind]));
    gfx_rect(px, cx - w / 2 - alen + shear / 2, shoulder - ah * 2, ah + (h >= 20), ah * 2 + 1, PAINT(skin[zz->kind]));
    gfx_rect(px, cx + w / 2 + alen + 1 - ah - (h >= 20) + shear / 2, shoulder - ah * 2, ah + (h >= 20), ah * 2 + 1, PAINT(skin[zz->kind]));
    const int hx = cx + shear + lean, hy = y_torso - head_r + 1;
    gfx_disc(px, hx, hy, head_r, PAINT(zz->kind == ZK_RUNNER ? 0x3E6E4A : skin[zz->kind]));
    if (zz->kind == ZK_RUNNER) gfx_disc(px, hx, hy + 1, head_r > 2 ? head_r - 1 : 1, PAINT(skin[zz->kind]));
    if (head_r >= 2) {
        const uint16_t eye = zz->flash ? c565(0xFFFFFF) : c565(misty(0xFF3020, z));
        gfx_plot(px, hx - head_r / 2, hy - 0, eye);
        gfx_plot(px, hx + head_r / 2, hy - 0, eye);
        if (head_r >= 3) gfx_hline(px, hx - head_r / 2, hx + head_r / 2, hy + head_r / 2 + 1, PAINT(0x2A1010));
        if (head_r >= 3 && !(zz->seed & 8U)) gfx_hline(px, hx - head_r + 1, hx + head_r - 1, hy - head_r + 1, PAINT(0x2A2A22)); /* what is left of the hair */
    }
#undef PAINT
}
/* ---- the graveyard ------------------------------------------------------------------------------------------------ */
static void draw_prop(uint16_t *px, const zb_t *g, unsigned i)
{
    const uint32_t h = gfx_hash(i * 0x9E37U + 77U);
    const int bearing = (int)(h % 2900U) - 1450, z = 300 + (int)((h >> 12) % 1150U);
    const int x = screen_x(g, bearing, 256), feet = feet_y(z);
    const int ph = (z > 1100 && (h & 0x300000U) ? 90 : 38) * ZB_FOC / z, pw = 26 * ZB_FOC / z;
    if (x < -40 || x > APP_CANVAS_W + 40) return;
    if (z > 1100 && (h & 0x300000U)) { /* a bare tree */
        const uint16_t trunk = c565(misty(0x1C1A20, z));
        gfx_rect(px, x - 1, feet - ph, 2 + ph / 14, ph, trunk);
        gfx_line(px, x, feet - ph * 2 / 3, x - ph / 3, feet - ph * 9 / 10, trunk);
        gfx_line(px, x, feet - ph / 2, x + ph / 3, feet - ph * 4 / 5, trunk);
        gfx_line(px, x, feet - ph, x + ph / 6, feet - ph - ph / 6, trunk);
        return;
    }
    const uint16_t stone = c565(misty(0x6E7490, z)), lit = c565(misty(0x959CB8, z)), dark = c565(misty(0x30333E, z));
    const int w = pw > 2 ? pw : 2, hh = ph > 3 ? ph : 3;
    if (h & 0x40000U) { /* a cross */
        gfx_rect(px, x - w / 6, feet - hh, w / 3 + 1, hh, stone);
        gfx_rect(px, x - w / 2, feet - hh * 2 / 3, w + 1, hh / 4 + 1, stone);
    } else { /* a rounded stone */
        gfx_rect(px, x - w / 2, feet - hh + w / 2, w + 1, hh - w / 2, stone);
        gfx_disc(px, x, feet - hh + w / 2, w / 2 + 1, stone);
        gfx_vline(px, x + w / 2, feet - hh + w / 2, feet, dark);
        gfx_plot(px, x - w / 4, feet - hh + 1, lit);
        if (hh >= 8) gfx_rect(px, x - w / 6, feet - hh * 2 / 3, w / 3 + 1, hh / 5 + 1, dark); /* an inscription */
    }
    gfx_hline(px, x - w / 2 - 1, x + w / 2 + 1, feet, dark);
}
static void draw_world(uint16_t *px, const zb_t *g, unsigned now)
{
    /* the night sky, a moon (red once the waves are big) and stars that move as the view turns */
    gfx_gradient(px, 0, HY + 1, 0x070A1E, 0x45304F);
    for (unsigned i = 0; i < 26U; ++i) {
        const uint32_t h = gfx_hash(i * 31U + 3U);
        const int x = ((int)(h % 640U) - (g->cam_x / 24) + 960) % 640 - 240, y = 1 + (int)((h >> 10) % 16U);
        if (x >= 0 && x < APP_CANVAS_W) gfx_tint_plot(px, x, y, 0xFFFFFF, 90U + (((now / 160U) + i * 5U) % 5U == 0U ? 100U : 0U));
    }
    const int mx = screen_x(g, -250, 3 * 256) + 8;
    const bool blood = g->wave >= 5U;
    gfx_tint_ellipse(px, mx, 9, 12, 12, blood ? 0xD04030 : 0xBFD0FF, 40);
    gfx_disc(px, mx, 9, 6, c565(blood ? 0xD8503A : 0xE8EEFF));
    gfx_disc(px, mx - 2, 8, 2, c565(blood ? 0xB8382A : 0xC4CCE4));
    gfx_disc(px, mx + 2, 12, 1, c565(blood ? 0xB8382A : 0xC4CCE4));
    for (unsigned i = 0; i < 3U; ++i) /* slow clouds across the moon */
        gfx_tint_ellipse(px, (screen_x(g, (int)i * 700 - 500, 2 * 256) + (int)(now / 300U)) % 220 - 30, 5 + (int)i * 5, 26 + (int)i * 6, 2, 0x201830, 110);
    gfx_ridge(px, g->cam_x / 9 + 60, 17, HY, 3, 7, 0x51D1U, 0x121626, 0x121626);
    gfx_gradient(px, HY, APP_CANVAS_H, 0x34402F, 0x0C100B);
    for (unsigned i = 0; i < 80U; ++i) { /* tufts and pebbles on the ground, the near ones spread out below */
        const uint32_t h = gfx_hash(i * 977U + 41U);
        const int z = 160 + (int)((h >> 8) % 1300U) * (int)((h >> 20) % 100U + 30U) / 130;
        const int x = screen_x(g, (int)(h % 2900U) - 1450, 256), y = feet_y(z);
        gfx_tint_plot(px, x, y, (h & 0x10000U) ? 0x6A7A4A : 0x0A0C08, 150);
        if ((h & 0x30000U) == 0U && z < 700) gfx_tint(px, x, y - 1, 1, 2, 0x6A7A4A, 120);
    }
    gfx_tint(px, 0, HY - 3, APP_CANVAS_W, 9, 0x7A6A92, 46); /* mist on the horizon */
}

/* ---- the gun, the HUD and the edges of the screen ----------------------------------------------------------------- */
static void draw_gun(uint16_t *px, const zb_t *g)
{
    const int kick = g->shot_age_ms < 140U ? (int)(140U - g->shot_age_ms) / 28 : 0;
    const int dip = g->reload ? (g->reload > ZB_RELOAD_STEPS - 8U ? (int)(ZB_RELOAD_STEPS - g->reload) * 2 : g->reload < 8U ? (int)g->reload * 2 : 16) : 0;
    const int oy = kick + dip, gx = CX + 3 + (g->reload ? (int)(g->reload % 4U) - 1 : 0);
    gfx_rect(px, gx - 6, 50 + oy, 13, 18, c565(0x2E3340));  /* the receiver */
    gfx_rect(px, gx - 6, 50 + oy, 3, 18, c565(0x4A5160));
    gfx_rect(px, gx - 3, 38 + oy, 7, 14, c565(0x555C6C));   /* the barrel */
    gfx_vline(px, gx - 2, 38 + oy, 50 + oy, c565(0x8A93A6));
    gfx_vline(px, gx + 3, 38 + oy, 50 + oy, c565(0x353A47));
    gfx_rect(px, gx - 3, 36 + oy, 7, 2, c565(0x23262F));    /* the muzzle */
    gfx_plot(px, gx, 35 + oy, c565(0xE8E8E8));              /* the sight */
    gfx_rect(px, gx - 15, 58 + oy, 8, 8, c565(0x8F6A4E));   /* the hands */
    gfx_rect(px, gx - 15, 58 + oy, 8, 2, c565(0xB08A68));
    gfx_rect(px, gx + 8, 56 + oy, 8, 10, c565(0x8F6A4E));
    gfx_rect(px, gx + 8, 56 + oy, 8, 2, c565(0xB08A68));
    gfx_rect(px, gx - 18, 64 + oy, 12, 6, c565(0x2A3A56));  /* the sleeves */
    gfx_rect(px, gx + 8, 64 + oy, 12, 6, c565(0x2A3A56));
    if (g->shot_age_ms < 90U) { /* the muzzle flash */
        const int fx = gx, fy = 34 + oy - kick;
        gfx_tint_ellipse(px, fx, fy, 14, 11, 0xFFB030, 90);
        gfx_disc(px, fx, fy, 5, c565(0xFFD060));
        gfx_disc(px, fx, fy, 3, c565(0xFFFFFF));
        gfx_line(px, fx, fy, fx - 9, fy - 7, c565(0xFFE070));
        gfx_line(px, fx, fy, fx + 9, fy - 7, c565(0xFFE070));
        gfx_line(px, fx, fy, fx, fy - 11, c565(0xFFE070));
    }
}
static void draw_heart(uint16_t *px, int x, int y, bool full)
{
    static const char *const rows[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
    const uint16_t body = c565(full ? 0xFF3A4A : 0x30242C);
    for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 7; ++col)
            if (rows[row][col] == '#') gfx_plot(px, x + col, y + row, body);
    if (full) gfx_plot(px, x + 1, y + 1, c565(0xFFC0C8));
}
static void draw_hud(uint16_t *px, const zb_t *g, unsigned now)
{
    for (unsigned i = 0; i < ZB_HP; ++i) draw_heart(px, 4 + (int)i * 9, 3, i < g->hp);
    for (unsigned i = 0; i < ZB_MAG; ++i) { /* the magazine */
        const bool have = i < g->ammo;
        gfx_rect(px, 114 + (int)i * 5, 3, 3, 6, c565(have ? 0xE8C860 : 0x35302A));
        if (have) gfx_rect(px, 114 + (int)i * 5, 3, 3, 2, c565(0xD08040));
    }
    if (g->reload) { /* reloading: a bar under the magazine fills */
        const int w = (int)(ZB_RELOAD_STEPS - g->reload) * 40 / (int)ZB_RELOAD_STEPS;
        gfx_rect(px, 114, 10, 40, 2, c565(0x2A2A30));
        gfx_rect(px, 114, 10, w, 2, c565(0xF0A030));
    } else if (g->ammo <= 2U && g->phase == ZB_PLAY && ((now / 160U) & 1U)) gfx_hline(px, 114, 153, 10, c565(0xFF4A3A));
}
static void draw_crosshair(uint16_t *px, const zb_t *g)
{
    bool on = false;
    for (unsigned i = 0; i < ZB_Z; ++i) on = on || zb_on_target(g, i);
    const uint16_t c = c565(on ? 0xFF4A3A : 0xE8E8E8);
    const int y = HY + 9;
    gfx_hline(px, CX - 7, CX - 3, y, c); gfx_hline(px, CX + 3, CX + 7, y, c);
    gfx_vline(px, CX, y - 7, y - 3, c); gfx_vline(px, CX, y + 3, y + 7, c);
    if (on) gfx_plot(px, CX, y, c);
    if (g->hitmark_age_ms < 140U) { /* a hit marker */
        const uint16_t m = c565(0xFFFFFF);
        gfx_line(px, CX - 5, y - 5, CX - 2, y - 2, m); gfx_line(px, CX + 5, y - 5, CX + 2, y - 2, m);
        gfx_line(px, CX - 5, y + 5, CX - 2, y + 2, m); gfx_line(px, CX + 5, y + 5, CX + 2, y + 2, m);
    }
}
/* The ones you cannot see: the nearest in each direction lights the edge of the screen. */
static void draw_edges(uint16_t *px, const zb_t *g, unsigned now)
{
    int left = 0, right = 0;
    for (unsigned i = 0; i < ZB_Z; ++i) {
        const zb_z_t *z = &g->z[i];
        if (!z->alive || z->dying_ms) continue;
        const int x = screen_x(g, z->x, 256), urgency = (ZB_ZF - z->z) * 255 / (ZB_ZF - ZB_ZN);
        if (x < -6) left = left > urgency ? left : urgency;
        else if (x > APP_CANVAS_W + 5) right = right > urgency ? right : urgency;
    }
    const bool blink = ((now / 110U) & 1U) != 0U;
    if (left > 20) {
        gfx_tint(px, 0, 4, 2 + left / 64, 56, 0xFF2418, (unsigned)left * 3U / 5U);
        if (left > 150 ? blink : true) gfx_tri(px, 1, HY + 9, 8, HY + 4, 8, HY + 14, c565(left > 150 ? 0xFF5040 : 0xB04030));
    }
    if (right > 20) {
        gfx_tint(px, APP_CANVAS_W - 2 - right / 64, 4, 2 + right / 64, 56, 0xFF2418, (unsigned)right * 3U / 5U);
        if (right > 150 ? blink : true) gfx_tri(px, APP_CANVAS_W - 2, HY + 9, APP_CANVAS_W - 9, HY + 4, APP_CANVAS_W - 9, HY + 14, c565(right > 150 ? 0xFF5040 : 0xB04030));
    }
}
static void draw_fx(uint16_t *px, const zb_t *g)
{
    for (unsigned i = 0; i < ZB_FX; ++i) {
        const zb_fx_t *f = &g->fx[i];
        if (!f->alive) continue;
        const int z = f->z < ZB_ZN ? ZB_ZN : f->z, x = screen_x(g, f->x, 256), y = feet_y(z) - zb_info(ZK_WALKER)->height * ZB_FOC / z / 2;
        const int big = zb_info(ZK_WALKER)->height * ZB_FOC / z / 6 + 2;
        if (f->kind == ZX_BITE) continue; /* drawn over the whole screen */
        const unsigned life = f->kind == ZX_SPLAT ? 22U : 10U;
        const unsigned a = 255U - f->age * 255U / life;
        for (unsigned k = 0; k < (f->kind == ZX_SPLAT ? 9U : 5U); ++k) {
            const unsigned ang = k * (256U / (f->kind == ZX_SPLAT ? 9U : 5U)) + (i * 37U);
            const int r = 1 + (int)f->age * big / 8 + (int)(k & 1U);
            gfx_tint_plot(px, x + gfx_sin256(ang + 64U) * r / 256, y + gfx_sin256(ang) * r / 320 + (int)f->age / 3, k & 1U ? 0xFF3A2A : 0xB01818, a);
            if (f->kind == ZX_SPLAT) gfx_tint_plot(px, x + gfx_sin256(ang + 64U) * r / 256 + 1, y + gfx_sin256(ang) * r / 320 + (int)f->age / 3, 0x701010, a / 2U);
        }
    }
}
static void draw_scene(uint16_t *px, state_t *s, unsigned now)
{
    const zb_t *g = &s->core;
    draw_world(px, g, now);
    item_t order[PROPS + ZB_Z + 3U];
    unsigned n = 0;
    for (unsigned i = 0; i < PROPS; ++i) {
        const uint32_t h = gfx_hash(i * 0x9E37U + 77U);
        order[n++] = (item_t){.z = (int16_t)(300 + (int)((h >> 12) % 1150U)), .zombie = 0, .index = (uint8_t)i};
    }
    zb_z_t idle[2]; /* the title screen has a pair of them wandering in the distance */
    if (g->phase == ZB_TITLE) {
        idle[0] = (zb_z_t){.alive = 1, .kind = ZK_WALKER, .x = -260, .z = 760, .seed = 5, .age = 0};
        idle[1] = (zb_z_t){.alive = 1, .kind = ZK_BRUTE, .x = 300, .z = 1000, .seed = 2};
        for (unsigned i = 0; i < 2U; ++i) order[n++] = (item_t){.z = idle[i].z, .zombie = 2, .index = (uint8_t)i};
    }
    for (unsigned i = 0; i < ZB_Z; ++i)
        if (g->z[i].alive) order[n++] = (item_t){.z = g->z[i].z, .zombie = 1, .index = (uint8_t)i};
    for (unsigned i = 1; i < n; ++i) { /* far to near */
        const item_t k = order[i];
        unsigned j = i;
        while (j && order[j - 1].z < k.z) { order[j] = order[j - 1]; --j; }
        order[j] = k;
    }
    for (unsigned i = 0; i < n; ++i) {
        if (!order[i].zombie) draw_prop(px, g, order[i].index);
        else {
            const zb_z_t *z = order[i].zombie == 1 ? &g->z[order[i].index] : &idle[order[i].index];
            draw_zombie(px, screen_x(g, z->x, 256), z->z < ZB_ZN ? ZB_ZN : z->z, z, now);
        }
    }
    draw_fx(px, g);
    if (g->phase != ZB_TITLE && g->phase != ZB_OVER) draw_edges(px, g, now);
    /* the edges of the screen are dark, like the light of the moon is not quite enough */
    gfx_tint(px, 0, 0, 7, APP_CANVAS_H, 0x000000, 70); gfx_tint(px, APP_CANVAS_W - 7, 0, 7, APP_CANVAS_H, 0x000000, 70);
    gfx_tint(px, 0, 0, APP_CANVAS_W, 3, 0x000000, 60);
    if (g->phase == ZB_PLAY || g->phase == ZB_INTRO || g->phase == ZB_DYING) draw_crosshair(px, g);
    draw_gun(px, g);
    draw_hud(px, g, now);
    if (g->bite_age_ms < 520U && g->phase != ZB_TITLE) { /* a bite: the screen goes red and claw marks cross it */
        const unsigned a = (520U - g->bite_age_ms) * 170U / 520U;
        gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0xE01010, a);
        if (g->bite_age_ms < 360U)
            for (int k = 0; k < 3; ++k) gfx_line(px, 52 + k * 17, 6 + k, 70 + k * 17, 52 + k, c565(0xFFD0C0));
    }
    if (g->phase == ZB_DYING) gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0x500000, 90U + (unsigned)(ZB_LOST_STEPS - g->dying) * 2U);
    if (g->phase == ZB_TITLE || g->phase == ZB_OVER || g->phase == ZB_INTRO) {
        gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0x000008, g->phase == ZB_OVER ? 140U : g->phase == ZB_TITLE ? 70U : 40U);
        gfx_tint(px, 14, 5, 132, 38, 0x0A0612, 185);
        gfx_hline(px, 14, 145, 5, c565(0x8A2A2A));
        gfx_hline(px, 14, 145, 42, c565(0x8A2A2A));
        gfx_vline(px, 14, 5, 42, c565(0x4A1A1A));
        gfx_vline(px, 145, 5, 42, c565(0x4A1A1A));
    }
}

/* ---- app ------------------------------------------------------------------------------------------------------- */
static void build(state_t *s, lv_obj_t *root)
{
    s->plot = app_ui_canvas(root, 0, 0, APP_CANVAS_W, APP_CANVAS_H);
    s->band = app_ui_band(root, 0x0E0A12, 0x5A1E1E);
    s->msg = app_ui_label(s->band, &aihook_font_12, 3, 2, 118, 14, LV_LABEL_LONG_MODE_CLIP);
    s->score = app_ui_label(s->band, &aihook_font_12, 118, 2, 41, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s->score, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s->msg, app_ui_color(0xE0D4D8), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->score, app_ui_color(0xFFD84A), LV_PART_MAIN);
    s->hold = app_ui_hold_bar(root, BAND_Y, APP_COLOR_ACCENT); /* the hold towards the exit */
    app_ui_hide(s->hold, true);
    s->tag = app_ui_rect(root, 126, 15, 29, 14, 0x2E8B57, 5);
    s->tag_text = app_ui_label(s->tag, &aihook_font_12, 0, 1, 29, 12, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->tag_text);
    app_ui_text(s->tag_text, "舵机");
    s->title = app_ui_label(root, &aihook_font_16, 14, 7, 132, 20, LV_LABEL_LONG_MODE_CLIP);
    s->sub = app_ui_label(root, &aihook_font_12, 14, 26, 132, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->title);
    app_ui_center(s->sub);
    lv_obj_set_style_text_color(s->title, app_ui_color(0xFF5A4A), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->sub, app_ui_color(0xF0E0C0), LV_PART_MAIN);
    app_ui_hide(s->tag, true);
    app_ui_hide(s->title, true);
    app_ui_hide(s->sub, true);
    s->shown_key = s->shown_score = s->shown_info = 0xFFFFFFFFU;
}
static void publish(const state_t *s, bool active, bool frame)
{
    const zb_t *g = &s->core;
    const haptic_state_t hs = haptic_state();
    app_critical_enter();
    s_status.active = active; s_status.phase = g->phase; s_status.hp = g->hp; s_status.wave = g->wave;
    s_status.ammo = g->ammo; s_status.alive = (uint8_t)zb_alive(g); s_status.src = s->steer.source; s_status.servo = (uint8_t)hs;
    s_status.score = g->score; s_status.best = g->best; s_status.kills = g->kills; s_status.shots = g->shots_fired;
    s_status.hits = g->hits; s_status.remaining = zb_remaining(g); s_status.steer = s->steer.steer; s_status.cam = g->cam_x;
    if (frame) ++s_status.frames;
    app_critical_exit();
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    zb_init(&s->core, app_random() ^ (uint32_t)ctx->now_ms);
    s->core.best = app_best_get(ctx); /* the record survives leaving the game and a reboot */
    steer_init(&s->steer);
    haptic_init(&s->haptic);
    s->last_ms = ctx->now_ms;
    app_set_triple_exit(ctx, false); /* the gun is fired by mashing the key: three quick presses are never an exit */
    app_critical_enter(); s_status = (status_t){0}; app_critical_exit();
    publish(s, true, false);
}
static bool idle_screen(const state_t *s)
{
    return s->core.phase == ZB_TITLE || (s->core.phase == ZB_OVER && s->core.over_age_ms > 800U);
}
static bool in_fight(const state_t *s)
{
    return s->core.phase == ZB_INTRO || s->core.phase == ZB_PLAY || s->core.phase == ZB_DYING;
}
/* The way out is a two second hold on the title or game over screen, and only for a press that began there: a gun
 * held down through a long fight must not leave the moment the last life is gone. */
static void sync_exit(app_context_t *ctx, const state_t *s)
{
    app_set_exit_hold(ctx, idle_screen(s) ? EXIT_HOLD_MS : 0U);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t now = ctx->now_ms;
    app_radar_t radar;
    app_radar_read(&radar);
    if (radar.available && radar.online) steer_radar(&s->steer, now, true, radar.frame.targets);
    zb_steer(&s->core, steer_tick(&s->steer, now));
    sync_exit(ctx, s);
    zb_advance(&s->core, now > s->last_ms ? (uint32_t)(now - s->last_ms) : 0U);
    s->last_ms = now;
    if (s->core.phase == ZB_OVER) (void)app_best_record(ctx, s->core.best);
    app_servo_t sv;
    app_servo_read(&sv);
    while (zb_take_hit(&s->core)) /* a shake for every bite, a longer one for the last */
        if (sv.available && sv.armed) haptic_shake(&s->haptic, now, s->core.hp ? 4 : 7, 120, s->core.hp ? 480 : 900, sv.speed_dps);
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
    zb_t *g = &s->core;
    const bool side = e->key == 1U || e->key == 2U; /* K2 turns left, K3 right */
    switch (e->kind) {
    case APP_EVENT_DOWN:
        if (side) steer_key(&s->steer, e->key, true);
        else {
            s->down_in_play = in_fight(s);
            zb_fire(g, true);
        }
        break;
    case APP_EVENT_UP:
        if (side) steer_key(&s->steer, e->key, false);
        else zb_fire(g, false);
        break;
    case APP_EVENT_SHORT:
        /* A press that began in the fight is a gun, not a "start". */
        if (!side && !s->down_in_play && idle_screen(s)) {
            zb_start(g);
            steer_recentre(&s->steer);
        }
        break;
    case APP_EVENT_CONFIRM:
        /* A deliberate half-second hold on the title or game over screen is the only thing that enables the servo. */
        if (!side && !s->down_in_play && (g->phase == ZB_TITLE || g->phase == ZB_OVER)) (void)haptic_arm(ctx, &s->haptic);
        break;
    case APP_EVENT_CANCEL:
        steer_release_keys(&s->steer);
        zb_fire(g, false);
        break;
    default: break;
    }
    sync_exit(ctx, s);
    publish(s, true, false);
}
static void sync_hud(state_t *s, uint64_t now, unsigned hold_ms)
{
    const zb_t *g = &s->core;
    const haptic_state_t hs = haptic_state();
    char text[48];
    const bool idle = idle_screen(s), exit_slot = idle && (now / HINT_SLOT_MS) % 2U == 1U; /* the hint of the way out alternates */
    const bool teach = g->phase == ZB_PLAY && g->play_steps < 300U;
    const unsigned remaining = zb_remaining(g);
    switch (g->phase) {
    case ZB_TITLE: snprintf(text, sizeof text, "%s", hs == HAPTIC_READY ? "按键开战 长按开舵机" : "按键开战 雷达转向"); break;
    case ZB_INTRO: snprintf(text, sizeof text, "第%u波 准备!", g->wave); break;
    case ZB_PLAY:
        if (teach) snprintf(text, sizeof text, "%s", hs == HAPTIC_ARMED ? "按住开火 舵机已启用" : "按住开火 雷达转向");
        else snprintf(text, sizeof text, "第%u波 还剩%u", g->wave, remaining);
        break;
    case ZB_DYING: snprintf(text, sizeof text, "你被咬了!"); break;
    default: snprintf(text, sizeof text, "%s", g->over_age_ms > 800U ? "按键再战" : "你被咬了!"); break;
    }
    if (exit_slot) snprintf(text, sizeof text, "按住2秒退出");
    const uint32_t key = gfx_hash((uint32_t)g->phase << 8 | (uint32_t)hs << 4 | (idle ? 1U : 0U) | (exit_slot ? 2U : 0U) | (teach ? 4U : 0U) |
                                  (uint32_t)g->wave << 12 | (uint32_t)remaining << 20);
    if (key != s->shown_key) { app_ui_text(s->msg, text); s->shown_key = key; }
    if (s->shown_score != g->score) {
        snprintf(text, sizeof text, "%lu", (unsigned long)g->score);
        app_ui_text(s->score, text);
        s->shown_score = g->score;
    }
    app_ui_hold_line(s->hold, hold_ms, hs == HAPTIC_READY, EXIT_HOLD_MS);
    app_ui_hide(s->tag, hs != HAPTIC_ARMED);
    const bool card = g->phase == ZB_TITLE || g->phase == ZB_OVER || g->phase == ZB_INTRO;
    app_ui_hide(s->title, !card);
    app_ui_hide(s->sub, !card);
    const uint32_t info = gfx_hash(g->best + g->kills * 100003U + (uint32_t)g->phase * 17U + (uint32_t)g->wave * 131U + g->score);
    if (card && s->shown_info != info) {
        s->shown_info = info;
        if (g->phase == ZB_TITLE) {
            app_ui_text(s->title, "僵尸围城");
            snprintf(text, sizeof text, "最高 %lu", (unsigned long)g->best);
        } else if (g->phase == ZB_INTRO) {
            snprintf(text, sizeof text, "第%u波", g->wave);
            app_ui_text(s->title, text);
            snprintf(text, sizeof text, "僵尸 %u 只", g->wave_total);
        } else {
            app_ui_text(s->title, g->new_best ? "新纪录!" : "游戏结束");
            snprintf(text, sizeof text, "第%u波 击杀%lu 最高%lu", g->wave, (unsigned long)g->kills, (unsigned long)g->best);
        }
        app_ui_text(s->sub, text);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    sync_hud(s, ctx->now_ms, app_exit_hold_progress(ctx));
    draw_scene(app_ui_pixels(), s, (unsigned)ctx->now_ms);
    app_ui_canvas_refresh(s->plot);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot zombie status")) return false;
    status_t v;
    app_shared_copy(v, s_status);
    (void)app_command_replyf("active=%u phase=%u wave=%u hp=%u ammo=%u alive=%u remain=%lu score=%lu best=%lu kills=%lu shots=%lu hits=%lu steer=%d cam=%d src=%u servo=%u frames=%lu",
            v.active ? 1U : 0U, v.phase, v.wave, v.hp, v.ammo, v.alive, (unsigned long)v.remaining, (unsigned long)v.score, (unsigned long)v.best,
            (unsigned long)v.kills, (unsigned long)v.shots, (unsigned long)v.hits, v.steer, v.cam, v.src, v.servo, (unsigned long)v.frames);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x + 1, y + 1, 26, 26, selected ? 0x2A1A38 : 0x161020, 5);        /* the night */
    app_ui_rect(parent, x + 18, y + 3, 6, 6, 0xE8EEFF, 3);                                /* the moon */
    app_ui_rect(parent, x + 1, y + 20, 26, 7, selected ? 0x2A3A24 : 0x1C2818, 3);         /* the ground */
    app_ui_rect(parent, x + 9, y + 12, 10, 10, 0x3F5F86, 1);                              /* a zombie */
    app_ui_rect(parent, x + 10, y + 4, 8, 8, 0x86A860, 4);
    app_ui_rect(parent, x + 11, y + 7, 2, 2, 0xFF3020, 0);
    app_ui_rect(parent, x + 15, y + 7, 2, 2, 0xFF3020, 0);
    app_ui_rect(parent, x + 5, y + 8, 3, 8, 0x86A860, 1);                                 /* arms up */
    app_ui_rect(parent, x + 20, y + 8, 3, 8, 0x86A860, 1);
    app_ui_rect(parent, x + 13, y + 17, 2, 9, 0xFF4A3A, 0);                               /* the crosshair */
    app_ui_rect(parent, x + 8, y + 12, 12, 2, 0xFF4A3A, 0);
}
const app_descriptor_t esphook_app_zombie = {.api_version = APP_API_VERSION, .id = "zombie", .title = "僵尸围城",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
