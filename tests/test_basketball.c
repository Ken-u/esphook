#include "app_input.h"
#include "basketball_game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void debounce_tests(void)
{
    app_button_t b = {0};
    assert(!app_button_sample(&b, false, 0));
    assert(!app_button_sample(&b, true, 10));
    assert(!app_button_sample(&b, false, 15));
    assert(!app_button_sample(&b, true, 18));
    assert(!app_button_sample(&b, true, 42));
    assert(app_button_sample(&b, true, 43));
    assert(!app_button_sample(&b, true, 500));
    assert(!app_button_sample(&b, false, 510));
    assert(!app_button_sample(&b, true, 515));
    assert(!app_button_sample(&b, false, 520));
    assert(!app_button_sample(&b, false, 545));
    assert(!app_button_sample(&b, true, 550));
    assert(app_button_sample(&b, true, 575));
    b = (app_button_t){0};
    assert(!app_button_sample(&b, true, 0)); /* Held at boot. */
    assert(!app_button_sample(&b, true, 1000));
    assert(!app_button_sample(&b, false, 1010));
    assert(!app_button_sample(&b, false, 1035));
    assert(!app_button_sample(&b, true, 1040));
    assert(app_button_sample(&b, true, 1065));
    unsigned repeats = 0;
    for (unsigned t = 1070; t < 121070; t += 5) repeats += app_button_sample(&b, true, t);
    assert(repeats == 0);
}
static void interrupt_debounce_tests(void)
{
    app_button_t b = {0};
    assert(!app_button_sample(&b, false, 0));
    app_button_edge(&b, true, 10);
    assert(!app_button_sample(&b, true, 10));
    /* A bounce away and back can be coalesced: same sampled level, new edge. */
    app_button_edge(&b, true, 30);
    assert(!app_button_sample(&b, true, 54));
    assert(app_button_sample(&b, true, 55));
    app_button_edge(&b, true, 60);
    assert(!app_button_sample(&b, true, 85)); /* No held-key repeat. */
    app_button_edge(&b, false, 100);
    assert(!app_button_sample(&b, false, 125));
    app_button_edge(&b, true, 130);
    assert(app_button_sample(&b, true, 155));
    b = (app_button_t){0};
    app_button_edge(&b, true, 1000); /* IRQ-mode startup held low. */
    assert(!app_button_sample(&b, true, 1025));
    app_button_edge(&b, false, 1100);
    assert(!app_button_sample(&b, false, 1125));
    app_button_edge(&b, true, 1200);
    assert(app_button_sample(&b, true, 1225));
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
    assert(g.phase == BASKETBALL_RESULT && g.shots == 1);
    basketball_stop(&g);
    assert(g.phase == BASKETBALL_OFF);
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
    debounce_tests(); interrupt_debounce_tests(); shot_tests(); timing_tests(); full_game_tests();
    puts("basketball: debounce, judgment, random pace, timer and scoring passed");
}
