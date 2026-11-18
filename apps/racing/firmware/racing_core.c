#include "racing_core.h"
#include <string.h>
#include "esphook/app_math.h"
#include "esphook/app_rand.h"

int rc_difficulty(int32_t pos_mm)
{
    if (pos_mm <= 0) return 0;
    const int32_t d = pos_mm / (RC_DIFF_MM / 256);
    return d > 256 ? 256 : (int)d;
}
/* ---- the road ------------------------------------------------------------------------------------------------ */
#define NODE_MM 160000 /* the road eases from one bend to the next over this length */
static int node_value(const rc_t *r, uint32_t n)
{
    static const int8_t table[10] = {0, 0, 1, -1, 2, -2, 3, -3, 1, -1};
    const int amp = 60 + rc_difficulty((int32_t)(n * (uint32_t)NODE_MM + 400000U)) * 40 / 256;
    return table[app_hash32(r->track_seed + n * 0x9E3779B1U) % 10U] * amp;
}
int rc_curve_at(const rc_t *r, int32_t pos_mm)
{
    if (pos_mm < 400000) return 0; /* a straight start */
    const uint32_t q = (uint32_t)(pos_mm - 400000), n = q / (uint32_t)NODE_MM;
    const int t = (int)(q % (uint32_t)NODE_MM / 160U); /* 0..999 */
    const int s = t * t * (3000 - 2 * t) / 1000000;    /* smoothstep, 0..1000 */
    return ((n ? node_value(r, n) : 0) * (1000 - s) + node_value(r, n + 1U) * s) / 1000; /* the first bend grows from nothing */
}

unsigned rc_speed_kmh(const rc_t *r) { return (unsigned)(r->speed16 / 16); }
unsigned rc_distance_m(const rc_t *r) { return r->pos_mm > 0 ? (unsigned)(r->pos_mm / 1000) : 0U; }
unsigned rc_score(const rc_t *r) { return rc_distance_m(r) + (unsigned)r->overtakes * 25U; }
unsigned rc_time_s(const rc_t *r) { return (r->time_ms + 999U) / 1000U; }
int rc_px(const rc_t *r) { return r->px16 / 16; }

void rc_init(rc_t *r, uint32_t seed)
{
    memset(r, 0, sizeof *r);
    r->rng = seed ? seed : 0xBADC0DE5U;
    r->track_seed = app_hash32(r->rng);
    r->phase = RC_TITLE;
    r->time_ms = RC_START_TIME_MS;
    r->cp_age_ms = r->crash_age_ms = r->nitro_age_ms = r->over_age_ms = APP_AGE_MAX;
}
void rc_start(rc_t *r)
{
    if (r->phase != RC_TITLE && r->phase != RC_OVER) return;
    const uint32_t best = r->best, seed = r->rng;
    rc_init(r, seed ^ 0x2545F491U);
    r->best = best; /* the best score survives a restart */
    r->phase = RC_COUNT;
    r->next_cp_mm = RC_CHECKPOINT_MM;
    r->spawn_wait = 150;
}
void rc_steer(rc_t *r, int steer) { r->steer = app_clamp(steer, -256, 256); }
void rc_throttle(rc_t *r, bool down) { r->throttle = down ? 1U : 0U; }
bool rc_nitro(rc_t *r)
{
    if (r->phase != RC_RACE || r->nitro_ms > 0 || r->nitro < RC_NITRO_FULL) return false;
    r->nitro_ms = (int)RC_NITRO_BURN_MS;
    r->nitro_age_ms = 0;
    return true;
}
bool rc_take_crash(rc_t *r)
{
    if (!r->crash_pending) return false;
    --r->crash_pending;
    return true;
}

