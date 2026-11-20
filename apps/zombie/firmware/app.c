#include "esphook/app_math.h"
#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_stack.h"
#include "esphook/app_game.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include "esphook/app_best.h"
#include "esphook/app_command.h"
#include "esphook/app_sync.h"
#include "zombie_core.h"
#include "zombie_models.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 僵尸围城: a first-person view of a graveyard at night on the shared 160x64 canvas, the bottom 16 px a message strip.
 * The radar (or the K2/K3 keys) turns the view, the main key fires the gun at the crosshair, and a zombie that gets
 * to you costs one of three lives and gives the servo a short shake when it has been enabled (hold a key for 0.5 s on
 * the title or game over screen; it is never enabled on its own). The rules live in zombie_core.c; this file draws
 * the world with the same bearing and distance the core uses. Zombies, gravestones and the gun are stacks of voxel
 * slices (esphook/app_stack.h, models in zombie_models.c): they turn, lean and show their sides as they weave. */
#define CX 80
#define HY 24     /* the horizon */
#define EYE 40    /* the eye is this high: things on the ground sit lower on the screen the nearer they are */
#define PROPS 24U
#define VEYE 90   /* the camera of the stacked models: how high above the ground it looks down at them (their tops show) */

typedef struct { int16_t z; uint8_t zombie, index; } item_t; /* what is in the picture: a prop (0), a zombie of the game (1), a title one (2) */

typedef struct {
    zb_t core;
    game_t game; /* steering, servo, status strip (esphook/app_game.h) */
    unsigned voxels; /* slices drawn in the last frame (the cost of the picture) */
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "zombie state too large");
_Static_assert(offsetof(state_t, core) == 0, "tests pose the scene through the core at the start of the state");
typedef struct {
    bool active;
    uint8_t phase, hp, wave, ammo, alive, src, servo;
    uint32_t score, best, kills, shots, hits, frames, remaining, voxels;
    int steer, cam;
} status_t;
static status_t s_status;

static int screen_x(const zb_t *g, int bearing, int shift) { return CX + (bearing - g->cam_x) / ZB_UNITS_PX * 256 / shift; }
static int feet_y(int z) { return HY + EYE * ZB_FOC / (z < 40 ? 40 : z); }
/* Things far off fade into the mist. */
static uint32_t misty(uint32_t rgb, int z) { return gfx_mix(rgb, 0x2A2040, (unsigned)app_clamp((z - 250) * 190 / 1250, 0, 190)); }

