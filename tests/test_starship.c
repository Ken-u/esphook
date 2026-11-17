/* Host tests for the starship game (apps/starship/firmware/starship_core.c). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "starship_core.h"

static void run(ss_t *s, uint32_t ms)
{
    while (ms) {
        uint32_t d = ms > 100 ? 100 : ms;
        ss_advance(s, d);
        ms -= d;
    }
}
static int absi(int v) { return v < 0 ? -v : v; }
static void clear(ss_t *s)
{
    memset(s->obj, 0, sizeof s->obj);
    memset(s->bolt, 0, sizeof s->bolt);
    memset(s->shot, 0, sizeof s->shot);
    s->rock_wait = s->enemy_wait = 60000; /* nothing new arrives */
}
static ss_t *fresh(ss_t *s, uint32_t seed)
{
    memset(s, 0, sizeof *s);
    ss_init(s, seed);
    ss_start(s);
    clear(s);
    return s;
}
static unsigned shots_alive(const ss_t *s)
{
    unsigned n = 0;
    for (unsigned i = 0; i < SS_SHOT; ++i) n += s->shot[i].alive;
    return n;
}
static void put_rock(ss_t *s, int x, int z, int r, int hp)
{
    ss_obj_t *o = &s->obj[0];
    memset(o, 0, sizeof *o);
    o->alive = 1; o->kind = SO_ASTEROID; o->x = (int16_t)x; o->z = o->pz = (int16_t)z; o->r = (int16_t)r; o->hp = (uint8_t)hp;
    o->vz = 0;
}

static void test_title_and_start(void)
{
    ss_t s;
    memset(&s, 0, sizeof s);
    ss_init(&s, 1);
    assert(s.phase == SS_TITLE && s.hp == SS_HP);
    ss_fire(&s, true);
    run(&s, 5000);
    assert(ss_live_objects(&s) == 0 && s.shots_fired == 0 && s.play_steps == 0); /* nothing happens on the title */
    ss_start(&s);
    assert(s.phase == SS_PLAY && s.hp == SS_HP && s.score == 0);
    const uint32_t seed = s.rng;
    ss_start(&s); /* starting twice does nothing */
    assert(s.rng == seed);
    run(&s, 4000);
    assert(ss_live_objects(&s) >= 1); /* the field fills up */
}

static void test_steering(void)
{
    ss_t s;
    fresh(&s, 2);
    ss_steer(&s, 256);
    assert(s.target_x == SS_X_MAX);
    ss_advance(&s, SS_STEP_MS);
    assert(s.cam_x == SS_SHIP_STEP); /* it slides, it does not jump */
    run(&s, 600);
    assert(s.cam_x == SS_X_MAX);
    ss_steer(&s, 9999);
    assert(s.target_x == SS_X_MAX);
    ss_steer(&s, -256);
    int last = s.cam_x;
    for (unsigned i = 0; i < 60; ++i) {
        ss_advance(&s, SS_STEP_MS);
        assert(absi(s.cam_x - last) <= SS_SHIP_STEP && s.cam_x >= -SS_X_MAX);
        last = s.cam_x;
    }
    assert(s.cam_x == -SS_X_MAX);
    ss_steer(&s, 0);
    run(&s, 1000);
    assert(s.cam_x == 0);
}

static void test_firing(void)
{
    ss_t s;
    fresh(&s, 3);
    ss_fire(&s, true);
    assert(s.shots_fired == 1 && shots_alive(&s) == 1); /* a tap is one shot, at once */
    ss_fire(&s, false);
    run(&s, 1000);
    assert(s.shots_fired == 1);
    ss_fire(&s, true);
    ss_fire(&s, false);
    ss_fire(&s, true); /* mashing inside the cooldown does not double the rate */
    ss_fire(&s, false);
    assert(s.shots_fired == 2);
    run(&s, 1000);
    ss_fire(&s, true);
    const uint32_t before = s.shots_fired;
    run(&s, 1000);
    assert(s.shots_fired - before >= 5 && s.shots_fired - before <= 7); /* held: one every 180 ms */
    ss_fire(&s, false);
    const uint32_t after = s.shots_fired;
    run(&s, 1000);
    assert(s.shots_fired == after);
    /* shots start at the two guns, alternating, and run away from the ship */
    clear(&s);
    s.cam_x = s.target_x = 300;
    ss_fire(&s, true);
    ss_fire(&s, false);
    run(&s, 200);
    ss_fire(&s, true);
    ss_fire(&s, false);
    int xs[2], n = 0;
    for (unsigned i = 0; i < SS_SHOT; ++i) if (s.shot[i].alive && n < 2) xs[n++] = s.shot[i].x;
    assert(n == 2 && xs[0] != xs[1] && absi(xs[0] - 300) == 60 && absi(xs[1] - 300) == 60);
}