/* ---- traffic ------------------------------------------------------------------------------------------------- */
static unsigned cars_alive(const rc_t *r)
{
    unsigned n = 0;
    for (unsigned i = 0; i < RC_CARS; ++i) n += r->car[i].alive;
    return n;
}
static void spawn_car(rc_t *r)
{
    static const int lanes[3] = {-640, 0, 640};
    for (unsigned i = 0; i < RC_CARS; ++i) {
        if (r->car[i].alive) continue;
        const int d = rc_difficulty(r->pos_mm);
        const int px = lanes[app_rand_range(&r->rng, 0, 2)] + app_rand_range(&r->rng, -70, 70);
        const int32_t pos = r->pos_mm + app_rand_range(&r->rng, 380000, 520000);
        for (unsigned k = 0; k < RC_CARS; ++k) /* not on top of another car */
            if (r->car[k].alive && app_abs(r->car[k].px - px) < 700 && r->car[k].pos_mm - pos < 40000 && pos - r->car[k].pos_mm < 40000) return;
        r->car[i] = (rc_car_t){.pos_mm = pos, .px = (int16_t)px, .speed = (int16_t)(app_rand_range(&r->rng, 55, 105) + d * 40 / 256), .alive = 1,
                               .color = (uint8_t)app_rand_range(&r->rng, 0, 5)};
        return;
    }
}
static void crash(rc_t *r, int keep_kmh)
{
    r->speed16 = r->speed16 < keep_kmh * 16 ? r->speed16 : keep_kmh * 16;
    r->stun_ms = 600;
    r->nitro_ms = 0;
    r->nitro = r->nitro > 0 ? r->nitro / 2 : 0; /* a crash takes some of the tank with it */
    r->crash_age_ms = 0;
    ++r->crashes;
    r->crash_pending = (uint8_t)(r->crash_pending < 250U ? r->crash_pending + 1U : 250U);
}
static void move_cars(rc_t *r, bool racing)
{
    for (unsigned i = 0; i < RC_CARS; ++i) {
        rc_car_t *c = &r->car[i];
        if (!c->alive) continue;
        c->pos_mm += c->speed * 5556 / 1000;
        if (c->flash) --c->flash;
        const int32_t dpos = c->pos_mm - r->pos_mm;
        if (dpos < -40000 || dpos > 700000) { c->alive = 0; continue; }
        if (!c->passed && dpos < -3000) {
            c->passed = 1;
            ++r->overtakes;
        }
        if (racing && dpos >= 0 && dpos < RC_CAR_LONG_MM && app_abs(rc_px(r) - c->px) < RC_CAR_WIDE) {
            const int keep = c->speed - 15 > 30 ? c->speed - 15 : 30;
            crash(r, keep);
            c->pos_mm = r->pos_mm + RC_CAR_LONG_MM; /* the other car is knocked ahead */
            c->speed = (int16_t)(c->speed + 12);
            c->flash = 10;
        }
    }
}