/* ---- the zombies ------------------------------------------------------------------------------------------------ */
static const stack_model_t *model_of(unsigned kind) { return kind == ZK_BRUTE ? &zm_brute : kind == ZK_RUNNER ? &zm_runner : &zm_walker; }
/* The view of something standing in the graveyard `z` away, its foot at screen column `x`. */
static void ground_view(stack_view_t *v, int x, int z, bool blood)
{
    stack_view_init(v);
    stack_view_world(v, x, feet_y(z), z, ZM_VOXEL, VEYE, ZB_FOC);
    v->tint = 0x2A2040; /* the mist */
    v->tint_amt = (unsigned)app_clamp((z - 250) * 190 / 1250, 0, 190);
    v->light = blood ? 0xFF9A80 : 0xC4D4FF; /* the moon lights the tops */
    v->top_light = 80;
    v->shade = 90;
    v->edge = z < 480 ? 70U : 0U;
}
/* `build` is how much of the stack is put together (256 all of it), `spin` turns it about its own axis (a showcase). */
static void draw_zombie(uint16_t *px, int cx, int z, const zb_z_t *zz, unsigned now, bool blood, unsigned build, int spin, unsigned *voxels)
{
    static const uint32_t shirts[4] = {0x7A4B3A, 0x3F5F86, 0x6A6A6A, 0x86403A};
    const stack_model_t *model = model_of(zz->kind);
    const int t = zz->dying_ms ? (int)(ZB_DYING_MS - zz->dying_ms) * 256 / (int)ZB_DYING_MS : 0;
    stack_view_t v;
    ground_view(&v, cx, z, blood);
    if (v.scale < 48) return;
    const unsigned stride = zz->kind == ZK_RUNNER ? 110U : zz->kind == ZK_BRUTE ? 260U : 180U;
    const unsigned rate = zz->kind == ZK_RUNNER ? 5U : zz->kind == ZK_BRUTE ? 2U : 3U;
    const int sign = zz->seed & 1U ? 1 : -1;
    /* it turns into its weave, and lurches */
    const int weave = (app_wave127((zz->age + 1U) * rate + zz->seed) - app_wave127(zz->age * rate + zz->seed)) * zz->amp / 127;
    const int lurch = app_sin256(now / (zz->kind == ZK_RUNNER ? 9U : 14U) + (unsigned)zz->seed * 37U) * (zz->kind == ZK_RUNNER ? 10 : zz->kind == ZK_BRUTE ? 7 : 12) / 256;
    v.yaw = (unsigned)(app_clamp(weave * 3 / 2, -26, 26) + lurch + spin + sign * t / 3) & 255U;
    v.step = zz->dying_ms || zz->stun ? 0U : 1U;                  /* a foot is always off the ground when it walks... */
    v.mirror = (uint8_t)(((now / stride) + zz->seed) & 1U);       /* ...and the feet take turns */
    v.build = build;
    if (z > 1100 && zz->age < 24U) v.build = (unsigned)zz->age * 256U / 24U; /* it takes shape as it comes out of the dark */
    v.lean = sign * t * (v.scale * model->h >> 8) * 3 / 100;      /* a falling one topples to a side... */
    v.squash = v.squash * (unsigned)(256 - t * 3 / 4) / 256U;     /* ...and sinks */
    v.dark = (unsigned)(t * 100 / 256);
    if (zz->flash) { v.tint = 0xFFFFFF; v.tint_amt = 170U; }
    uint32_t pal[ZM_COLORS];
    if (zz->kind == ZK_WALKER) { /* every walker has a shirt of its own */
        memcpy(pal, model->colors, sizeof pal);
        pal[ZM_SHIRT] = shirts[zz->seed & 3U];
        pal[ZM_SHIRT_DARK] = gfx_mix(pal[ZM_SHIRT], 0x000000, 90);
        v.palette = pal;
    }
    stack_shadow(px, model, &v, 0x000000, 90);
    *voxels += stack_draw(px, model, &v);
}
/* ---- the graveyard ------------------------------------------------------------------------------------------------ */
static void draw_prop(uint16_t *px, const zb_t *g, unsigned i, bool blood, unsigned *voxels)
{
    const uint32_t h = gfx_hash(i * 0x9E37U + 77U);
    const int bearing = (int)(h % 2900U) - 1450, z = 300 + (int)((h >> 12) % 1150U);
    const int x = screen_x(g, bearing, 256), feet = feet_y(z);
    if (x < -40 || x > APP_CANVAS_W + 40) return;
    if (z > 1100 && (h & 0x300000U)) { /* a bare tree: a few lines, too thin to be worth slices */
        const int ph = 90 * ZB_FOC / z;
        const uint16_t trunk = gfx_rgb565(misty(0x1C1A20, z));
        gfx_rect(px, x - 1, feet - ph, 2 + ph / 14, ph, trunk);
        gfx_line(px, x, feet - ph * 2 / 3, x - ph / 3, feet - ph * 9 / 10, trunk);
        gfx_line(px, x, feet - ph / 2, x + ph / 3, feet - ph * 4 / 5, trunk);
        gfx_line(px, x, feet - ph, x + ph / 6, feet - ph - ph / 6, trunk);
        return;
    }
    const stack_model_t *model = (h & 0x40000U) ? &zm_cross : &zm_tomb;
    stack_view_t v;
    ground_view(&v, x, z, blood);
    /* a stone stands where it was put and faces the middle of the view: the side towards you shows, so as the view turns
     * a stone that slides to the left of the screen shows its right side */
    v.yaw = (unsigned)((int)((h >> 8) & 31U) - 16 + stack_yaw_seen(x - CX, 120)) & 255U;
    v.edge = z < 450 ? 50U : 0U;
    stack_shadow(px, model, &v, 0x000000, 70);
    *voxels += stack_draw(px, model, &v);
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
    gfx_disc(px, mx, 9, 6, gfx_rgb565(blood ? 0xD8503A : 0xE8EEFF));
    gfx_disc(px, mx - 2, 8, 2, gfx_rgb565(blood ? 0xB8382A : 0xC4CCE4));
    gfx_disc(px, mx + 2, 12, 1, gfx_rgb565(blood ? 0xB8382A : 0xC4CCE4));
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
/* The gun you hold, from behind and above. Turning the view makes it swing the other way (`sway`), a shot kicks it, a
 * reload drops it, and at the start of a wave it is put together slice by slice (`build`). */
static void draw_gun(uint16_t *px, const zb_t *g, int sway, unsigned build, unsigned *voxels)
{
    const int kick = g->shot_age_ms < 140U ? (int)(140U - g->shot_age_ms) / 28 : 0;
    const int dip = g->reload ? (g->reload > ZB_RELOAD_STEPS - 8U ? (int)(ZB_RELOAD_STEPS - g->reload) * 2 : g->reload < 8U ? (int)g->reload * 2 : 16) : 0;
    stack_view_t v;
    stack_view_init(&v);
    v.x = CX + 3 + (g->reload ? (int)(g->reload % 4U) - 1 : 0);
    v.y = 66 + kick + dip;
    v.scale = 3 * 256 + 40;
    stack_view_pitch(&v, 26U + (unsigned)kick);
    v.yaw = (unsigned)(256 + sway) & 255U;
    v.build = build;
    v.edge = 50;
    v.top_light = 34;
    v.shade = 40;
    v.light = 0xB4C0E0;
    *voxels += stack_draw(px, &zm_gun, &v);
    if (g->shot_age_ms < 90U) { /* the muzzle flash */
        int fx, fy;
        stack_project(&zm_gun, &v, 4, 0, 3, &fx, &fy);
        fy -= 2;
        gfx_tint_ellipse(px, fx, fy, 14, 11, 0xFFB030, 90);
        gfx_disc(px, fx, fy, 5, gfx_rgb565(0xFFD060));
        gfx_disc(px, fx, fy, 3, gfx_rgb565(0xFFFFFF));
        gfx_line(px, fx, fy, fx - 9, fy - 7, gfx_rgb565(0xFFE070));
        gfx_line(px, fx, fy, fx + 9, fy - 7, gfx_rgb565(0xFFE070));
        gfx_line(px, fx, fy, fx, fy - 11, gfx_rgb565(0xFFE070));
    }
}
static void draw_heart(uint16_t *px, int x, int y, bool full)
{
    static const char *const rows[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
    const uint16_t body = gfx_rgb565(full ? 0xFF3A4A : 0x30242C);
    for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 7; ++col)
            if (rows[row][col] == '#') gfx_plot(px, x + col, y + row, body);
    if (full) gfx_plot(px, x + 1, y + 1, gfx_rgb565(0xFFC0C8));
}
static void draw_hud(uint16_t *px, const zb_t *g, unsigned now)
{
    for (unsigned i = 0; i < ZB_HP; ++i) draw_heart(px, 4 + (int)i * 9, 3, i < g->hp);
    for (unsigned i = 0; i < ZB_MAG; ++i) { /* the magazine */
        const bool have = i < g->ammo;
        gfx_rect(px, 114 + (int)i * 5, 3, 3, 6, gfx_rgb565(have ? 0xE8C860 : 0x35302A));
        if (have) gfx_rect(px, 114 + (int)i * 5, 3, 3, 2, gfx_rgb565(0xD08040));
    }
    if (g->reload) { /* reloading: a bar under the magazine fills */
        const int w = (int)(ZB_RELOAD_STEPS - g->reload) * 40 / (int)ZB_RELOAD_STEPS;
        gfx_rect(px, 114, 10, 40, 2, gfx_rgb565(0x2A2A30));
        gfx_rect(px, 114, 10, w, 2, gfx_rgb565(0xF0A030));
    } else if (g->ammo <= 2U && g->phase == ZB_PLAY && ((now / 160U) & 1U)) gfx_hline(px, 114, 153, 10, gfx_rgb565(0xFF4A3A));
}
static void draw_crosshair(uint16_t *px, const zb_t *g)
{
    bool on = false;
    for (unsigned i = 0; i < ZB_Z; ++i) on = on || zb_on_target(g, i);
    const uint16_t c = gfx_rgb565(on ? 0xFF4A3A : 0xE8E8E8);
    const int y = HY + 9;
    gfx_hline(px, CX - 7, CX - 3, y, c); gfx_hline(px, CX + 3, CX + 7, y, c);
    gfx_vline(px, CX, y - 7, y - 3, c); gfx_vline(px, CX, y + 3, y + 7, c);
    if (on) gfx_plot(px, CX, y, c);
    if (g->hitmark_age_ms < 140U) { /* a hit marker */
        const uint16_t m = gfx_rgb565(0xFFFFFF);
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
        if (left > 150 ? blink : true) gfx_tri(px, 1, HY + 9, 8, HY + 4, 8, HY + 14, gfx_rgb565(left > 150 ? 0xFF5040 : 0xB04030));
    }
    if (right > 20) {
        gfx_tint(px, APP_CANVAS_W - 2 - right / 64, 4, 2 + right / 64, 56, 0xFF2418, (unsigned)right * 3U / 5U);
        if (right > 150 ? blink : true) gfx_tri(px, APP_CANVAS_W - 2, HY + 9, APP_CANVAS_W - 9, HY + 4, APP_CANVAS_W - 9, HY + 14, gfx_rgb565(right > 150 ? 0xFF5040 : 0xB04030));
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
    const bool blood = g->wave >= 5U;
    unsigned voxels = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (!order[i].zombie) draw_prop(px, g, order[i].index, blood, &voxels);
        else if (order[i].zombie == 1) {
            const zb_z_t *z = &g->z[order[i].index];
            draw_zombie(px, screen_x(g, z->x, 256), z->z < ZB_ZN ? ZB_ZN : z->z, z, now, blood, 256U, 0, &voxels);
        } else { /* the title: a pair that put themselves together slice by slice, and turn on the spot */
            const zb_z_t *z = &idle[order[i].index];
            const unsigned build = g->phase_ms >= 1400U ? 256U : g->phase_ms * 256U / 1400U;
            draw_zombie(px, screen_x(g, z->x, 256), z->z, z, now, blood, build, order[i].index ? -(int)(now / 64U) : (int)(now / 48U), &voxels);
        }
    }
    draw_fx(px, g);
    if (g->phase != ZB_TITLE && g->phase != ZB_OVER) draw_edges(px, g, now);
    /* the edges of the screen are dark, like the light of the moon is not quite enough */
    gfx_tint(px, 0, 0, 7, APP_CANVAS_H, 0x000000, 70); gfx_tint(px, APP_CANVAS_W - 7, 0, 7, APP_CANVAS_H, 0x000000, 70);
    gfx_tint(px, 0, 0, APP_CANVAS_W, 3, 0x000000, 60);
    if (g->phase == ZB_PLAY || g->phase == ZB_INTRO || g->phase == ZB_DYING) draw_crosshair(px, g);
    const int sway = app_clamp((g->target_x - g->cam_x) / 50, -10, 10);
    const unsigned gun_build = (g->phase == ZB_TITLE || g->phase == ZB_INTRO) && g->phase_ms < 700U ? g->phase_ms * 256U / 700U : 256U;
    draw_gun(px, g, sway, gun_build, &voxels);
    s->voxels = voxels;
    draw_hud(px, g, now);
    if (g->bite_age_ms < 520U && g->phase != ZB_TITLE) { /* a bite: the screen goes red and claw marks cross it */
        const unsigned a = (520U - g->bite_age_ms) * 170U / 520U;
        gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0xE01010, a);
        if (g->bite_age_ms < 360U)
            for (int k = 0; k < 3; ++k) gfx_line(px, 52 + k * 17, 6 + k, 70 + k * 17, 52 + k, gfx_rgb565(0xFFD0C0));
    }
    if (g->phase == ZB_DYING) gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0x500000, 90U + (unsigned)(ZB_LOST_STEPS - g->dying) * 2U);
    if (g->phase == ZB_TITLE || g->phase == ZB_OVER || g->phase == ZB_INTRO) {
        gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0x000008, g->phase == ZB_OVER ? 140U : g->phase == ZB_TITLE ? 70U : 40U);
        gfx_tint(px, 14, 5, 132, 38, 0x0A0612, 185);
        gfx_hline(px, 14, 145, 5, gfx_rgb565(0x8A2A2A));
        gfx_hline(px, 14, 145, 42, gfx_rgb565(0x8A2A2A));
        gfx_vline(px, 14, 5, 42, gfx_rgb565(0x4A1A1A));
        gfx_vline(px, 145, 5, 42, gfx_rgb565(0x4A1A1A));
    }
}

