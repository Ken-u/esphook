#include "basketball_game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void debounce_tests(void)
{
    basketball_button_t b = {0};
    assert(!basketball_button_sample(&b, false, 0));
    assert(!basketball_button_sample(&b, true, 10));
    assert(!basketball_button_sample(&b, false, 15));
    assert(!basketball_button_sample(&b, true, 18));
    assert(!basketball_button_sample(&b, true, 42));
    assert(basketball_button_sample(&b, true, 43));
    assert(!basketball_button_sample(&b, true, 500));
    assert(!basketball_button_sample(&b, false, 510));
    assert(!basketball_button_sample(&b, true, 515));
    assert(!basketball_button_sample(&b, false, 520));
    assert(!basketball_button_sample(&b, false, 545));
    assert(!basketball_button_sample(&b, true, 550));
    assert(basketball_button_sample(&b, true, 575));
    b = (basketball_button_t){0};
    assert(!basketball_button_sample(&b, true, 0)); /* Held at boot. */
    assert(!basketball_button_sample(&b, true, 1000));
    assert(!basketball_button_sample(&b, false, 1010));
    assert(!basketball_button_sample(&b, false, 1035));
    assert(!basketball_button_sample(&b, true, 1040));
    assert(basketball_button_sample(&b, true, 1065));
    unsigned repeats = 0;
    for (unsigned t = 1070; t < 121070; t += 5) repeats += basketball_button_sample(&b, true, t);
    assert(repeats == 0);
}
static void entry_tests(void)
{
    basketball_game_t g = {0};
    basketball_press(&g, 100, 1);
    basketball_press(&g, 450, 2);
    assert(g.phase == BASKETBALL_OFF);
    basketball_press(&g, 900, 3); /* Exactly 800ms total: third click is consumed. */
    assert(g.phase == BASKETBALL_PLAY && g.started_ms == 900 && g.shots == 0);
    basketball_press(&g, 1000, 4);
    basketball_press(&g, 1100, 5);
    basketball_press(&g, 1200, 6);
    assert(g.started_ms == 900 && g.shots == 1); /* Rapid clicks shoot, never restart. */
    basketball_stop(&g);
    basketball_press(&g, 2000, 1);
    basketball_press(&g, 2500, 1);
    basketball_press(&g, 2900, 1); /* Too slow: this becomes click one. */
    assert(g.phase == BASKETBALL_OFF && g.clicks == 1);
    basketball_press(&g, 3050, 1);
    basketball_press(&g, 3200, 1);
    assert(g.phase == BASKETBALL_PLAY && g.started_ms == 3200 && g.shots == 0);
}
static void shot_tests(void)
{
    basketball_game_t g;
    basketball_view_t view;
    basketball_start(&g, 10000, 123);
    unsigned old = g.speed_pps;
    uint64_t center = g.round_ms + (uint64_t)ceilf(64000.0f / g.speed_pps);
    assert(basketball_shoot(&g, center));
    assert(g.last_hit && g.shots == 1 && g.made == 1);
    assert(g.speed_pps != old && g.speed_pps >= 36 && g.speed_pps <= 100);
    float frozen = g.shot_x;
    basketball_snapshot(&g, center + 100, &view);
    assert(view.in_flight && view.hoop_x == frozen && view.score == 2);
    assert(view.ball_y < 57);
    assert(!basketball_shoot(&g, center + 649));
    assert(g.shots == 1);
    basketball_tick(&g, center + 650);
    basketball_snapshot(&g, center + 650, &view);
    assert(!view.in_flight && view.hoop_x == 16);
    old = g.speed_pps;
    assert(basketball_shoot(&g, center + 650)); /* A miss also changes the speed. */
    assert(!g.last_hit && g.shots == 2 && g.made == 1 && g.speed_pps != old);
    basketball_snapshot(&g, center + 1150, &view);
    assert(view.ball_x > 80 && view.score == 2);
    /* Exact judgment boundaries use the input timestamp, not the render cadence. */
    unsigned times[] = {539, 540, 640, 740, 741};
    bool hits[] = {false, true, true, true, false};
    for (unsigned i = 0; i < 5; ++i) {
        basketball_start(&g, 10000, 1);
        g.speed_pps = 100;
        assert(basketball_shoot(&g, 10000 + times[i]));
        assert(g.last_hit == hits[i]);
    }
    /* Left-to-right motion wraps rather than bouncing back to the left. */
    basketball_start(&g, 10000, 1);
    g.speed_pps = 100;
    basketball_snapshot(&g, 11279, &view);
    assert(view.hoop_x > 143);
    basketball_snapshot(&g, 11280, &view);
    assert(view.hoop_x == 16);
}
static void timing_tests(void)
{
    basketball_game_t g;
    basketball_view_t view;
    uint64_t start = UINT64_C(1) << 40;
    basketball_start(&g, start, 0);
    basketball_snapshot(&g, start, &view);
    assert(view.seconds_left == 120);
    basketball_snapshot(&g, start + 119001, &view);
    assert(view.seconds_left == 1);
    assert(basketball_shoot(&g, start + 119999));
    assert(g.shots == 1);
    basketball_tick(&g, start + 120000);
    basketball_snapshot(&g, start + 120000, &view);
    assert(view.phase == BASKETBALL_RESULT && view.seconds_left == 0 && view.shots == 1);
    assert(!basketball_shoot(&g, start + 120000));
    basketball_press(&g, start + 120749, 3);
    assert(g.phase == BASKETBALL_RESULT && g.shots == 1);
    basketball_press(&g, start + 120750, 3);
    assert(g.phase == BASKETBALL_OFF);
    basketball_press(&g, start + 120900, 3);
    basketball_press(&g, start + 121050, 3);
    assert(g.phase == BASKETBALL_PLAY && g.shots == 0 && g.made == 0);
    basketball_stop(&g);
    assert(g.phase == BASKETBALL_OFF && g.clicks == 0);
    basketball_start(&g, 0, 0);
    basketball_tick(&g, BASKETBALL_DURATION_MS);
    basketball_snapshot(&g, BASKETBALL_DURATION_MS, &view);
    assert(view.phase == BASKETBALL_RESULT && view.shots == 0 && view.score == 0);
    basketball_game_t a, b;
    basketball_start(&a, 0, 123); b = a;
    for (unsigned t = 0; t <= 1234; t += 17) basketball_tick(&a, t);
    basketball_tick(&b, 1234);
    basketball_view_t av, bv;
    basketball_snapshot(&a, 1234, &av);
    basketball_snapshot(&b, 1234, &bv);
    assert(av.hoop_x == bv.hoop_x);
}
static void full_game_tests(void)
{
    bool speeds[101] = {0};
    for (unsigned seed = 0; seed < 30; ++seed) {
        basketball_game_t g;
        basketball_start(&g, 0, seed);
        uint64_t now = 0;
        while (g.phase == BASKETBALL_PLAY) {
            unsigned old = g.speed_pps;
            speeds[old] = true;
            now = g.round_ms + (uint64_t)ceilf(64000.0f / old);
            if (now >= BASKETBALL_DURATION_MS) { basketball_tick(&g, now); break; }
            assert(basketball_shoot(&g, now));
            assert(g.last_hit && g.made == g.shots && g.speed_pps != old);
            now += BASKETBALL_SHOT_MS;
            basketball_tick(&g, now);
        }
        basketball_view_t v;
        basketball_snapshot(&g, now, &v);
        assert(v.phase == BASKETBALL_RESULT && v.score == v.made * 2 && v.made > 30);
        assert(v.shots < 186);
    }
    unsigned distinct = 0;
    for (unsigned speed = 36; speed <= 100; ++speed) distinct += speeds[speed];
    assert(distinct > 40);
}
int main(void)
{
    debounce_tests(); entry_tests(); shot_tests(); timing_tests(); full_game_tests();
    puts("basketball: debounce, triple-click, judgment, random pace, timer and scoring passed");
}