static void test_hits(void)
{
    ss_t s;
    fresh(&s, 4);
    put_rock(&s, 0, 900, 100, 1);
    ss_fire(&s, true); ss_fire(&s, false);
    run(&s, 400);
    assert(!s.obj[0].alive && s.score == 10 && s.kills == 1);
    bool boom = false;
    for (unsigned i = 0; i < SS_FX; ++i) boom |= s.fx[i].alive && s.fx[i].kind == SX_ROCK;
    assert(boom);
    /* off to the side: the shot flies past */
    put_rock(&s, 400, 900, 100, 1);
    ss_fire(&s, true); ss_fire(&s, false);
    run(&s, 800);
    assert(s.obj[0].alive && s.kills == 1);
    /* the edge of a rock is still a hit (radius + shot width) */
    put_rock(&s, 60 + 100 + 20, 900, 100, 1);
    s.cam_x = s.target_x = 0;
    ss_fire(&s, true); ss_fire(&s, false);
    run(&s, 400);
    assert(!s.obj[0].alive && s.kills == 2);
    /* a big rock takes three, an enemy two */
    fresh(&s, 5);
    put_rock(&s, 0, 1200, 170, 3);
    ss_fire(&s, true);
    run(&s, 1800);
    ss_fire(&s, false);
    assert(!s.obj[0].alive && s.score == 30 && s.kills == 1);
    memset(s.shot, 0, sizeof s.shot); /* shots still flying would hit it early */
    put_rock(&s, 0, 1200, 95, 2);
    s.obj[0].kind = SO_ENEMY; s.obj[0].state = SE_HOLD; s.obj[0].x0 = 0; s.obj[0].amp = 0; s.obj[0].fire = 60000;
    s.obj[0].age = 0;
    s.cooldown = 0;
    ss_fire(&s, true);
    run(&s, 440);
    assert(s.obj[0].alive && s.obj[0].hp == 1 && s.obj[0].flash > 0);
    run(&s, 400);
    ss_fire(&s, false);
    assert(!s.obj[0].alive && s.score == 130);
    /* a shot that leaves the field is gone */
    clear(&s);
    ss_fire(&s, true); ss_fire(&s, false);
    run(&s, 2000);
    assert(shots_alive(&s) == 0);
}

static void test_damage(void)
{
    ss_t s;
    fresh(&s, 6);
    s.bolt[0] = (ss_bolt_t){.x = 0, .z = 400, .vz = -34, .alive = 1};
    run(&s, 400);
    assert(s.hp == SS_HP - 1 && s.invuln > 0 && ss_take_hit(&s) && !ss_take_hit(&s) && s.hit_age_ms < 400);
    /* the shield flickers for a moment after a hit: a second hit inside it does not count */
    s.bolt[0] = (ss_bolt_t){.x = 0, .z = 150, .vz = -34, .alive = 1};
    run(&s, 200);
    assert(s.hp == SS_HP - 1 && !ss_take_hit(&s));
    run(&s, 1500);
    /* a bolt that misses */
    s.bolt[0] = (ss_bolt_t){.x = SS_BOLT_HALF + 20, .z = 400, .vz = -34, .alive = 1};
    run(&s, 400);
    assert(s.hp == SS_HP - 1 && !s.bolt[0].alive);
    /* a bolt just inside the hit zone */
    s.bolt[0] = (ss_bolt_t){.x = SS_BOLT_HALF - 2, .z = 400, .vz = -34, .alive = 1};
    run(&s, 400);
    assert(s.hp == SS_HP - 2 && ss_take_hit(&s));
    run(&s, 1500);
    /* a rock straight ahead is a collision, a rock beside the ship is a near miss */
    fresh(&s, 7);
    put_rock(&s, 50, 400, 100, 1);
    s.obj[0].vz = -10;
    run(&s, 800);
    assert(s.hp == SS_HP - 1 && !s.obj[0].alive && s.kills == 0);
    run(&s, 1500);
    fresh(&s, 8);
    put_rock(&s, 100 + SS_SHIP_HALF + 40, 400, 100, 1);
    s.obj[0].vz = -10;
    run(&s, 800);
    assert(s.hp == SS_HP && !s.obj[0].alive);
    /* dodging works: the ship slides out of the way of a bolt */
    fresh(&s, 9);
    s.bolt[0] = (ss_bolt_t){.x = 0, .z = 700, .vz = -34, .alive = 1};
    ss_steer(&s, 128);
    run(&s, 600);
    assert(s.hp == SS_HP);
}

static void test_game_over(void)
{
    ss_t s;
    fresh(&s, 10);
    s.score = 70;
    for (unsigned i = 0; i < SS_HP; ++i) {
        s.bolt[0] = (ss_bolt_t){.x = s.cam_x, .z = 200, .vz = -34, .alive = 1};
        run(&s, 200);
        run(&s, 1300);
        if (i + 1 < SS_HP) { assert(s.phase == SS_PLAY && s.hp == SS_HP - 1 - i); }
    }
    assert(s.hp == 0 && s.phase != SS_PLAY && s.best == 70);
    unsigned hits = 0;
    while (ss_take_hit(&s)) ++hits;
    assert(hits == SS_HP);
    ss_fire(&s, true);
    const uint32_t shots = s.shots_fired;
    run(&s, 3000);
    assert(s.phase == SS_OVER && s.shots_fired == shots); /* no shooting once it is over */
    assert(s.over_age_ms > 1000);
    ss_start(&s);
    assert(s.phase == SS_PLAY && s.hp == SS_HP && s.score == 0 && s.kills == 0 && s.best == 70 && s.cam_x == 0);
}

