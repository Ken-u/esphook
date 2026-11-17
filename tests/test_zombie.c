/* Host tests for the zombie game (apps/zombie/firmware/zombie_core.c): aim, ammo, waves, bites and a bot that plays. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zombie_core.h"

static void run(zb_t *s, uint32_t ms) { while (ms) { const uint32_t d = ms > 20U ? 20U : ms; zb_advance(s, d); ms -= d; } }
static void begin(zb_t *s, uint32_t seed)
{
    zb_init(s, seed);
    zb_start(s);
    assert(s->phase == ZB_INTRO);
    run(s, ZB_INTRO_MS + 40U);
    assert(s->phase == ZB_PLAY);
}
/* A quiet fight with one zombie of our choosing in it. */
static void pose(zb_t *s, unsigned kind, int x, int z)
{
    memset(s->z, 0, sizeof s->z);
    s->z[0] = (zb_z_t){.alive = 1, .kind = (uint8_t)kind, .x = (int16_t)x, .x0 = (int16_t)x, .z = (int16_t)z, .hp = zb_info(kind)->hp};
    s->to_spawn = 5;
    s->spawn_wait = 60000U;
    s->cooldown = 0;
    s->ammo = ZB_MAG;
}
static void shoot(zb_t *s) { while (s->cooldown) zb_advance(s, ZB_STEP_MS); zb_fire(s, true); zb_advance(s, ZB_STEP_MS); zb_fire(s, false); }

static void test_info(void)
{
    assert(zb_info(ZK_WALKER)->hp == 2 && zb_info(ZK_RUNNER)->hp == 1 && zb_info(ZK_BRUTE)->hp == 6);
    assert(zb_info(ZK_RUNNER)->speed > zb_info(ZK_WALKER)->speed && zb_info(ZK_WALKER)->speed > zb_info(ZK_BRUTE)->speed);
    assert(zb_info(ZK_BRUTE)->points > zb_info(ZK_RUNNER)->points && zb_info(ZK_RUNNER)->points > zb_info(ZK_WALKER)->points);
    assert(zb_info(99)->hp == 2); /* an unknown kind is a walker */
}

static void test_flow(void)
{
    zb_t s;
    zb_init(&s, 7);
    zb_fire(&s, true);
    run(&s, 2000);
    assert(s.phase == ZB_TITLE && s.shots_fired == 0 && zb_alive(&s) == 0); /* nothing happens on the title screen */
    zb_fire(&s, false);
    zb_start(&s);
    assert(s.phase == ZB_INTRO && s.wave == 1 && s.wave_total == 7 && s.to_spawn == 7 && s.hp == ZB_HP);
    zb_fire(&s, true);
    run(&s, 1000);
    assert(s.shots_fired == 0 && s.phase == ZB_INTRO); /* nor between waves */
    zb_fire(&s, false);
    zb_start(&s);
    assert(s.wave == 1); /* a start in the middle of a game is nothing */
    run(&s, ZB_INTRO_MS);
    assert(s.phase == ZB_PLAY);
    const uint32_t clock = s.clock_ms;
    zb_advance(&s, 5000); /* a long gap is a pause, not a backlog */
    assert(s.clock_ms == clock + ZB_MAX_ADVANCE_MS);
    /* the same seed and the same inputs make the same fight */
    zb_t a, b;
    begin(&a, 99); begin(&b, 99);
    for (unsigned i = 0; i < 300; ++i) { zb_steer(&a, (int)(i % 40) * 12 - 240); zb_steer(&b, (int)(i % 40) * 12 - 240); zb_fire(&a, i % 7 < 3); zb_fire(&b, i % 7 < 3); run(&a, 40); run(&b, 40); }
    assert(!memcmp(&a, &b, sizeof a));
    begin(&b, 100);
    run(&b, 12000);
    run(&a, 12000);
    assert(memcmp(a.z, b.z, sizeof a.z));
}

