#include "app_input.h"
#include "basketball_game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
/* A hoop that crosses the field at a constant speed and never redraws: easy to compute by hand. */
static void fixed_path(basketball_game_t *g, uint64_t at, float x, float v)
{
    g->path = (basketball_path_t){.leg_ms = at, .redraw_ms = UINT64_MAX / 2, .x0 = x, .v = v, .random = 7};
}
static bool near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}
static void bounce_tests(void)
{
    basketball_game_t g;
    basketball_view_t view;
    basketball_start(&g, 10000, 1);
    fixed_path(&g, 10000, 16, 100);
    /* Reaching the right end it turns back to the left instead of starting over. */
    assert(near(basketball_hoop_at(&g, 10000), 16));
    assert(near(basketball_hoop_at(&g, 11279), 143.9f));
    assert(near(basketball_hoop_at(&g, 11280), 144));
    assert(near(basketball_hoop_at(&g, 11380), 134));
    assert(near(basketball_hoop_at(&g, 12000), 72));
    assert(near(basketball_hoop_at(&g, 12560), 16));
    assert(near(basketball_hoop_at(&g, 12660), 26)); /* and bounces off the left end too */
    for (uint64_t t = 10000; t <= 14000; t += 33) basketball_tick(&g, t);
    basketball_snapshot(&g, 14000, &view);
    /* 4000 ms = right end at 1280, left end at 2560, right end at 3840, then 160 ms back to the left. */
    assert(near(view.hoop_x, 128) && view.speed_pps == 100);
    basketball_start(&g, 0, 1);
    fixed_path(&g, 0, 144, -100);
    assert(near(basketball_hoop_at(&g, 500), 94));
}
static void shot_tests(void)
{
    basketball_game_t g;
    basketball_view_t view;
    /* The shot is judged where the hoop is when the ball reaches the rim, 450 ms after release.
     * Hoop at 16 px moving 100 px/s is in the 70..90 window from 540 to 740 ms. */
    unsigned offsets[] = {89, 90, 290, 291, 640};
    bool hits[] = {false, true, true, false, false};
    for (unsigned i = 0; i < 5; ++i) {
        basketball_start(&g, 10000, 1);
        fixed_path(&g, 10000, 16, 100);
        assert(basketball_shoot(&g, 10000 + offsets[i]));
        assert(g.last_hit == hits[i]);
    }
    /* Releasing at 640 ms, when the hoop is centred right now, misses: it has moved on by 450 ms. */
    assert(!hits[4] && near(basketball_hoop_at(&g, 10640), 80));

    basketball_start(&g, 10000, 1);
    fixed_path(&g, 10000, 16, 100);
    const uint64_t t = 10190; /* the hoop is at 35 now and at 80 when the ball arrives */
    float at_release = basketball_hoop_at(&g, t);
    assert(basketball_shoot(&g, t));
    assert(g.last_hit && g.shots == 1 && g.made == 1 && near(g.rim_x, 80) && near(at_release, 35));
    /* The hoop keeps moving along its path while the ball is in the air. */
    for (unsigned dt = 0; dt < 650; dt += 50) {
        basketball_snapshot(&g, t + dt, &view);
        assert(view.in_flight && near(view.hoop_x, 35 + dt / 10.0f));
    }
    basketball_snapshot(&g, t + 100, &view);
    assert(view.hoop_x > at_release + 5 && !view.reached_rim && view.ball_y < 57);
    assert(view.made == 0 && view.score == 0 && view.shots == 1); /* the result is not revealed early */
    basketball_snapshot(&g, t + 449, &view);
    assert(!view.reached_rim && view.score == 0);
    basketball_snapshot(&g, t + 450, &view);
    assert(view.reached_rim && view.last_hit && view.made == 1 && view.score == 2 && near(view.hoop_x, 80));
    basketball_snapshot(&g, t + 640, &view); /* the ball drops through the moving net */
    assert(near(view.hoop_x, 99) && fabsf(view.ball_x - view.hoop_x) < 1.5f && view.ball_y > 34);
    assert(!basketball_shoot(&g, t + 649));
    assert(g.shots == 1);
    basketball_tick(&g, t + 650);
    basketball_snapshot(&g, t + 650, &view);
    assert(!view.in_flight && near(view.hoop_x, 100)); /* not frozen, not reset to the left */
    assert(basketball_shoot(&g, t + 650));
    assert(!g.last_hit && g.shots == 2 && g.made == 1 && g.rim_x > 80);
    basketball_snapshot(&g, t + 650 + 640, &view);
    assert(view.reached_rim && view.ball_x < 80 && view.score == 2); /* missed, bounced away from the hoop */

    basketball_start(&g, 10000, 1);
    fixed_path(&g, 10000, 16, 100);
    assert(basketball_shoot(&g, 10000) && !g.last_hit && g.rim_x < 80);
    basketball_snapshot(&g, 10640, &view);
    assert(view.ball_x > 80 && view.made == 0);
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
    assert(view.made == g.made && view.score == g.made * 2); /* an accepted shot counts even though the clock cut the flight */
    assert(!basketball_shoot(&g, start + 120000));
    assert(g.phase == BASKETBALL_RESULT && g.shots == 1);
    basketball_stop(&g);
    assert(g.phase == BASKETBALL_OFF);
    basketball_start(&g, 0, 0);
    basketball_tick(&g, BASKETBALL_DURATION_MS);
    basketball_snapshot(&g, BASKETBALL_DURATION_MS, &view);
    assert(view.phase == BASKETBALL_RESULT && view.shots == 0 && view.score == 0);
}
static void random_path_tests(void)
{
    enum { SEEDS = 60, STEP = 10 };
    bool speeds[BASKETBALL_MAX_SPEED + 1] = {0};
    float first[SEEDS], at_30s[SEEDS];
    unsigned up = 0, down = 0, centered = 0, samples = 0, fewest_turns = ~0u;
    for (unsigned seed = 0; seed < SEEDS; ++seed) {
        basketball_game_t g;
        basketball_view_t view;
        basketball_start(&g, 0, seed);
        float prev = first[seed] = basketball_hoop_at(&g, 0), prev_dx = 0;
        if (basketball_hoop_at(&g, 100) > prev) ++up; else ++down;
        unsigned turns = 0;
        for (uint64_t now = STEP; now < BASKETBALL_DURATION_MS; now += STEP) {
            basketball_tick(&g, now);
            float x = basketball_hoop_at(&g, now);
            assert(x >= BASKETBALL_HOOP_MIN_X && x <= BASKETBALL_HOOP_MAX_X);
            float dx = x - prev;
            assert(fabsf(dx) <= 1.0001f); /* at most 100 px/s: it never jumps, wraps or restarts */
            if (dx != 0) {
                if (dx * prev_dx < 0 && x > 20 && x < 140) ++turns; /* turned around in open field, not at a wall */
                prev_dx = dx;
            }
            prev = x;
            samples += 1;
            centered += fabsf(x - BASKETBALL_CENTER_X) <= BASKETBALL_HIT_PX;
            if (now % 100 == 0) {
                basketball_snapshot(&g, now, &view);
                assert(view.speed_pps >= BASKETBALL_MIN_SPEED && view.speed_pps <= BASKETBALL_MAX_SPEED);
                speeds[view.speed_pps] = true;
            }
            if (now == 30000) at_30s[seed] = x;
        }
        if (turns < fewest_turns) fewest_turns = turns;
    }
    assert(fewest_turns >= 8); /* every game has many unpredictable reversals */
    unsigned distinct_speeds = 0, distinct_start = 0, distinct_30s = 0;
    for (unsigned s = BASKETBALL_MIN_SPEED; s <= BASKETBALL_MAX_SPEED; ++s) distinct_speeds += speeds[s];
    assert(distinct_speeds > 50);
    for (unsigned a = 0; a < SEEDS; ++a) {
        bool new_start = true, new_30s = true;
        for (unsigned b = 0; b < a; ++b) {
            new_start &= fabsf(first[a] - first[b]) >= 1.0f;
            new_30s &= fabsf(at_30s[a] - at_30s[b]) >= 1.0f;
        }
        distinct_start += new_start;
        distinct_30s += new_30s;
    }
    assert(distinct_start > 40 && distinct_30s > 40); /* seeds give different starts and different tracks */
    assert(up > 10 && down > 10); /* it does not always begin by going right */
    assert(centered * 100 > samples * 5 && centered * 100 < samples * 40); /* it still visits the centre regularly */

    /* The path depends on the seed alone: tick cadence, and ticking at all, change nothing. */
    basketball_game_t a, b, c;
    basketball_start(&a, 5, 42);
    b = c = a;
    for (uint64_t now = 5; now <= 90000; now += 17) basketball_tick(&a, now);
    basketball_tick(&b, 90000);
    const uint64_t probes[] = {90000, 91234, 95000, 119999};
    for (unsigned i = 0; i < 4; ++i) {
        float x = basketball_hoop_at(&a, probes[i]);
        assert(x == basketball_hoop_at(&b, probes[i]) && x == basketball_hoop_at(&c, probes[i]));
    }
}
typedef enum { PERFECT, LEADING, IMMEDIATE, CENTERED_NOW, STRATEGIES } strategy_t;
/* Mirror the hoop back into its track, as a player anticipates a bounce. */
static float reflect(float x)
{
    const float lo = BASKETBALL_HOOP_MIN_X, width = BASKETBALL_HOOP_MAX_X - BASKETBALL_HOOP_MIN_X;
    float u = fmodf(x - lo, 2 * width);
    if (u < 0) u += 2 * width;
    return lo + (u > width ? 2 * width - u : u);
}
/* Plays one game at 33 ms frames. PERFECT knows the whole path; LEADING is a skilled human who reads the
 * hoop's current speed and direction and leads by the ball's flight time but cannot foresee a random
 * change; IMMEDIATE shoots the moment it can; CENTERED_NOW shoots when the hoop is centred at this
 * instant (the old winning habit). */