/* ---- app ------------------------------------------------------------------------------------------------------- */
static void build(state_t *s, lv_obj_t *root)
{
    static const game_style_t style = {.band_bg = 0x0E0A12, .band_rule = 0x5A1E1E, .msg_rgb = 0xE0D4D8, .score_rgb = 0xFFD84A,
        .title_rgb = 0xFF5A4A, .sub_rgb = 0xF0E0C0, .msg_w = 118, .score_x = 118, .score_w = 41, .score_right = true,
        .tag_x = 126, .tag_y = 15, .title_y = 7, .sub_y = 26};
    game_build(&s->game, root, &style);
}
static void publish(const state_t *s, bool active, bool frame)
{
    const zb_t *g = &s->core;
    const haptic_state_t hs = haptic_state();
    app_critical_enter();
    s_status.active = active; s_status.phase = g->phase; s_status.hp = g->hp; s_status.wave = g->wave;
    s_status.ammo = g->ammo; s_status.alive = (uint8_t)zb_alive(g); s_status.src = s->game.steer.source; s_status.servo = (uint8_t)hs;
    s_status.score = g->score; s_status.best = g->best; s_status.kills = g->kills; s_status.shots = g->shots_fired;
    s_status.hits = g->hits; s_status.remaining = zb_remaining(g); s_status.steer = s->game.steer.steer; s_status.cam = g->cam_x;
    s_status.voxels = s->voxels;
    if (frame) ++s_status.frames;
    app_critical_exit();
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    zb_init(&s->core, app_random() ^ (uint32_t)ctx->now_ms);
    s->core.best = app_best_get(ctx); /* the record survives leaving the game and a reboot */
    game_enter(ctx, &s->game); /* the gun is fired by mashing the key: three quick presses are never an exit */
    app_critical_enter(); s_status = (status_t){0}; app_critical_exit();
    publish(s, true, false);
}
static bool idle_screen(const state_t *s)
{
    return game_idle(s->core.phase == ZB_TITLE, s->core.phase == ZB_OVER, s->core.over_age_ms);
}
static bool in_fight(const state_t *s)
{
    return s->core.phase == ZB_INTRO || s->core.phase == ZB_PLAY || s->core.phase == ZB_DYING;
}
/* The way out is a two second hold on the title or game over screen, and only for a press that began there: a gun
 * held down through a long fight must not leave the moment the last life is gone. */