static void test_view(void)
{
    zb_t s;
    begin(&s, 3);
    zb_steer(&s, 256);
    assert(s.target_x == ZB_X_MAX);
    for (unsigned i = 0; i < 10; ++i) zb_advance(&s, ZB_STEP_MS);
    assert(s.cam_x == 10 * ZB_CAM_STEP);
    run(&s, 1000);
    assert(s.cam_x == ZB_X_MAX);
    zb_steer(&s, -9999);
    assert(s.target_x == -ZB_X_MAX);
    run(&s, 2000);
    assert(s.cam_x == -ZB_X_MAX);
    zb_steer(&s, 0);
    run(&s, 1000);
    assert(s.cam_x == 0);
}

static void test_aim_and_damage(void)
{
    zb_t s;
    begin(&s, 5);
    /* the crosshair is the middle of the screen: the width of what is there depends on how close it is */
    pose(&s, ZK_WALKER, 0, 800);
    assert(zb_on_target(&s, 0));
    s.z[0].x = 20; assert(zb_on_target(&s, 0));
    s.z[0].x = 60; assert(!zb_on_target(&s, 0));
    s.z[0].z = 200; s.z[0].x = 50; assert(zb_on_target(&s, 0)); /* closer, wider */
    s.z[0].x = 120; assert(!zb_on_target(&s, 0));
    s.cam_x = 120; assert(zb_on_target(&s, 0)); /* turning the view moves the crosshair */
    assert(!zb_on_target(&s, ZB_Z) && !zb_on_target(&s, 3)); /* nothing there */
    s.cam_x = 0;
    /* a walker takes two shots from afar; a point-blank one kills at once */
    pose(&s, ZK_WALKER, 0, 800);
    shoot(&s);
    assert(s.z[0].hp == 1 && s.hits == 1 && s.shots_fired == 1 && s.hitmark_age_ms < 100U && s.z[0].z > 800);
    assert(s.score == 0);
    shoot(&s);
    assert(s.z[0].hp == 0 && s.z[0].dying_ms > 0 && s.kills == 1 && s.score == 100 && s.streak == 1);
    run(&s, ZB_DYING_MS + 40U);
    assert(!s.z[0].alive);
    pose(&s, ZK_WALKER, 0, 300);
    shoot(&s);
    assert(s.z[0].dying_ms > 0 && s.kills == 2); /* close in a shot is twice as hard */
    /* a dying one cannot be shot again, and the one behind it is next */
    pose(&s, ZK_WALKER, 0, 300);
    s.z[1] = (zb_z_t){.alive = 1, .kind = ZK_RUNNER, .x = 0, .x0 = 0, .z = 900, .hp = 1};
    shoot(&s);
    assert(s.z[0].dying_ms > 0 && s.z[1].hp == 1);
    shoot(&s);
    assert(s.z[1].hp == 0 && s.kills == 4);
    /* the nearest takes the shot */
    pose(&s, ZK_WALKER, 0, 900);
    s.z[1] = (zb_z_t){.alive = 1, .kind = ZK_WALKER, .x = 10, .x0 = 10, .z = 500, .hp = 2};
    shoot(&s);
    assert(s.z[1].hp == 1 && s.z[0].hp == 2);
    /* a shot at nothing is a shot spent */
    pose(&s, ZK_WALKER, 600, 900);
    const unsigned fired = s.shots_fired, hits = s.hits;
    shoot(&s);
    assert(s.shots_fired == fired + 1 && s.hits == hits && s.z[0].hp == 2 && s.ammo == ZB_MAG - 1);
    /* a brute takes a pounding */
    pose(&s, ZK_BRUTE, 0, 1000);
    for (unsigned i = 0; i < 5; ++i) { shoot(&s); pose(&s, ZK_BRUTE, 0, 1000); s.z[0].hp = (uint8_t)(6 - (i + 1)); }
    pose(&s, ZK_BRUTE, 0, 1000);
    unsigned shots = 0;
    while (s.z[0].hp && shots < 20U) { shoot(&s); ++shots; }
    assert(shots == 6 && s.z[0].dying_ms > 0);
    pose(&s, ZK_BRUTE, 0, 300);
    shots = 0;
    while (s.z[0].hp && shots < 20U) { shoot(&s); ++shots; s.z[0].z = 300; } /* held where it was: close shots count double */
    assert(shots == 3);
    /* a hit pushes a zombie back, a brute hardly */
    pose(&s, ZK_WALKER, 0, 700);
    shoot(&s);
    const int walker_back = s.z[0].z;
    pose(&s, ZK_BRUTE, 0, 700);
    shoot(&s);
    assert(walker_back > 700 + 30 && s.z[0].z < 700 + 30);
}