/* ---- one step ------------------------------------------------------------------------------------------------ */
static void drive(rc_t *r, bool racing)
{
    const int top = (r->nitro_ms > 0 ? RC_NITRO_TOP_KMH : RC_TOP_KMH) * 16;
    int v = r->speed16;
    if (racing && r->throttle) {
        if (v < top) {
            int acc = (880 * (top - v) / top + 96) / 50;
            if (r->nitro_ms > 0) acc *= 2;
            v = v + acc > top ? top : v + acc;
        } else v -= 13; /* the nitro is over: back to the normal top speed */
    } else {
        v -= (128 + v * 4 / 100) / 50 + (v > top ? 13 : 0);
    }
    r->offroad = app_abs(rc_px(r)) > RC_ROAD_EDGE;
    if (r->offroad && v > 45 * 16) v -= 70 * 16 / 50 + 6; /* the verge drags the car down */
    r->speed16 = v < 0 ? 0 : v;

    r->pos_acc += r->speed16 * 89;
    r->pos_mm += r->pos_acc >> 8;
    r->pos_acc &= 255;

    const int kmh = r->speed16 / 16, eff = r->stun_ms > 0 ? r->steer / 3 : r->steer;
    const int grip = kmh < 140 ? kmh : 140;
    const int push = -(rc_curve_at(r, r->pos_mm) * kmh * kmh) * 16 / 1000000; /* the bend throws the car outwards */
    r->px16 += eff * grip * 192 / 25600 + push;
    if (r->stun_ms > 0) r->stun_ms -= (int)RC_STEP_MS;
    if (app_abs(r->px16) > RC_WALL * 16) { /* the barrier */
        r->px16 = r->px16 > 0 ? RC_ROAD_EDGE * 16 : -RC_ROAD_EDGE * 16; /* thrown back onto the edge of the road */
        crash(r, kmh / 2 > 25 ? kmh / 2 : 25);
        r->stun_ms = 500;
    }
}
static void step(rc_t *r)
{
    r->clock_ms += RC_STEP_MS;
    r->cp_age_ms = app_age_add(r->cp_age_ms, RC_STEP_MS);
    r->crash_age_ms = app_age_add(r->crash_age_ms, RC_STEP_MS);
    r->nitro_age_ms = app_age_add(r->nitro_age_ms, RC_STEP_MS);
    if (r->phase == RC_OVER) { r->over_age_ms = app_age_add(r->over_age_ms, RC_STEP_MS); return; }
    if (r->phase == RC_COUNT) {
        r->count_ms += RC_STEP_MS;
        if (r->count_ms >= RC_COUNT_MS) r->phase = RC_RACE;
        return;
    }
    if (r->phase != RC_RACE && r->phase != RC_FINISH) return;
    const bool racing = r->phase == RC_RACE;
    drive(r, racing);
    move_cars(r, racing);
    if (!racing) {
        r->finish_ms += RC_STEP_MS;
        if (r->finish_ms >= RC_FINISH_MS) {
            r->phase = RC_OVER;
            r->over_age_ms = 0;
            if (rc_score(r) > r->best) r->best = rc_score(r); /* the coast to a stop counts too */
        }
        return;
    }
    if (r->nitro_ms > 0) {
        r->nitro_ms -= (int)RC_STEP_MS;
        if (r->nitro_ms <= 0) { r->nitro_ms = 0; r->nitro = 0; }
        else r->nitro = r->nitro_ms * RC_NITRO_FULL / (int)RC_NITRO_BURN_MS;
    } else if (r->nitro < RC_NITRO_FULL) {
        r->nitro = r->nitro + (int)(RC_NITRO_FULL * RC_STEP_MS / RC_NITRO_FILL_MS) > RC_NITRO_FULL
                       ? RC_NITRO_FULL : r->nitro + (int)(RC_NITRO_FULL * RC_STEP_MS / RC_NITRO_FILL_MS);
    }
    if (r->pos_mm >= r->next_cp_mm) {
        r->time_ms += RC_CHECKPOINT_BONUS_MS;
        r->next_cp_mm += RC_CHECKPOINT_MM;
        r->cp_age_ms = 0;
        ++r->checkpoints;
    }
    if (r->time_ms > RC_STEP_MS) r->time_ms -= RC_STEP_MS;
    else {
        r->time_ms = 0;
        r->phase = RC_FINISH;
        r->finish_ms = 0;
        if (rc_score(r) > r->best) r->best = rc_score(r);
    }
    const int d = rc_difficulty(r->pos_mm);
    if (r->spawn_wait) --r->spawn_wait;
    else {
        if (cars_alive(r) < 3U + (unsigned)(d * 3 / 256)) spawn_car(r);
        r->spawn_wait = (uint16_t)(50 - d * 25 / 256 + app_rand_range(&r->rng, 0, 40));
    }
}
void rc_advance(rc_t *r, uint32_t dt_ms)
{
    for (unsigned n = app_step_take(&r->carry_ms, dt_ms, RC_STEP_MS, RC_MAX_ADVANCE_MS); n; --n) step(r);
}