static void test_difficulty(void)
{
    ss_t s;
    fresh(&s, 11);
    s.rock_wait = 1;
    s.enemy_wait = 60000;
    run(&s, 100);
    assert(s.rock_wait >= 60 && s.rock_wait <= 84); /* a rock every 1.2-1.7 s at the start */
    s.play_steps = 150 * 50;
    s.rock_wait = 1;
    s.hp = 3;
    s.invuln = 60000;
    run(&s, 100);
    assert(s.rock_wait >= 15 && s.rock_wait <= 39); /* and every 0.3-0.8 s after two and a half minutes */
    /* without moving or shooting, the ship does not last */
    unsigned died = 0, total = 0;
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        memset(&s, 0, sizeof s);
        ss_init(&s, seed * 77);
        ss_start(&s);
        for (unsigned t = 0; t < 4000 && s.phase == SS_PLAY; ++t) ss_advance(&s, 100);
        died += s.phase != SS_PLAY;
        ++total;
    }
    assert(died >= total - 1);
    /* shooting from the middle gets further than not shooting */
    uint32_t idle = 0, shooter = 0;
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        memset(&s, 0, sizeof s);
        ss_init(&s, seed * 31);
        ss_start(&s);
        for (unsigned t = 0; t < 4000 && s.phase == SS_PLAY; ++t) ss_advance(&s, 100);
        idle += s.play_steps;
        memset(&s, 0, sizeof s);
        ss_init(&s, seed * 31);
        ss_start(&s);
        ss_fire(&s, true);
        for (unsigned t = 0; t < 4000 && s.phase == SS_PLAY; ++t) ss_advance(&s, 100);
        shooter += s.play_steps;
        assert(s.kills >= 5);
    }
    assert(shooter > idle);
}

static void test_determinism_and_bounds(void)
{
    ss_t a, b;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    ss_init(&a, 5); ss_init(&b, 5);
    ss_start(&a); ss_start(&b);
    for (unsigned t = 0; t < 1500; ++t) {
        const int steer = (int)((t * 37U) % 513U) - 256;
        ss_steer(&a, steer); ss_steer(&b, steer);
        if (t % 40 == 3) { ss_fire(&a, true); ss_fire(&b, true); }
        if (t % 40 == 20) { ss_fire(&a, false); ss_fire(&b, false); }
        ss_advance(&a, 20);
        ss_advance(&b, 10); ss_advance(&b, 10); /* the same time in other chunks */
    }
    assert(a.clock_ms == b.clock_ms && a.score == b.score && a.cam_x == b.cam_x && a.hp == b.hp && a.kills == b.kills);
    ss_t e;
    memset(&e, 0, sizeof e);
    ss_init(&e, 1);
    ss_start(&e);
    ss_advance(&e, 3600000U);
    assert(e.clock_ms == SS_MAX_ADVANCE_MS);
    /* soak: wild input never breaks the invariants */
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        ss_t s;
        memset(&s, 0, sizeof s);
        ss_init(&s, seed * 1013);
        for (unsigned round = 0; round < 6; ++round) {
            ss_start(&s);
            for (unsigned t = 0; t < 3000; ++t) {
                if (t % 7 == 0) ss_steer(&s, (int)((t * seed * 31U) % 700U) - 350);
                if (t % 13 == 0) ss_fire(&s, (t / 13) & 1U);
                ss_advance(&s, 20);
                assert(s.cam_x >= -SS_X_MAX && s.cam_x <= SS_X_MAX && s.hp <= SS_HP);
                for (unsigned i = 0; i < SS_OBJ; ++i)
                    if (s.obj[i].alive) assert(s.obj[i].z > SS_ZN && s.obj[i].z <= SS_ZF && absi(s.obj[i].x) <= 1300);
                for (unsigned i = 0; i < SS_BOLT; ++i)
                    if (s.bolt[i].alive) assert(s.bolt[i].z > SS_ZN && s.bolt[i].z <= SS_ZF && absi(s.bolt[i].x) <= 1300);
                for (unsigned i = 0; i < SS_SHOT; ++i)
                    if (s.shot[i].alive) assert(s.shot[i].z >= SS_ZN && s.shot[i].z <= SS_ZF + 100);
                if (s.phase == SS_OVER) break;
            }
            for (unsigned t = 0; t < 100 && s.phase == SS_DYING; ++t) ss_advance(&s, 100);
            if (s.phase == SS_PLAY) s.phase = SS_OVER; /* still alive after a minute: end it */
        }
    }
}

int main(void)
{
    test_title_and_start();
    test_steering();
    test_firing();
    test_hits();
    test_damage();
    test_game_over();
    test_difficulty();
    test_determinism_and_bounds();
    puts("starship tests passed");
    return 0;
}