static void test_ammo(void)
{
    zb_t s;
    begin(&s, 11);
    pose(&s, ZK_WALKER, 900, 1400); /* far off to the side: nothing to hit */
    zb_fire(&s, true);
    zb_advance(&s, ZB_STEP_MS);
    assert(s.shots_fired == 1 && s.ammo == ZB_MAG - 1); /* the first shot is at once */
    run(&s, 220);
    assert(s.shots_fired == 2);
    run(&s, 2 * 220U);
    assert(s.shots_fired == 4);
    while (s.shots_fired < ZB_MAG) run(&s, 20);
    assert(s.ammo == 0 && s.reload > 0);
    const unsigned at = s.shots_fired;
    run(&s, 1000);
    assert(s.shots_fired == at); /* reloading: held down, nothing comes out */
    run(&s, 400);
    assert(s.shots_fired == at + 1 && s.ammo == ZB_MAG - 1); /* and it is firing again by itself */
    zb_fire(&s, false);
    /* a quiet gun fills its magazine, slowly */
    run(&s, 400);
    assert(s.ammo == ZB_MAG - 1);
    run(&s, 400);
    assert(s.ammo == ZB_MAG);
    for (unsigned i = 0; i < 3; ++i) shoot(&s);
    assert(s.ammo == ZB_MAG - 3);
    run(&s, 600);
    assert(s.ammo == ZB_MAG - 3); /* it waits for a moment of quiet first */
    run(&s, 300);
    assert(s.ammo == ZB_MAG - 2);
    run(&s, 450);
    assert(s.ammo == ZB_MAG);
    /* a tap is one shot, however long the press */
    const unsigned before = s.shots_fired;
    zb_fire(&s, true); zb_advance(&s, 20); zb_fire(&s, false);
    run(&s, 500);
    assert(s.shots_fired == before + 1);
}

static void test_bites(void)
{
    zb_t s;
    begin(&s, 13);
    /* a walker comes in a straight line at the player, and it takes about five seconds */
    pose(&s, ZK_WALKER, 0, ZB_ZF);
    unsigned t = 0;
    while (s.hp == ZB_HP && t < 20000U) { run(&s, 20); t += 20; }
    assert(s.hp == ZB_HP - 1 && t > 4500U && t < 6500U);
    assert(zb_take_hit(&s) && !zb_take_hit(&s));
    assert(s.bite_age_ms < 100U && s.z[0].z > ZB_ZN + 300 && s.z[0].stun > 0); /* shoved back and stunned */
    assert(s.invuln > 40);
    /* one that comes back while the bite is fresh does no more harm */
    s.z[0].z = ZB_ZN + 5; s.z[0].stun = 0;
    run(&s, 100);
    assert(s.hp == ZB_HP - 1 && s.z[0].z > ZB_ZN + 300);
    /* runners are quicker, brutes slower */
    zb_t r, b;
    begin(&r, 14); begin(&b, 14);
    pose(&r, ZK_RUNNER, 0, ZB_ZF); pose(&b, ZK_BRUTE, 0, ZB_ZF);
    unsigned tr = 0, tb = 0;
    while (r.hp == ZB_HP && tr < 30000U) { run(&r, 20); tr += 20; }
    while (b.hp == ZB_HP && tb < 60000U) { run(&b, 20); tb += 20; }
    assert(tr > 2000U && tr < 3400U && tb > 8500U && tb < 12500U && tr * 3 < tb);
    /* with nothing done, the zombies get the player: three lives, one at a time */
    zb_t d;
    begin(&d, 15);
    unsigned lost = 0, frames = 0;
    uint8_t hp = d.hp;
    while (d.phase == ZB_PLAY && frames < 3000U) {
        run(&d, 100); ++frames;
        while (zb_take_hit(&d)) ++lost;
        assert(d.hp <= hp);
        hp = d.hp;
    }
    assert(d.phase == ZB_DYING && d.hp == 0 && lost == 3);
    const zb_z_t frozen[ZB_Z] = {d.z[0], d.z[1], d.z[2], d.z[3], d.z[4], d.z[5], d.z[6], d.z[7], d.z[8], d.z[9], d.z[10], d.z[11]};
    run(&d, 600);
    for (unsigned i = 0; i < ZB_Z; ++i) assert(d.z[i].alive == frozen[i].alive && (!d.z[i].alive || (d.z[i].z == frozen[i].z && d.z[i].x == frozen[i].x)));
    assert(d.phase == ZB_DYING);
    run(&d, ZB_LOST_STEPS * ZB_STEP_MS);
    assert(d.phase == ZB_OVER && d.over_age_ms < 1000U && d.kills == 0 && d.score == 0 && d.best == 0 && !d.new_best);
    zb_fire(&d, true);
    run(&d, 500);
    assert(d.shots_fired == 0);
    zb_fire(&d, false);
    zb_start(&d);
    assert(d.phase == ZB_INTRO && d.wave == 1 && d.hp == ZB_HP && d.score == 0);
}