static void sync_exit(app_context_t *ctx, const state_t *s)
{
    game_sync_exit(ctx, idle_screen(s));
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t now = ctx->now_ms;
    zb_steer(&s->core, game_steer(ctx, &s->game));
    sync_exit(ctx, s);
    zb_advance(&s->core, game_dt(&s->game, now));
    if (s->core.phase == ZB_OVER) (void)app_best_record(ctx, s->core.best);
    while (zb_take_hit(&s->core)) /* a shake for every bite, a longer one for the last */
        game_shake(ctx, &s->game, s->core.hp ? 4 : 7, 120, s->core.hp ? 480 : 900);
    haptic_pump(ctx, &s->game.haptic);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    game_leave(&s->game);
    app_critical_enter(); s_status.active = false; app_critical_exit();
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    zb_t *g = &s->core;
    const bool side = game_side(e); /* K2 turns left, K3 right */
    switch (e->kind) {
    case APP_EVENT_DOWN:
        if (side) steer_key(&s->game.steer, e->key, true);
        else {
            s->game.down_in_play = in_fight(s);
            zb_fire(g, true);
        }
        break;
    case APP_EVENT_UP:
        if (side) steer_key(&s->game.steer, e->key, false);
        else zb_fire(g, false);
        break;
    case APP_EVENT_SHORT:
        /* A press that began in the fight is a gun, not a "start". */
        if (game_may_start(&s->game, side, idle_screen(s))) {
            zb_start(g);
            steer_recentre(&s->game.steer);
        }
        break;
    case APP_EVENT_CONFIRM:
        /* A deliberate half-second hold on the title or game over screen is the only thing that enables the servo. */
        game_confirm(ctx, &s->game, side, g->phase == ZB_TITLE || g->phase == ZB_OVER);
        break;
    case APP_EVENT_CANCEL:
        steer_release_keys(&s->game.steer);
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
    const bool idle = idle_screen(s);
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
    game_hud_msg(&s->game, text, gfx_hash((uint32_t)g->phase << 8 | (uint32_t)hs << 4 | (idle ? 1U : 0U) | (teach ? 4U : 0U) |
                                          (uint32_t)g->wave << 12 | (uint32_t)remaining << 20), idle, now);
    game_hud_score(&s->game, g->score);
    game_hud_servo(&s->game, hold_ms);
    const bool card = g->phase == ZB_TITLE || g->phase == ZB_OVER || g->phase == ZB_INTRO;
    if (game_hud_card(&s->game, card, gfx_hash(g->best + g->kills * 100003U + (uint32_t)g->phase * 17U + (uint32_t)g->wave * 131U + g->score))) {
        char title[16];
        if (g->phase == ZB_TITLE) {
            snprintf(title, sizeof title, "僵尸围城");
            snprintf(text, sizeof text, "最高 %lu", (unsigned long)g->best);
        } else if (g->phase == ZB_INTRO) {
            snprintf(title, sizeof title, "第%u波", g->wave);
            snprintf(text, sizeof text, "僵尸 %u 只", g->wave_total);
        } else {
            snprintf(title, sizeof title, "%s", g->new_best ? "新纪录!" : "游戏结束");
            snprintf(text, sizeof text, "第%u波 击杀%lu 最高%lu", g->wave, (unsigned long)g->kills, (unsigned long)g->best);
        }
        game_hud_card_text(&s->game, title, text);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->game.plot) build(s, ctx->root);
    sync_hud(s, ctx->now_ms, app_exit_hold_progress(ctx));
    draw_scene(app_ui_pixels(), s, (unsigned)ctx->now_ms);
    app_ui_canvas_refresh(s->game.plot);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot zombie status")) return false;
    status_t v;
    app_shared_copy(v, s_status);
    (void)app_command_replyf("active=%u phase=%u wave=%u hp=%u ammo=%u alive=%u remain=%lu score=%lu best=%lu kills=%lu shots=%lu hits=%lu steer=%d cam=%d src=%u servo=%u frames=%lu vox=%lu",
            v.active ? 1U : 0U, v.phase, v.wave, v.hp, v.ammo, v.alive, (unsigned long)v.remaining, (unsigned long)v.score, (unsigned long)v.best,
            (unsigned long)v.kills, (unsigned long)v.shots, (unsigned long)v.hits, v.steer, v.cam, v.src, v.servo, (unsigned long)v.frames, (unsigned long)v.voxels);
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
    .flags = APP_SERVO_DEFAULT_HAPTIC,
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
