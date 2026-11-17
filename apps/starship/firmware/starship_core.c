#include "starship_core.h"
#include <string.h>

static int iabs(int v) { return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t rnd(ss_t *s)
{
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 17;
    s->rng ^= s->rng << 5;
    return s->rng;
}
static int rr(ss_t *s, int lo, int hi) { return lo + (int)(rnd(s) % (uint32_t)(hi - lo + 1)); }
/* -127..127 over a 256 step period (a parabola: close enough for strafing). */
static int isin(unsigned phase)
{
    const int x = (int)(phase & 127U), v = x * (128 - x) * 127 / 4096;
    return (phase & 128U) ? -v : v;
}
static uint32_t sat_add(uint32_t v, uint32_t d) { return v + d > SS_AGE_MAX ? SS_AGE_MAX : v + d; }
/* 0..256: how far into the game, saturating after 150 seconds. */
static int difficulty(const ss_t *s)
{
    const uint32_t steps = s->play_steps > 7500U ? 7500U : s->play_steps;
    return (int)(steps * 256U / 7500U);
}
unsigned ss_seconds(const ss_t *s) { return (unsigned)(s->play_steps / 50U); }
unsigned ss_live_objects(const ss_t *s)
{
    unsigned n = 0;
    for (unsigned i = 0; i < SS_OBJ; ++i) n += s->obj[i].alive;
    return n;
}

static void add_fx(ss_t *s, int x, int z, unsigned kind, unsigned size)
{
    for (unsigned i = 0; i < SS_FX; ++i) {
        ss_fx_t *f = &s->fx[i];
        if (f->alive) continue;
        *f = (ss_fx_t){.x = (int16_t)x, .z = (int16_t)z, .alive = 1, .kind = (uint8_t)kind, .size = (uint8_t)size};
        return;
    }
}
static void damage(ss_t *s)
{
    if (s->invuln || s->phase != SS_PLAY) return;
    --s->hp;
    s->invuln = SS_INVULN_STEPS;
    s->hit_pending = (uint8_t)(s->hit_pending < 250U ? s->hit_pending + 1U : 250U);
    s->hit_age_ms = 0;
    add_fx(s, s->cam_x, SS_ZN + 20, SX_HIT, 20);
    if (!s->hp) {
        s->phase = SS_DYING;
        s->dying = SS_DYING_STEPS;
        s->firing = 0;
        if (s->score > s->best) s->best = s->score;
        add_fx(s, s->cam_x, SS_ZN + 60, SX_SHIP, 40);
    }
}
bool ss_take_hit(ss_t *s)
{
    if (!s->hit_pending) return false;
    --s->hit_pending;
    return true;
}

void ss_init(ss_t *s, uint32_t seed)
{
    memset(s, 0, sizeof *s);
    s->rng = seed ? seed : 0xC0FFEE11U;
    s->phase = SS_TITLE;
    s->hp = SS_HP;
    s->hit_age_ms = s->over_age_ms = SS_AGE_MAX;
}
void ss_start(ss_t *s)
{
    if (s->phase != SS_TITLE && s->phase != SS_OVER) return;
    const uint32_t best = s->best, seed = s->rng;
    ss_init(s, seed ^ 0x9E3779B9U);
    s->best = best; /* the best score survives a restart */
    s->phase = SS_PLAY;
    s->rock_wait = 70;
    s->enemy_wait = 320;
}
void ss_steer(ss_t *s, int steer) { s->target_x = (int16_t)(clampi(steer, -256, 256) * SS_X_MAX / 256); }
static void fire_shot(ss_t *s)
{
    for (unsigned i = 0; i < SS_SHOT; ++i) {
        ss_shot_t *b = &s->shot[i];
        if (b->alive) continue;
        s->gun ^= 1U;
        b->x = (int16_t)(s->cam_x + (s->gun ? 60 : -60));
        b->z = b->pz = SS_ZN + 30;
        b->alive = 1;
        ++s->shots_fired;
        s->cooldown = SS_FIRE_STEPS;
        return;
    }
}
void ss_fire(ss_t *s, bool down)
{
    if (s->phase != SS_PLAY) { s->firing = 0; return; }
    s->firing = down ? 1U : 0U;
    if (down && !s->cooldown) fire_shot(s);
}

/* ---- spawning ------------------------------------------------------------------------------------------------ */
static void spawn_rock(ss_t *s)
{
    for (unsigned i = 0; i < SS_OBJ; ++i) {
        ss_obj_t *o = &s->obj[i];
        if (o->alive) continue;
        const int d = difficulty(s);
        const bool large = rr(s, 0, 3) == 0;
        memset(o, 0, sizeof *o);
        o->alive = 1;
        o->kind = SO_ASTEROID;
        o->r = (int16_t)(large ? rr(s, 150, 190) : rr(s, 70, 125));
        o->hp = large ? 3U : 1U;
        o->x = (int16_t)(rr(s, 0, 99) < 55 ? clampi(s->cam_x + rr(s, -(o->r + 260), o->r + 260), -1250, 1250) : rr(s, -1150, 1150));
        o->z = o->pz = SS_ZF;
        o->vz = (int16_t)-(10 + d * 8 / 256 + rr(s, 0, 3));
        o->seed = (uint8_t)rnd(s);
        return;
    }
}
static void spawn_enemy(ss_t *s)
{
    for (unsigned i = 0; i < SS_OBJ; ++i) {
        ss_obj_t *o = &s->obj[i];
        if (o->alive) continue;
        memset(o, 0, sizeof *o);
        o->alive = 1;
        o->kind = SO_ENEMY;
        o->r = 95;
        o->hp = 2;
        o->x0 = (int16_t)rr(s, -600, 600);
        o->x = o->x0;
        o->amp = (int16_t)rr(s, 250, 520);
        o->z = o->pz = SS_ZF;
        o->vz = -8;
        o->fire = (uint16_t)rr(s, 40, 70);
        o->seed = (uint8_t)rr(s, 0, 255);
        o->state = SE_ENTER;
        return;
    }
}
static void fire_bolt(ss_t *s, int x, int z)
{
    for (unsigned i = 0; i < SS_BOLT; ++i) {
        ss_bolt_t *b = &s->bolt[i];
        if (b->alive) continue;
        *b = (ss_bolt_t){.x = (int16_t)clampi(x, -1300, 1300), .z = (int16_t)z, .vz = -34, .alive = 1};
        return;
    }
}
static void enemy_volley(ss_t *s, const ss_obj_t *o)
{
    const int d = difficulty(s);
    /* aimed where the ship is heading, not where it is */
    const int lead = clampi((s->target_x - s->cam_x) / 3, -300, 300), aim = s->cam_x + lead;
    fire_bolt(s, aim, o->z - 30);
    if (d > 100 && rr(s, 0, 99) < 50) { /* a fan of three */
        fire_bolt(s, aim - 230, o->z - 30);
        fire_bolt(s, aim + 230, o->z - 30);
    }
}

/* ---- one step ------------------------------------------------------------------------------------------------ */
static void destroy(ss_t *s, ss_obj_t *o, bool by_player)
{
    o->alive = 0;
    add_fx(s, o->x, o->z, SX_ROCK, (unsigned)(o->r / 4));
    if (!by_player) return;
    ++s->kills;
    s->score += o->kind == SO_ENEMY ? 100U : o->r > 140 ? 30U : 10U;
    if (s->score > s->best) s->best = s->score;
}
static void move_objects(ss_t *s)
{
    for (unsigned i = 0; i < SS_OBJ; ++i) {
        ss_obj_t *o = &s->obj[i];
        if (!o->alive) continue;
        o->pz = o->z;
        ++o->age;
        if (o->flash) --o->flash;
        if (o->kind == SO_ENEMY) {
            if (o->state == SE_ENTER) {
                o->z = (int16_t)(o->z + o->vz);
                if (o->z <= 820 - (int)(o->seed & 3U) * 60) o->state = SE_HOLD;
            } else if (o->state == SE_HOLD) {
                o->x = (int16_t)clampi(o->x0 + o->amp * isin(o->age * 2U + o->seed) / 127, -1000, 1000);
                if (o->fire) --o->fire;
                else {
                    enemy_volley(s, o);
                    const int d = difficulty(s);
                    o->fire = (uint16_t)(80 - d * 35 / 256 + rr(s, 0, 20));
                }
                if (o->age > SS_HOLD_STEPS) o->state = SE_DIVE;
            } else {
                o->vz = -18;
                o->z = (int16_t)(o->z + o->vz);
                o->x = (int16_t)(o->x + clampi(s->cam_x - o->x, -9, 9));
            }
        } else o->z = (int16_t)(o->z + o->vz);
        if (o->z <= SS_ZN) { /* it has reached the ship */
            if (iabs(o->x - s->cam_x) <= o->r + SS_SHIP_HALF) { damage(s); destroy(s, o, false); }
            else o->alive = 0; /* a near miss */
        }
    }
}
static void move_shots(ss_t *s)
{
    for (unsigned i = 0; i < SS_SHOT; ++i) {
        ss_shot_t *b = &s->shot[i];
        if (!b->alive) continue;
        b->pz = b->z;
        b->z = (int16_t)(b->z + SS_SHOT_STEP);
        if (b->z > SS_ZF + 100) { b->alive = 0; continue; }
        for (unsigned k = 0; k < SS_OBJ; ++k) {
            ss_obj_t *o = &s->obj[k];
            if (!o->alive || b->z < o->z || b->pz > o->pz) continue;
            if (iabs(b->x - o->x) > o->r + 20) continue;
            b->alive = 0;
            o->flash = 5;
            add_fx(s, b->x, o->z, SX_HIT, 6);
            if (--o->hp == 0) destroy(s, o, true);
            break;
        }
    }
}
static void move_bolts(ss_t *s)
{
    for (unsigned i = 0; i < SS_BOLT; ++i) {
        ss_bolt_t *b = &s->bolt[i];
        if (!b->alive) continue;
        b->z = (int16_t)(b->z + b->vz);
        if (b->z > SS_ZN) continue;
        if (iabs(b->x - s->cam_x) <= SS_BOLT_HALF) damage(s);
        b->alive = 0;
    }
}
static void step(ss_t *s)
{
    s->clock_ms += SS_STEP_MS;
    s->hit_age_ms = sat_add(s->hit_age_ms, SS_STEP_MS);
    s->over_age_ms = s->phase == SS_OVER ? sat_add(s->over_age_ms, SS_STEP_MS) : 0U;
    for (unsigned i = 0; i < SS_FX; ++i) {
        ss_fx_t *f = &s->fx[i];
        if (!f->alive) continue;
        if (++f->age > (f->kind == SX_SHIP ? 50U : 18U)) f->alive = 0;
    }
    if (s->phase == SS_DYING) {
        if (--s->dying == 0) { s->phase = SS_OVER; s->over_age_ms = 0; }
    }
    if (s->phase != SS_PLAY && s->phase != SS_DYING) return;
    /* the ship slides towards where it is told to be, within its speed */
    s->cam_x = (int16_t)(s->cam_x + clampi(s->target_x - s->cam_x, -SS_SHIP_STEP, SS_SHIP_STEP));
    if (s->phase == SS_PLAY) {
        ++s->play_steps;
        if (s->invuln) --s->invuln;
        if (s->cooldown) --s->cooldown;
        if (s->firing && !s->cooldown) fire_shot(s);
        const int d = difficulty(s);
        if (s->rock_wait) --s->rock_wait;
        else {
            spawn_rock(s);
            s->rock_wait = (uint16_t)(70 - d * 45 / 256 + rr(s, -10, 14));
        }
        unsigned enemies = 0;
        for (unsigned i = 0; i < SS_OBJ; ++i) enemies += s->obj[i].alive && s->obj[i].kind == SO_ENEMY;
        if (s->enemy_wait) --s->enemy_wait;
        else if (enemies < (d > 128 ? 2U : 1U)) {
            spawn_enemy(s);
            s->enemy_wait = (uint16_t)(420 - d * 220 / 256 + rr(s, 0, 80));
        }
    }
    move_objects(s);
    if (s->phase == SS_PLAY || s->phase == SS_DYING) {
        move_shots(s);
        move_bolts(s);
    }
}
void ss_advance(ss_t *s, uint32_t dt_ms)
{
    if (dt_ms > SS_MAX_ADVANCE_MS) dt_ms = SS_MAX_ADVANCE_MS;
    s->carry_ms += dt_ms;
    while (s->carry_ms >= SS_STEP_MS) {
        s->carry_ms -= SS_STEP_MS;
        step(s);
    }
}