/* A bot that turns to the nearest zombie and fires when the crosshair is on it. */
static void bot(zb_t *s, int *last_target)
{
    int best = -1;
    for (unsigned i = 0; i < ZB_Z; ++i)
        if (s->z[i].alive && !s->z[i].dying_ms && (best < 0 || s->z[i].z < s->z[best].z)) best = (int)i;
    if (best < 0) { zb_steer(s, 0); zb_fire(s, false); return; }
    const int aim = s->z[best].x;
    *last_target = aim;
    zb_steer(s, aim * 256 / ZB_X_MAX);
    zb_fire(s, zb_on_target(s, (unsigned)best));
}
static void test_waves(void)
{
    zb_t s;
    begin(&s, 21);
    unsigned kinds[ZK_KINDS] = {0};
    int bearing = 0;
    for (unsigned t = 0; t < 600U * 50U && s.wave == 1U; ++t) {
        for (unsigned i = 0; i < ZB_Z; ++i) if (s.z[i].alive && s.z[i].z > ZB_ZF - 12) { ++kinds[s.z[i].kind]; assert(abs(s.z[i].x0) <= ZB_LANE); }
        bot(&s, &bearing);
        zb_advance(&s, ZB_STEP_MS);
    }
    assert(s.wave == 2 && s.phase == ZB_INTRO && s.kills == 7 && s.wave_total == 9 && s.to_spawn == 9);
    assert(kinds[ZK_RUNNER] == 0 && kinds[ZK_BRUTE] == 0); /* the first wave is walkers only */
    assert(s.score >= 700 + 50 && s.wave_bonus == 50);
    assert(s.hp == ZB_HP); /* a perfect bot lost nothing, and a life lost comes back */
    zb_t b;
    begin(&b, 22);
    b.hp = 1; b.wave = 4;
    b.to_spawn = 0;
    memset(b.z, 0, sizeof b.z);
    run(&b, 100);
    assert(b.wave == 5 && b.hp == 2 && b.phase == ZB_INTRO && b.wave_bonus == 200 && b.score == 200);
    /* the mix of kinds: walkers early, runners from the second wave, brutes from the third */
    for (unsigned w = 1; w <= 6; ++w) {
        unsigned count[ZK_KINDS] = {0};
        for (unsigned seed = 1; seed <= 60; ++seed) {
            zb_t g;
            zb_init(&g, seed * 7919U);
            g.wave = (uint8_t)w; g.phase = ZB_PLAY; g.to_spawn = 5; g.spawn_wait = 0; g.ammo = ZB_MAG;
            for (unsigned n = 0; n < 5; ++n) {
                zb_advance(&g, ZB_STEP_MS);
                for (unsigned i = 0; i < ZB_Z; ++i) if (g.z[i].alive) { ++count[g.z[i].kind]; memset(&g.z[i], 0, sizeof g.z[i]); }
                g.spawn_wait = 0;
            }
        }
        if (w == 1) assert(count[ZK_RUNNER] == 0 && count[ZK_BRUTE] == 0 && count[ZK_WALKER] > 200);
        if (w == 2) assert(count[ZK_RUNNER] > 40 && count[ZK_BRUTE] == 0);
        if (w >= 3) assert(count[ZK_RUNNER] > 40 && count[ZK_BRUTE] > 8 && count[ZK_WALKER] > 60);
    }
    /* later waves are bigger, quicker and come closer together */
    zb_t early, late;
    zb_init(&early, 1); zb_init(&late, 1);
    early.wave = 1; late.wave = 10;
    assert(late.wave > early.wave);
    zb_start(&early);
    late.phase = ZB_OVER;
    late.wave = 9;
    zb_start(&late); /* a restart is wave one again */
    assert(late.wave == 1);
    zb_t w9;
    zb_init(&w9, 5); w9.wave = 8; w9.phase = ZB_OVER; zb_start(&w9);
    assert(w9.wave == 1);
}
static void test_scoring(void)
{
    zb_t s;
    begin(&s, 31);
    /* kills in a quick run are worth a little more each time, and the run ends after three quiet seconds */
    const unsigned expect[] = {100, 110, 120, 130};
    unsigned total = 0;
    for (unsigned i = 0; i < 4; ++i) {
        pose(&s, ZK_WALKER, 0, 300);
        shoot(&s);
        total += expect[i];
        assert(s.score == total && s.streak == i + 1);
        run(&s, 500);
    }
    run(&s, 3200);
    assert(s.streak == 0);
    pose(&s, ZK_RUNNER, 0, 300);
    shoot(&s);
    assert(s.score == total + 150);
    pose(&s, ZK_BRUTE, 0, 300);
    for (unsigned i = 0; i < 3; ++i) { shoot(&s); s.z[0].z = 300; }
    assert(s.score == total + 150 + 500 + 10);
    /* the best is kept over a restart, and a game that beats it says so */
    zb_t d;
    begin(&d, 32);
    d.score = 480; d.hp = 1; d.invuln = 0;
    pose(&d, ZK_WALKER, 0, ZB_ZN + 3);
    run(&d, 100);
    assert(d.phase == ZB_DYING && d.best == 480 && d.new_best);
    run(&d, 2000);
    assert(d.phase == ZB_OVER);
    run(&d, 1000);
    zb_start(&d);
    assert(d.best == 480 && d.score == 0 && !d.new_best);
    run(&d, ZB_INTRO_MS + 40U);
    assert(d.phase == ZB_PLAY);
    d.hp = 1; d.score = 100; d.invuln = 0;
    pose(&d, ZK_WALKER, 0, ZB_ZN + 3);
    run(&d, 100);
    run(&d, 2000);
    assert(d.phase == ZB_OVER && d.best == 480 && !d.new_best);
}
static void test_bot(void)
{
    /* a bot with perfect aim holds out for a good while, and does not for ever */
    unsigned best_wave = 0, longest = 0, dead = 0, worst_wave = 1000;
    for (unsigned seed = 1; seed <= 12; ++seed) {
        zb_t s;
        begin(&s, seed * 2654435761U);
        int bearing = 0;
        unsigned t = 0;
        for (; t < 20U * 60U * 50U && s.phase != ZB_OVER; ++t) {
            bot(&s, &bearing);
            zb_advance(&s, ZB_STEP_MS);
            assert(zb_alive(&s) <= ZB_Z && s.ammo <= ZB_MAG && s.hp <= ZB_HP);
            for (unsigned i = 0; i < ZB_Z; ++i) assert(!s.z[i].alive || (s.z[i].z >= ZB_ZN && s.z[i].z <= ZB_ZF + 500));
        }
        if (s.phase == ZB_OVER) ++dead;
        if (s.wave > best_wave) best_wave = s.wave;
        if (s.wave < worst_wave) worst_wave = s.wave;
        if (t > longest) longest = t;
        assert(s.kills > 20 && s.wave >= 3 && s.shots_fired >= s.hits);
    }
    printf("zombie bot: waves %u..%u, longest %u s, %u of 12 died\n", worst_wave, best_wave, longest / 50U, dead);
    assert(best_wave >= 6 && dead >= 1);
}

int main(void)
{
    test_info();
    test_flow();
    test_view();
    test_aim_and_damage();
    test_ammo();
    test_bites();
    test_waves();
    test_scoring();
    test_bot();
    puts("zombie core: aim, damage, ammo, bites and flow passed");
    return 0;
}
