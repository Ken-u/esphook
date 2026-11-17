#include "zombie_core.h"
#include <string.h>

static int iabs(int v) { return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t rnd(zb_t *s)
{
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 17;
    s->rng ^= s->rng << 5;
    return s->rng;
}
static int rr(zb_t *s, int lo, int hi) { return lo + (int)(rnd(s) % (uint32_t)(hi - lo + 1)); }
/* -127..127 over a 256 step period (a parabola: close enough for weaving). */
static int isin(unsigned phase)
{
    const int x = (int)(phase & 127U), v = x * (128 - x) * 127 / 4096;
    return (phase & 128U) ? -v : v;
}
static uint32_t sat_add(uint32_t v, uint32_t d) { return v + d > ZB_AGE_MAX ? ZB_AGE_MAX : v + d; }
const zb_info_t *zb_info(unsigned kind)
{
    static const zb_info_t info[ZK_KINDS] = {
        {2, 5, 100, 22, 100},  /* the shambler */
        {1, 10, 150, 18, 94},  /* fast and thin */
        {6, 3, 500, 36, 128},  /* big, slow and hard to put down */
    };
    return &info[kind < ZK_KINDS ? kind : 0U];
}
unsigned zb_alive(const zb_t *s)
{
    unsigned n = 0;
    for (unsigned i = 0; i < ZB_Z; ++i) n += s->z[i].alive;
    return n;
}
unsigned zb_remaining(const zb_t *s) { return s->to_spawn + zb_alive(s); }

static void add_fx(zb_t *s, int x, int z, unsigned kind)
{
    for (unsigned i = 0; i < ZB_FX; ++i) {
        zb_fx_t *f = &s->fx[i];
        if (f->alive) continue;
        *f = (zb_fx_t){.x = (int16_t)x, .z = (int16_t)z, .alive = 1, .kind = (uint8_t)kind};
        return;
    }
}
void zb_init(zb_t *s, uint32_t seed)
{
    memset(s, 0, sizeof *s);
    s->rng = seed ? seed : 0xBADC0DE5U;
    s->phase = ZB_TITLE;
    s->hp = ZB_HP;
    s->ammo = ZB_MAG;
    s->shot_age_ms = s->hitmark_age_ms = s->bite_age_ms = s->kill_age_ms = s->over_age_ms = ZB_AGE_MAX;
}
static int speed_pct(const zb_t *s) { return 100 + (s->wave < 15U ? s->wave : 15U) * 4; }
static int spawn_gap_steps(const zb_t *s)
{
    const int ms = 1700 - 110 * (int)s->wave;
    return (ms < 450 ? 450 : ms) / (int)ZB_STEP_MS;
}
static void begin_wave(zb_t *s)
{
    ++s->wave;
    s->wave_total = (uint8_t)(5 + 2 * (s->wave < 13U ? s->wave : 13U));
    s->to_spawn = s->wave_total;
    s->spawn_wait = 25;
    s->phase = ZB_INTRO;
    s->phase_ms = 0;
    if (s->wave > 1U && s->hp < ZB_HP) ++s->hp; /* a breather between waves */
}
void zb_start(zb_t *s)
{
    if (s->phase != ZB_TITLE && s->phase != ZB_OVER) return;
    const uint32_t best = s->best, seed = s->rng;
    zb_init(s, seed ^ 0x9E3779B9U);
    s->best = best; /* the best score survives a restart */
    begin_wave(s);
}
void zb_steer(zb_t *s, int steer) { s->target_x = (int16_t)(clampi(steer, -256, 256) * ZB_X_MAX / 256); }
void zb_fire(zb_t *s, bool down)
{
    s->firing = down; /* it fires only in a fight, but a key held down from the last wave still counts */
}
bool zb_take_hit(zb_t *s)
{
    if (!s->hit_pending) return false;
    --s->hit_pending;
    return true;
}
static int half_units(const zb_t *s, const zb_z_t *z)
{
    (void)s;
    return zb_info(z->kind)->half_w * ZB_FOC * ZB_UNITS_PX / (z->z < ZB_ZN ? ZB_ZN : z->z) + 12;
}
bool zb_on_target(const zb_t *s, unsigned i)
{
    const zb_z_t *z = &s->z[i];
    return i < ZB_Z && z->alive && !z->dying_ms && iabs(z->x - s->cam_x) <= half_units(s, z);
}

static void spawn(zb_t *s)
{
    zb_z_t *slot = NULL;
    for (unsigned i = 0; i < ZB_Z && !slot; ++i) if (!s->z[i].alive) slot = &s->z[i];
    if (!slot) return;
    const unsigned w = s->wave;
    unsigned kind = ZK_WALKER;
    const int r = rr(s, 0, 99), brute_p = w >= 3U ? (4 + (int)w < 18 ? 4 + (int)w : 18) : 0,
              runner_p = w >= 2U ? (12 + 3 * (int)w < 40 ? 12 + 3 * (int)w : 40) : 0;
    if (r < brute_p) kind = ZK_BRUTE;
    else if (r < brute_p + runner_p) kind = ZK_RUNNER;
    int x0 = 0;
    for (unsigned tries = 0; tries < 5U; ++tries) { /* not on top of one that has just come out */
        x0 = rr(s, -ZB_LANE, ZB_LANE);
        bool crowded = false;
        for (unsigned i = 0; i < ZB_Z; ++i)
            if (s->z[i].alive && s->z[i].z > ZB_ZF - 500 && iabs(s->z[i].x0 - x0) < 80) crowded = true;
        if (!crowded) break;
    }
    *slot = (zb_z_t){.x = (int16_t)x0, .x0 = (int16_t)x0, .z = ZB_ZF, .alive = 1, .kind = (uint8_t)kind, .hp = zb_info(kind)->hp,
                     .seed = (uint8_t)rnd(s), .amp = (uint8_t)(kind == ZK_BRUTE ? rr(s, 10, 30) : kind == ZK_RUNNER ? rr(s, 50, 110) : rr(s, 25, 80))};
}
static void kill(zb_t *s, zb_z_t *z)
{
    const zb_info_t *info = zb_info(z->kind);
    z->dying_ms = ZB_DYING_MS;
    z->flash = 0;
    const unsigned streak = s->streak_steps ? s->streak : 0U;
    s->score += info->points + 10U * (streak < 10U ? streak : 10U);
    s->streak = (uint8_t)(streak < 250U ? streak + 1U : 250U);
    s->streak_steps = ZB_STREAK_STEPS;
    ++s->kills;
    s->kill_age_ms = 0;
    add_fx(s, z->x, z->z, ZX_SPLAT);
}
static void fire_shot(zb_t *s)
{
    if (!s->ammo || s->reload) return;
    s->cooldown = ZB_FIRE_STEPS;
    --s->ammo;
    ++s->shots_fired;
    s->shot_age_ms = 0;
    s->idle_steps = 0;
    if (!s->ammo) s->reload = ZB_RELOAD_STEPS;
    int best = -1;
    for (unsigned i = 0; i < ZB_Z; ++i)
        if (zb_on_target(s, i) && (best < 0 || s->z[i].z < s->z[best].z)) best = (int)i;
    if (best < 0) return;
    zb_z_t *z = &s->z[best];
    const unsigned dmg = z->z <= ZB_CLOSE_Z ? 2U : 1U;
    z->hp = (uint8_t)(z->hp > dmg ? z->hp - dmg : 0U);
    z->flash = 5;
    ++s->hits;
    s->hitmark_age_ms = 0;
    add_fx(s, z->x, z->z, ZX_BLOOD);
    if (!z->hp) kill(s, z);
    else z->z = (int16_t)(z->z + (z->kind == ZK_BRUTE ? 10 : 45)); /* a hit that does not kill pushes it back */
}
static void bite(zb_t *s, zb_z_t *z)
{
    if (!s->invuln) {
        --s->hp;
        s->invuln = ZB_INVULN_STEPS;
        s->hit_pending = (uint8_t)(s->hit_pending < 250U ? s->hit_pending + 1U : 250U);
        s->bite_age_ms = 0;
        add_fx(s, z->x, ZB_ZN, ZX_BITE);
        if (!s->hp) {
            s->phase = ZB_DYING;
            s->dying = ZB_LOST_STEPS;
            s->firing = 0;
            s->new_best = s->score > s->best;
            if (s->new_best) s->best = s->score;
        }
    }
    z->z = (int16_t)(ZB_ZN + (s->hp ? 420 : 100)); /* shoved off and stunned either way */
    z->stun = 35;
}
static void move_zombies(zb_t *s)
{
    for (unsigned i = 0; i < ZB_Z; ++i) {
        zb_z_t *z = &s->z[i];
        if (!z->alive) continue;
        if (z->flash) --z->flash;
        if (z->dying_ms) {
            z->dying_ms = (uint16_t)(z->dying_ms > ZB_STEP_MS ? z->dying_ms - ZB_STEP_MS : 0U);
            if (!z->dying_ms) z->alive = 0;
            continue;
        }
        if (s->phase == ZB_DYING) continue; /* everything stands and watches */
        ++z->age;
        const unsigned rate = z->kind == ZK_RUNNER ? 5U : z->kind == ZK_BRUTE ? 2U : 3U;
        z->x = (int16_t)(z->x0 + z->amp * isin(z->age * rate + z->seed) / 127);
        if (z->stun) { --z->stun; continue; }
        const int speed16 = zb_info(z->kind)->speed * 16 * speed_pct(s) / 100 + z->zf;
        z->zf = (uint8_t)(speed16 & 15);
        z->z = (int16_t)(z->z - (speed16 >> 4));
        if (z->z <= ZB_ZN) bite(s, z);
    }
}
static void step(zb_t *s)
{
    s->clock_ms += ZB_STEP_MS;
    s->shot_age_ms = sat_add(s->shot_age_ms, ZB_STEP_MS);
    s->hitmark_age_ms = sat_add(s->hitmark_age_ms, ZB_STEP_MS);
    s->bite_age_ms = sat_add(s->bite_age_ms, ZB_STEP_MS);
    s->kill_age_ms = sat_add(s->kill_age_ms, ZB_STEP_MS);
    s->over_age_ms = s->phase == ZB_OVER ? sat_add(s->over_age_ms, ZB_STEP_MS) : 0U;
    s->phase_ms = sat_add(s->phase_ms, ZB_STEP_MS);
    for (unsigned i = 0; i < ZB_FX; ++i) {
        zb_fx_t *f = &s->fx[i];
        if (f->alive && ++f->age > (f->kind == ZX_BITE ? 16U : f->kind == ZX_SPLAT ? 22U : 10U)) f->alive = 0;
    }
    if (s->phase == ZB_DYING && --s->dying == 0) { s->phase = ZB_OVER; s->over_age_ms = 0; }
    if (s->phase == ZB_TITLE || s->phase == ZB_OVER) return;
    s->cam_x = (int16_t)(s->cam_x + clampi(s->target_x - s->cam_x, -ZB_CAM_STEP, ZB_CAM_STEP));
    if (s->invuln) --s->invuln;
    if (s->cooldown) --s->cooldown;
    if (s->phase == ZB_INTRO) {
        if (s->phase_ms >= ZB_INTRO_MS) { s->phase = ZB_PLAY; s->phase_ms = 0; }
        return;
    }
    if (s->phase == ZB_PLAY) {
        ++s->play_steps;
        if (s->streak_steps && !--s->streak_steps) s->streak = 0;
        if (s->reload && !--s->reload) s->ammo = ZB_MAG;
        if (s->firing && !s->cooldown) fire_shot(s);
        if (!s->firing && !s->cooldown && !s->reload && s->ammo < ZB_MAG && ++s->idle_steps >= 30U) { /* the magazine fills while the gun is quiet */
            ++s->ammo;
            s->idle_steps = 19;
        }
        if (s->to_spawn) {
            if (s->spawn_wait) --s->spawn_wait;
            else if (zb_alive(s) < ZB_Z) {
                spawn(s);
                --s->to_spawn;
                s->spawn_wait = (uint16_t)(spawn_gap_steps(s) + rr(s, -8, 12));
            }
        }
    }
    move_zombies(s);
    if (s->phase == ZB_PLAY && !s->to_spawn && !zb_alive(s)) { /* the wave is cleared */
        s->wave_bonus = (int16_t)(s->wave * 50U);
        s->score += (uint32_t)s->wave_bonus;
        begin_wave(s);
    }
}
void zb_advance(zb_t *s, uint32_t dt_ms)
{
    if (dt_ms > ZB_MAX_ADVANCE_MS) dt_ms = ZB_MAX_ADVANCE_MS;
    s->carry_ms += dt_ms;
    while (s->carry_ms >= ZB_STEP_MS) {
        s->carry_ms -= ZB_STEP_MS;
        step(s);
    }
}