static void play(strategy_t strategy, unsigned seed, unsigned *shots, unsigned *made)
{
    basketball_game_t g;
    basketball_view_t view;
    basketball_start(&g, 0, seed);
    uint64_t now = 0;
    unsigned frozen = 0;
    while (g.phase == BASKETBALL_PLAY) {
        uint64_t release = g.ready_ms;
        if (strategy != IMMEDIATE) {
            basketball_game_t probe = g; /* ticking a copy keeps each look-ahead short */
            for (; release < g.ready_ms + 20000 && release < BASKETBALL_DURATION_MS; ++release) {
                basketball_tick(&probe, release);
                float x = basketball_hoop_at(&probe, release + (strategy == PERFECT ? BASKETBALL_RIM_MS : 0));
                float tolerance = strategy == PERFECT ? BASKETBALL_HIT_PX : 2.0f;
                if (strategy == LEADING) {
                    float velocity = (x - basketball_hoop_at(&probe, release > 30 ? release - 30 : 0)) / 0.03f;
                    x = reflect(x + velocity * BASKETBALL_RIM_MS / 1000.0f);
                    tolerance = 5.0f;
                }
                if (fabsf(x - BASKETBALL_CENTER_X) <= tolerance) break;
            }
            assert(release < g.ready_ms + 20000);
        }
        for (; now < release && g.phase == BASKETBALL_PLAY; now += 33) basketball_tick(&g, now);
        if (release >= BASKETBALL_DURATION_MS) { basketball_tick(&g, BASKETBALL_DURATION_MS); break; }
        assert(basketball_shoot(&g, release));
        float last = basketball_hoop_at(&g, release);
        for (now = release; now < release + BASKETBALL_SHOT_MS && g.phase == BASKETBALL_PLAY; now += 33) {
            basketball_tick(&g, now);
            basketball_snapshot(&g, now, &view);
            frozen += view.in_flight && now > release && view.hoop_x == last;
            last = view.hoop_x;
        }
        /* What the shot was judged against is exactly where the hoop really is when the ball arrives. */
        assert(basketball_hoop_at(&g, release + BASKETBALL_RIM_MS) == g.rim_x);
    }
    assert(frozen == 0);
    basketball_snapshot(&g, now, &view);
    assert(view.phase == BASKETBALL_RESULT && view.score == view.made * 2);
    *shots = view.shots;
    *made = view.made;
}
static void pan_tests(void)
{
    basketball_game_t idle, shot;
    basketball_start(&idle, 0, 24);
    uint64_t release = 0;
    while (fabsf(basketball_hoop_at(&idle, release) - BASKETBALL_CENTER_X) < 35.0f) release += 20;
    basketball_tick(&idle, release);
    shot = idle;
    assert(basketball_shoot(&shot, release));
    int old = basketball_pan(&idle, release, 12, 35);
    assert(abs(old) >= 3 * 10);
    for (uint64_t now = release; now < release + BASKETBALL_SHOT_MS; now += 20) {
        basketball_tick(&idle, now);
        basketball_tick(&shot, now);
        const int pan = basketball_pan(&shot, now, 12, 35);
        const float x = basketball_hoop_at(&idle, now);
        assert(pan == basketball_pan(&idle, now, 12, 35)); /* the ball never takes over */
        if (fabsf(x - BASKETBALL_CENTER_X) > 12.0f) assert((pan > 0) == (x > BASKETBALL_CENTER_X));
        assert(abs(pan - old) <= 4 && abs(pan) <= 80);
        assert(abs(basketball_pan(&shot, now, 4, 35)) <= 30);
        assert(abs(basketball_pan(&shot, now, 12, 5)) <= 25);
        old = pan;
    }
    basketball_tick(&shot, BASKETBALL_DURATION_MS);
    assert(basketball_pan(&shot, BASKETBALL_DURATION_MS, 12, 35) == 0);
}
static void full_game_tests(void)
{
    unsigned counts[STRATEGIES][2] = {{0}};
    for (unsigned seed = 0; seed < 30; ++seed) {
        for (strategy_t strategy = PERFECT; strategy < STRATEGIES; ++strategy) {
            unsigned shots, made;
            play(strategy, seed, &shots, &made);
            assert(shots < 186); /* 120 s / 650 ms */
            if (strategy == PERFECT) assert(made == shots && made > 25);
            counts[strategy][0] += shots;
            counts[strategy][1] += made;
        }
    }
    printf("basketball hit rates: perfect %u/%u, leading %u/%u, blind %u/%u, centred-now %u/%u\n",
           counts[PERFECT][1], counts[PERFECT][0], counts[LEADING][1], counts[LEADING][0],
           counts[IMMEDIATE][1], counts[IMMEDIATE][0], counts[CENTERED_NOW][1], counts[CENTERED_NOW][0]);
    /* Reading the hoop and leading it pays off, but random turns keep it from being a sure thing. */
    assert(counts[LEADING][1] * 100 > counts[LEADING][0] * 60 && counts[LEADING][1] * 100 < counts[LEADING][0] * 95);
    /* Shooting blind hits roughly the share of the track the 20 px window covers. */
    assert(counts[IMMEDIATE][1] * 100 > counts[IMMEDIATE][0] * 8 && counts[IMMEDIATE][1] * 100 < counts[IMMEDIATE][0] * 30);
    /* Pressing when the hoop is centred right now used to be a sure hit; with a moving hoop and a 450 ms
     * flight it must not be. */
    assert(counts[CENTERED_NOW][1] * 100 < counts[CENTERED_NOW][0] * 40);
}
int main(void)
{
    debounce_tests(); interrupt_debounce_tests(); bounce_tests(); shot_tests(); timing_tests(); random_path_tests();
    pan_tests(); full_game_tests();
    puts("basketball: debounce, bouncing random path, judgment at the rim, timer and scoring passed");
}
