/* Host tests for the racing game (apps/racing/firmware/racing_core.c). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "racing_core.h"

static int absi(int v) { return v < 0 ? -v : v; }
static void run(rc_t *r, uint32_t ms)
{
    while (ms) {
        uint32_t d = ms > 100 ? 100 : ms;
        rc_advance(r, d);
        ms -= d;
    }
}
static rc_t *racing(rc_t *r, uint32_t seed)
{
    memset(r, 0, sizeof *r);
    rc_init(r, seed);
    rc_start(r);
    run(r, RC_COUNT_MS + 20);
    assert(r->phase == RC_RACE);
    r->spawn_wait = 60000; /* no traffic unless a test puts it there */
    return r;
}
static void put_car(rc_t *r, unsigned i, int32_t ahead_mm, int px, int speed)
{
    r->car[i] = (rc_car_t){.pos_mm = r->pos_mm + ahead_mm, .px = (int16_t)px, .speed = (int16_t)speed, .alive = 1};
}

static void test_phases(void)
{
    rc_t r;
    memset(&r, 0, sizeof r);
    rc_init(&r, 1);
    assert(r.phase == RC_TITLE && rc_time_s(&r) == 45);
    rc_throttle(&r, true);
    run(&r, 3000);
    assert(r.pos_mm == 0 && r.phase == RC_TITLE); /* nothing moves on the title */
    rc_start(&r);
    assert(r.phase == RC_COUNT && !r.throttle);
    rc_throttle(&r, true); /* flooring it during the countdown is allowed, it just does nothing yet */
    run(&r, 2000);
    assert(r.phase == RC_COUNT && r.speed16 == 0 && r.pos_mm == 0 && rc_time_s(&r) == 45);
    run(&r, 900);
    assert(r.phase == RC_RACE);
    run(&r, 1000);
    assert(r.speed16 > 0 && r.pos_mm > 0 && rc_time_s(&r) <= 45 && rc_time_s(&r) >= 43);
    const uint32_t seed = r.rng;
    rc_start(&r);
    assert(r.rng == seed && r.phase == RC_RACE); /* starting twice does nothing */
}

static void test_driving(void)
{
    rc_t r;
    racing(&r, 2);
    rc_throttle(&r, true);
    run(&r, 1000);
    assert(rc_speed_kmh(&r) >= 45 && rc_speed_kmh(&r) <= 75); /* a good launch */
    run(&r, 6500);
    assert(rc_speed_kmh(&r) >= 185 && rc_speed_kmh(&r) <= 200);
    run(&r, 5000);
    assert(rc_speed_kmh(&r) == 200); /* the top speed is a cap */
    rc_throttle(&r, false);
    run(&r, 5000);
    assert(rc_speed_kmh(&r) >= 90 && rc_speed_kmh(&r) <= 165); /* it rolls, it does not stop dead */
    run(&r, 30000);
    assert(rc_speed_kmh(&r) < 12);
    /* distance follows speed */
    racing(&r, 3);
    r.speed16 = 180 * 16;
    rc_throttle(&r, false);
    r.nitro_ms = 0;
    const int32_t before = r.pos_mm;
    for (unsigned i = 0; i < 250; ++i) { r.speed16 = 180 * 16; rc_advance(&r, 20); } /* 5 s at 180 km/h: 250 m */
    assert(r.pos_mm - before > 249000 && r.pos_mm - before < 251000);
}

static void test_steering(void)
{
    rc_t r;
    racing(&r, 4);
    r.speed16 = 100 * 16;
    rc_steer(&r, 256);
    for (unsigned i = 0; i < 25; ++i) { r.speed16 = 100 * 16; rc_advance(&r, 20); } /* half a second on the straight */
    assert(rc_px(&r) > 250 && rc_px(&r) < 350);
    rc_steer(&r, 0);
    const int held = rc_px(&r);
    for (unsigned i = 0; i < 100; ++i) { r.speed16 = 100 * 16; rc_advance(&r, 20); }
    assert(rc_px(&r) == held); /* no steering, no drift on a straight road */
    rc_steer(&r, -9999);
    assert(r.steer == -256);
    racing(&r, 4);
    rc_steer(&r, 256);
    run(&r, 1000);
    assert(rc_px(&r) == 0); /* a stationary car does not steer */
    /* the verge slows the car, the barrier stops it */
    racing(&r, 5);
    rc_throttle(&r, true);
    run(&r, 6000);
    rc_steer(&r, 256);
    while (rc_px(&r) <= RC_ROAD_EDGE + 20) run(&r, 20);
    assert(r.offroad);
    const unsigned fast = rc_speed_kmh(&r);
    for (unsigned i = 0; i < 25; ++i) rc_advance(&r, 20);
    assert(rc_speed_kmh(&r) < fast);
    unsigned guard = 0;
    while (!r.crash_pending && guard++ < 2000) rc_advance(&r, 20); /* still steering right */
    assert(r.crash_pending == 1 && rc_take_crash(&r) && !rc_take_crash(&r) && absi(rc_px(&r)) <= 1150);
    assert(r.stun_ms > 0 && rc_speed_kmh(&r) <= 100 && r.crashes == 1);
}

static void test_road(void)
{
    rc_t a, b;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    rc_init(&a, 7); rc_init(&b, 8);
    int max = 0, diff = 0, last = 0, signs[2] = {0, 0};
    for (int32_t p = 0; p < 20000000; p += 1000) {
        const int c = rc_curve_at(&a, p);
        if (p < 400000) assert(c == 0);
        assert(absi(c) <= 300);
        if (p) assert(absi(c - last) <= 8); /* eased in and out: never a kink */
        last = c;
        if (absi(c) > max) max = absi(c);
        diff += c != rc_curve_at(&b, p);
        if (c) signs[c > 0] += 1;
    }
    assert(max >= 240 && diff > 5000 && signs[0] > 1000 && signs[1] > 1000); /* real bends, both ways, different per seed */
    assert(rc_curve_at(&a, 5000000) == rc_curve_at(&a, 5000000));
    assert(rc_difficulty(0) == 0 && rc_difficulty(RC_DIFF_MM / 2) >= 126 && rc_difficulty(RC_DIFF_MM * 3) == 256);
    /* a bend throws the car to its outside */
    rc_t r;
    racing(&r, 7);
    int32_t at = 400000;
    while (rc_curve_at(&r, at) < 220) at += 1000;
    r.pos_mm = at;
    rc_throttle(&r, false);
    for (unsigned i = 0; i < 50; ++i) { r.speed16 = 200 * 16; rc_advance(&r, 20); }
    assert(rc_px(&r) < -120); /* right-hand bend: pushed to the left */
    racing(&r, 7);
    at = 400000;
    while (rc_curve_at(&r, at) > -220) at += 1000;
    r.pos_mm = at;
    for (unsigned i = 0; i < 50; ++i) { r.speed16 = 200 * 16; rc_advance(&r, 20); }
    assert(rc_px(&r) > 120);
    racing(&r, 7);
    at = 400000;
    while (rc_curve_at(&r, at) < 220) at += 1000;
    r.pos_mm = at;
    for (unsigned i = 0; i < 50; ++i) { r.speed16 = 60 * 16; rc_advance(&r, 20); }
    assert(absi(rc_px(&r)) < 30); /* slowly, the same bend is nothing */
}

static void test_traffic(void)
{
    rc_t r;
    racing(&r, 9);
    r.speed16 = 100 * 16;
    rc_throttle(&r, true);
    put_car(&r, 0, 5200, 0, 70);
    run(&r, 100);
    assert(r.crash_pending == 1 && r.stun_ms > 0 && r.car[0].flash > 0 && r.car[0].pos_mm >= r.pos_mm + RC_CAR_LONG_MM);
    assert(rc_speed_kmh(&r) <= 62 && rc_speed_kmh(&r) >= 30); /* it ends up behind the car it hit, slower than it */
    run(&r, 400);
    assert(r.crash_pending == 1 && r.crashes == 1); /* one bump, not a pile-up */
    /* a car in the next lane is passed without a scratch, and counts as an overtake */
    racing(&r, 9);
    r.speed16 = 120 * 16;
    rc_throttle(&r, true);
    put_car(&r, 0, 30000, 640, 70);
    put_car(&r, 1, 60000, -640, 80);
    for (unsigned i = 0; i < 600 && r.overtakes < 2; ++i) { r.speed16 = r.speed16 < 120 * 16 ? 120 * 16 : r.speed16; rc_advance(&r, 20); }
    assert(r.overtakes == 2 && r.crashes == 0);
    run(&r, 3000);
    assert(!r.car[0].alive && !r.car[1].alive); /* left behind and gone */
    assert(rc_score(&r) == rc_distance_m(&r) + 50);
    /* traffic appears by itself, ahead, in lanes, never overlapping */
    racing(&r, 10);
    r.spawn_wait = 1;
    rc_throttle(&r, false);
    r.speed16 = 0;
    run(&r, 20000);
    unsigned n = 0;
    for (unsigned i = 0; i < RC_CARS; ++i) {
        if (!r.car[i].alive) continue;
        ++n;
        assert(absi(r.car[i].px) <= 720 && r.car[i].speed >= 55);
        for (unsigned k = i + 1; k < RC_CARS; ++k)
            if (r.car[k].alive) assert(absi(r.car[i].px - r.car[k].px) >= 400 || absi(r.car[i].pos_mm - r.car[k].pos_mm) >= 3000);
    }
    assert(n >= 1 && n <= RC_CARS);
}

/* A driver: steers back to the middle (or into a free lane), cancels the pull of the bend and lifts when it is tight. */
static void bot(rc_t *r, bool smart, bool auto_nitro)
{
    const int px = rc_px(r), kmh = (int)rc_speed_kmh(r);
    int target = 0;
    if (smart)
        for (unsigned i = 0; i < RC_CARS; ++i) {
            const rc_car_t *c = &r->car[i];
            const int32_t d = c->pos_mm - r->pos_mm;
            if (c->alive && d > 0 && d < 20000 + kmh * 400 && absi(px - c->px) < 700) target = c->px >= 0 ? c->px - 760 : c->px + 760;
        }
    int steer = (target - px) * 256 / 250;
    const int grip = kmh < 140 ? kmh : 140;
    if (smart && grip > 0) steer += rc_curve_at(r, r->pos_mm) * kmh * kmh / (grip * 703);
    rc_steer(r, steer > 256 ? 256 : steer < -256 ? -256 : steer);
    rc_throttle(r, !smart || (absi(px) < 850 && (r->stun_ms <= 0 || kmh < 60)));
    if (smart && auto_nitro && r->nitro == RC_NITRO_FULL && absi(rc_curve_at(r, r->pos_mm + 200000)) < 100) rc_nitro(r);
}
static void drive(rc_t *r, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 20) { bot(r, true, false); rc_advance(r, 20); }
}
static void test_nitro(void)
{
    rc_t r;
    racing(&r, 11);
    assert(!rc_nitro(&r));
    rc_throttle(&r, true);
    drive(&r, 9000);
    assert(!rc_nitro(&r) && r.nitro < RC_NITRO_FULL && r.nitro > 800); /* it fills on a timer: 10 seconds */
    drive(&r, 1200);
    assert(r.nitro == RC_NITRO_FULL);
    assert(rc_speed_kmh(&r) >= 195 && rc_speed_kmh(&r) <= 200);
    assert(rc_nitro(&r) && !rc_nitro(&r) && r.nitro_ms == 3000); /* one burst at a time */
    drive(&r, 2000);
    assert(rc_speed_kmh(&r) > 240 && r.nitro < 500);
    drive(&r, 1200);
    assert(r.nitro_ms == 0 && r.nitro < 60 && rc_speed_kmh(&r) <= 290 && rc_speed_kmh(&r) > 230);
    assert(!rc_nitro(&r)); /* empty: it has to fill up again */
    drive(&r, 12000);
    assert(rc_speed_kmh(&r) >= 195 && rc_speed_kmh(&r) <= 200 && r.nitro == RC_NITRO_FULL); /* back to the normal top speed, tank full again */
    /* a crash ends the burst */
    assert(rc_nitro(&r));
    drive(&r, 500);
    r.speed16 = 220 * 16;
    put_car(&r, 0, 4000, rc_px(&r), 60);
    drive(&r, 100);
    assert(r.nitro_ms == 0 && r.crashes == 1);
}

static void test_play(void)
{
    /* a careful driver is rewarded: checkpoints add time, the run goes on far past the first legs */
    unsigned long careful = 0, hands_off = 0;
    for (uint32_t seed = 1; seed <= 10; ++seed) {
        rc_t r;
        memset(&r, 0, sizeof r);
        rc_init(&r, seed * 17);
        rc_start(&r);
        uint32_t cps = 0;
        for (unsigned t = 0; t < 30000 && r.phase != RC_OVER; ++t) {
            bot(&r, true, true);
            const uint32_t time_before = r.time_ms;
            rc_advance(&r, 20);
            if (r.checkpoints != cps) { cps = r.checkpoints; assert(r.time_ms + RC_STEP_MS >= time_before + RC_CHECKPOINT_BONUS_MS - 1); }
            assert(absi(rc_px(&r)) <= RC_WALL);
        }
        assert(r.phase == RC_OVER && r.best == rc_score(&r) && r.checkpoints >= 5);
        careful += rc_distance_m(&r);
    }
    /* flat out with hands off the wheel gets a fraction of that: the bends and the traffic need a driver */
    for (uint32_t seed = 1; seed <= 10; ++seed) {
        rc_t r;
        memset(&r, 0, sizeof r);
        rc_init(&r, seed * 29);
        rc_start(&r);
        for (unsigned t = 0; t < 30000 && r.phase != RC_OVER; ++t) { rc_steer(&r, 0); rc_throttle(&r, true); rc_advance(&r, 20); }
        assert(r.phase == RC_OVER && rc_distance_m(&r) < 5000);
        hands_off += rc_distance_m(&r);
    }
    assert(hands_off * 4 < careful);
    /* standing still: the clock runs out, a quiet end, a score of zero */
    rc_t r;
    racing(&r, 12);
    run(&r, 44000);
    assert(r.phase == RC_RACE);
    run(&r, 1500);
    assert(r.phase == RC_FINISH && r.time_ms == 0);
    run(&r, RC_FINISH_MS + 100);
    assert(r.phase == RC_OVER && rc_score(&r) == 0);
    rc_throttle(&r, true);
    run(&r, 2000);
    assert(r.speed16 == 0 && r.pos_mm == 0);
    rc_start(&r);
    assert(r.phase == RC_COUNT && r.time_ms == RC_START_TIME_MS);
}

static void test_determinism_and_bounds(void)
{
    rc_t a, b;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    rc_init(&a, 5); rc_init(&b, 5);
    rc_start(&a); rc_start(&b);
    for (unsigned t = 0; t < 4000; ++t) {
        const int steer = (int)((t * 37U) % 513U) - 256;
        rc_steer(&a, steer); rc_steer(&b, steer);
        rc_throttle(&a, (t / 200) & 1U); rc_throttle(&b, (t / 200) & 1U);
        if (t % 400 == 399) { rc_nitro(&a); rc_nitro(&b); }
        rc_advance(&a, 20);
        rc_advance(&b, 10); rc_advance(&b, 10); /* the same time in other chunks */
    }
    assert(a.clock_ms == b.clock_ms && a.pos_mm == b.pos_mm && a.speed16 == b.speed16 && a.px16 == b.px16 && a.time_ms == b.time_ms);
    rc_t e;
    memset(&e, 0, sizeof e);
    rc_init(&e, 1);
    rc_start(&e);
    rc_advance(&e, 3600000U);
    assert(e.clock_ms == RC_MAX_ADVANCE_MS);
    for (uint32_t seed = 1; seed <= 12; ++seed) { /* soak with wild input */
        rc_t s;
        memset(&s, 0, sizeof s);
        rc_init(&s, seed * 1013);
        for (unsigned round = 0; round < 4; ++round) {
            rc_start(&s);
            for (unsigned t = 0; t < 6000 && s.phase != RC_OVER; ++t) {
                if (t % 9 == 0) rc_steer(&s, (int)((t * seed * 31U) % 700U) - 350);
                if (t % 31 == 0) rc_throttle(&s, (t / 31) % 3 != 0);
                if (t % 211 == 0) rc_nitro(&s);
                rc_advance(&s, 20);
                assert(absi(rc_px(&s)) <= RC_WALL && s.speed16 >= 0 && s.speed16 <= RC_NITRO_TOP_KMH * 16 + 16);
                assert(s.nitro >= 0 && s.nitro <= RC_NITRO_FULL && s.pos_mm >= 0 && s.time_ms <= RC_START_TIME_MS + 30U * RC_CHECKPOINT_BONUS_MS);
                for (unsigned i = 0; i < RC_CARS; ++i)
                    if (s.car[i].alive) assert(s.car[i].pos_mm - s.pos_mm > -40001 && s.car[i].pos_mm - s.pos_mm < 700001);
            }
            if (s.phase != RC_OVER) s.phase = RC_OVER;
        }
    }
}

int main(void)
{
    test_phases();
    test_driving();
    test_steering();
    test_road();
    test_traffic();
    test_nitro();
    test_play();
    test_determinism_and_bounds();
    puts("racing tests passed");
    return 0;
}
