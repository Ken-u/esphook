#include "ski_game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float slope_angle(uint32_t seed, double x) { return atanf(ski_terrain_slope(seed, x)) * 57.29578f; }
/* A run in progress on an empty slope: nothing is generated unless a test places it. */
static ski_game_t fresh_run(uint32_t seed)
{
    ski_game_t g;
    ski_start(&g, 0, seed);
    g.phase = SKI_PLAY;
    g.next_rel = 1e9f;
    return g;
}
static void place(ski_game_t *g, ski_kind_t kind, unsigned w, unsigned h, float rel, float y)
{
    assert(g->count < SKI_MAX_THINGS);
    g->things[g->count++] = (ski_thing_t){(uint8_t)kind, (uint8_t)w, (uint8_t)h, 0, rel, y};
}
static void run_to(ski_game_t *g, uint64_t until)
{
    for (uint64_t t = g->clock_ms + 1; t <= until; ++t) ski_tick(g, t);
}

static void terrain_tests(void)
{
    double min_slope = 9, max_slope = -9;
    for (uint32_t seed = 0; seed < 10; ++seed) {
        float prev = ski_terrain(seed, 0.0);
        for (double x = 1.0; x <= 20000.0; x += 1.0) {
            float h = ski_terrain(seed, x), slope = ski_terrain_slope(seed, x);
            assert(fabsf(h - prev) < 1.0f); /* continuous: no steps */
            float numeric = (ski_terrain(seed, x + 0.5) - ski_terrain(seed, x - 0.5));
            assert(fabsf(numeric - slope) < 0.01f); /* the analytic slope really is the derivative */
            if (slope < min_slope) min_slope = slope;
            if (slope > max_slope) max_slope = slope;
            prev = h;
        }
        double mean = (ski_terrain(seed, 20000.0) - ski_terrain(seed, 0.0)) / 20000.0;
        assert(mean < -0.11 && mean > -0.13); /* an overall descent */
    }
    assert(min_slope > -0.8 && max_slope < 0.5);
    assert(ski_terrain(1, 100.0) != ski_terrain(2, 100.0));
    printf("ski terrain: slope %.2f..%.2f (steepest drop %.0f deg)\n", min_slope, max_slope, atan(-min_slope) * 57.3);
}
static void start_tests(void)
{
    ski_game_t g;
    ski_view_t v;
    ski_start(&g, 1000, 3);
    ski_tick(&g, 9000); /* Waiting on the title screen changes nothing. */
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_READY && v.score == 0 && v.count == 0 && v.grounded && v.x == 0.0);
    assert(ski_press(&g, 9000)); /* The first press both starts the run and jumps. */
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_PLAY && !v.grounded && v.held);
    assert(sizeof g < 600 && sizeof v < 500);
}
/* One tap on an empty slope: how high and how long. */
static void jump_tests(void)
{
    float lo_air = 9, hi_air = 0, lo_apex = 99, hi_apex = 0;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        ski_game_t g = fresh_run(seed);
        g.x = 40.0 * seed; /* different stretches of slope */
        g.y = g.ground_y = g.cam_y = ski_terrain(g.seed, g.x);
        assert(ski_press(&g, 0));
        ski_release(&g, 40);
        float apex = 0;
        unsigned landed = 0;
        for (unsigned t = 1; t < 1500; ++t) {
            ski_tick(&g, t);
            assert(g.phase == SKI_PLAY);
            float rise = g.y - ski_terrain(g.seed, g.x);
            if (rise > apex) apex = rise;
            if (g.grounded && !landed) landed = t;
        }
        assert(landed > 0);
        float air = (float)landed / 1000.0f;
        if (air < lo_air) lo_air = air;
        if (air > hi_air) hi_air = air;
        if (apex < lo_apex) lo_apex = apex;
        if (apex > hi_apex) hi_apex = apex;
    }
    assert(lo_air > 0.55f && hi_air < 1.1f); /* a hop is about 0.8 s on level ground, hills change that a little */
    assert(lo_apex > 8.0f && hi_apex < 36.0f);
    printf("ski jump: airtime %.2f..%.2f s, apex over the ground %.0f..%.0f px\n", lo_air, hi_air, lo_apex, hi_apex);
}
static void spin_tests(void)
{
    ski_game_t g = fresh_run(3);
    ski_view_t v;
    assert(ski_press(&g, 0));
    run_to(&g, 300); /* Holding in the air spins backwards at SKI_SPIN_DPS. */
    ski_snapshot(&g, &v);
    assert(v.held && !v.grounded && fabsf(g.rot - SKI_SPIN_DPS * 0.3f) < 2.0f);
    ski_release(&g, 300);
    run_to(&g, 340);
    float after = g.rot;
    assert(after < SKI_SPIN_DPS * 0.3f); /* The first half turn levels back to upright, not on to the next turn. */
    run_to(&g, 400);
    assert(g.rot < after);
    ski_press(&g, 400); /* Pressing again in the air resumes the spin without a second jump. */
    float vy = g.vy;
    run_to(&g, 420);
    assert(g.rot > 0.0f && fabsf(g.vy - (vy - SKI_GRAVITY * 0.02f)) < 0.5f);

    /* Levelling: past half a turn the rider finishes the turn, short of it the rider unwinds. */
    static const float starts[] = {100.0f, 190.0f, 300.0f, 540.0f + 5.0f, 730.0f};
    static const float ends[] = {0.0f, 360.0f, 360.0f, 720.0f, 720.0f};
    for (unsigned i = 0; i < 5; ++i) {
        ski_game_t h = fresh_run(3);
        h.grounded = false;
        h.y = h.ground_y + 500.0f; /* high enough to still be flying */
        h.vy = 0.0f;
        h.rot = starts[i];
        h.held = false;
        run_to(&h, 1500);
        assert(!h.grounded && fabsf(h.rot - ends[i]) < 0.01f);
    }
}
/* Puts the rider just above the ground with a chosen rotation and lets it land. */
static ski_game_t land_with(float rot, unsigned combo, unsigned air_ms)
{
    ski_game_t g = fresh_run(7);
    g.x = 500.0;
    float ground = ski_terrain(g.seed, g.x);
    g.grounded = false;
    g.base = slope_angle(g.seed, g.x);
    g.rot = rot;
    g.held = false;
    g.y = ground + 0.3f;
    g.vy = -20.0f;
    g.combo = combo;
    g.air_ms = air_ms;
    run_to(&g, 60);
    return g;
}
static void landing_tests(void)
{
    static const float safe[] = {0.0f, 20.0f, 55.0f, 305.0f, 340.0f};
    static const float crash[] = {110.0f, 150.0f, 180.0f, 220.0f, 250.0f};
    for (unsigned i = 0; i < 5; ++i) {
        ski_game_t g = land_with(safe[i], 0, 400);
        assert(g.phase == SKI_PLAY && g.grounded && g.crash == SKI_NO_CRASH && g.bonus == 0);
        g = land_with(crash[i], 0, 400);
        assert(g.phase == SKI_OVER && g.crash == SKI_CRASH_LANDING && g.bonus == 0);
        g = land_with(crash[i] + 360.0f, 0, 400); /* spinning more does not make a bad angle good */
        assert(g.phase == SKI_OVER && g.bonus == 0 && g.flips == 0);
    }
    ski_game_t g = land_with(385.0f, 0, 700); /* one flip, 25 degrees off or better: perfect, 50 * 1 * 1 + half */
    assert(g.phase == SKI_PLAY && g.flips == 1 && g.combo == 1 && g.bonus == 75 && g.popup_perfect && g.popup_points == 75);
    g = land_with(420.0f, 0, 700); /* 60 degrees off: a flip, but not a perfect landing */
    assert(g.phase == SKI_PLAY && g.flips == 1 && g.bonus == 50 && !g.popup_perfect);
    g = land_with(745.0f, 0, 900); /* two turns in one jump pay for both */
    assert(g.phase == SKI_PLAY && g.flips == 2 && g.bonus == 150 && g.popup_flips == 2);
    g = land_with(385.0f, 2, 700); /* the streak multiplies: combo 3 */
    assert(g.combo == 3 && g.bonus == 50 * 3 + 75);
    g = land_with(385.0f, SKI_COMBO_MAX, 700);
    assert(g.combo == SKI_COMBO_MAX);
    g = land_with(10.0f, 3, 700); /* a plain jump breaks the streak ... */
    assert(g.combo == 0 && g.phase == SKI_PLAY);
    g = land_with(10.0f, 3, 100); /* ... but a small hop does not */
    assert(g.combo == 3);
    g = land_with(385.0f, 0, 700);
    ski_view_t v;
    ski_snapshot(&g, &v);
    assert(v.popup_points == 75 && v.popup_age_ms < 100 && v.score >= 500 / 8 + 75);
    ski_tick(&g, g.clock_ms + 1700);
    ski_snapshot(&g, &v);
    assert(v.popup_points == 0); /* the pop-up fades out */
}
/* Rider placed `lift` above `ground` while an obstacle sits at `rel`; steps 1 ms and says whether the run ended. */
static bool rock_collides(float rel, unsigned w, unsigned h, float lift_over_top)
{
    ski_game_t g = fresh_run(11);
    g.x = 300.0;
    float ground = ski_terrain(g.seed, g.x);
    place(&g, SKI_ROCK, w, h, rel, ground);
    g.grounded = false;
    g.vy = 0.0f;
    g.held = false;
    g.y = ground + (float)h - 1.5f + lift_over_top;
    ski_tick(&g, 1);
    return g.phase == SKI_OVER;
}
static void obstacle_tests(void)
{
    /* Heights: feet must be above the rock's top minus a 1.5 px forgiveness. */
    assert(rock_collides(0.0f, 10, 8, -0.4f) && !rock_collides(0.0f, 10, 8, 0.4f));
    assert(rock_collides(0.0f, 12, 10, -0.4f) && !rock_collides(0.0f, 12, 10, 0.4f));
    /* Widths: the rock's rim is not solid within 3 px of the rider's centre line. */
    assert(rock_collides(2.6f, 10, 8, -0.4f) && !rock_collides(3.4f, 10, 8, -0.4f));
    assert(rock_collides(-12.6f, 10, 8, -0.4f) && !rock_collides(-13.4f, 10, 8, -0.4f));

    ski_game_t g = fresh_run(5); /* Riding straight into a rock ends the run. */
    float ground = ski_terrain(g.seed, g.x + 65.0);
    place(&g, SKI_ROCK, 10, 8, 60.0f, ground);
    run_to(&g, 3000);
    assert(g.phase == SKI_OVER && g.crash == SKI_CRASH_ROCK && g.x > 50.0 && g.x < 65.0);

    g = fresh_run(5); /* Riding into a chasm too. */
    place(&g, SKI_CHASM, 26, 0, 40.0f, fminf(ski_terrain(g.seed, g.x + 40.0), ski_terrain(g.seed, g.x + 66.0)));
    run_to(&g, 3000);
    assert(g.phase == SKI_OVER && g.crash == SKI_CRASH_FALL);

    g = fresh_run(5); /* A wall: arriving low at the far rim is a crash, a few px below it is climbed. */
    g.grounded = false;
    g.vy = 0.0f;
    g.held = false;
    float rim = ski_terrain(g.seed, g.x);
    g.y = rim - 8.0f;
    ski_tick(&g, 1);
    assert(g.phase == SKI_OVER && g.crash == SKI_CRASH_FALL);
    g = fresh_run(5);
    g.grounded = false;
    g.vy = 0.0f;
    g.held = false;
    g.y = rim - 3.0f;
    ski_tick(&g, 1);
    assert(g.phase == SKI_PLAY && g.grounded);

    g = fresh_run(5); /* Gems on the path are collected once and score. */
    ground = ski_terrain(g.seed, g.x + 30.0);
    place(&g, SKI_GEM, 5, 5, 28.0f, ground + 5.0f);
    place(&g, SKI_GEM, 5, 5, 28.0f, ground + 40.0f); /* too high to reach from the ground */
    run_to(&g, 1000);
    ski_view_t v;
    ski_snapshot(&g, &v);
    assert(g.gems == 1 && v.count == 1 && v.score >= SKI_GEM_POINTS && v.gem_age_ms < 1000);
}
static bool same_state(const ski_game_t *a, const ski_game_t *b)
{
    return a->phase == b->phase && a->crash == b->crash && a->run_ms == b->run_ms && a->x == b->x && a->y == b->y &&
        a->vy == b->vy && a->rot == b->rot && a->random == b->random && a->seed == b->seed && a->bonus == b->bonus &&
        a->gems == b->gems && a->count == b->count && a->cam_y == b->cam_y &&
        memcmp(a->things, b->things, sizeof a->things) == 0;
}
static void determinism_tests(void)
{
    ski_game_t games[3];
    for (unsigned i = 0; i < 3; ++i) ski_start(&games[i], 0, 99);
    static const unsigned presses[][2] = {{0, 300}, {1700, 1900}, {3200, 3700}, {5000, 5400}};
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned n = 0; n < 3; ++n) {
            ski_press(&games[n], presses[i][0]);
            ski_release(&games[n], presses[i][1]);
        }
    }
    for (unsigned t = 0; t <= 9000; t += 5) ski_tick(&games[0], t);
    for (unsigned t = 0; t <= 9000; t += 33) ski_tick(&games[1], t);
    ski_tick(&games[1], 9000);
    for (unsigned t = 0; t <= 9000; t += 1000) ski_tick(&games[2], t); /* strides below the 2 s stall limit */
    ski_tick(&games[2], 9000);
    assert(same_state(&games[0], &games[2]) && same_state(&games[1], &games[2]));
    ski_game_t d;
    ski_start(&d, 0, 100); /* A different seed must not replay the same slope. */
    ski_press(&d, 0);
    ski_tick(&d, 4000);
    assert(games[0].seed != d.seed && games[0].y != d.y);
}
static void stall_tests(void)
{
    ski_game_t g;
    ski_view_t v;
    ski_start(&g, 0, 5);
    ski_press(&g, 0);
    ski_tick(&g, 600000); /* Something froze the clock for ten minutes: treated as a pause. */
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_PLAY && v.score < 10 && v.x < 200.0);
}
static void crash_and_restart_tests(void)
{
    ski_game_t g;
    ski_view_t v;
    ski_start(&g, 0, 21);
    ski_press(&g, 0);
    ski_release(&g, 40);
    uint64_t t = 0;
    while (g.phase == SKI_PLAY && t < 30000) ski_tick(&g, ++t); /* One hop, then straight into the first rock. */
    assert(g.phase == SKI_OVER && g.crash == SKI_CRASH_ROCK);
    ski_snapshot(&g, &v);
    assert(v.score > 5 && v.best == v.score && !v.new_best && !v.can_restart); /* No record to beat on the first run. */
    unsigned score = v.score;
    assert(!ski_press(&g, t + 100)); /* A panicking double tap must not skip the result. */
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_OVER && v.score == score && v.over_age_ms >= 100);
    double x0 = g.x;
    ski_tick(&g, t + 300);
    assert(g.x == x0); /* The world is frozen after a crash. */
    ski_tick(&g, t + SKI_RESTART_DELAY_MS + 10);
    ski_snapshot(&g, &v);
    assert(v.can_restart);
    uint32_t old_seed = g.seed;
    assert(ski_press(&g, t + SKI_RESTART_DELAY_MS + 10));
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_PLAY && v.score < 3 && v.count == 0 && v.best == score && !v.grounded && g.seed != old_seed);
    uint64_t now = t + SKI_RESTART_DELAY_MS + 10;
    g.best = 1; /* Beating an earlier best is a record ... */
    while (g.phase == SKI_PLAY && now < t + 60000) ski_tick(&g, ++now);
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_OVER && v.new_best && v.best == v.score && v.score > 1);
    now += SKI_RESTART_DELAY_MS + 10;
    assert(ski_press(&g, now));
    g.best = 100000; /* ... falling short of it is not. */
    while (g.phase == SKI_PLAY && now < t + 120000) ski_tick(&g, ++now);
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_OVER && !v.new_best && v.best == 100000);
}

/* ---- players ----------------------------------------------------------------------------------------------
 * A bot looks at the next obstacle, then asks the game itself (by simulating copies of it) at which press time a
 * jump clears it. That gives the exact window of press times that work, and a bot that presses somewhere inside
 * the window, or a little outside it. */
typedef struct {
    bool died;
    ski_crash_t cause;
    uint64_t ms;
    unsigned score, flips, jumps, unsolved, gems;
} bot_result_t;
typedef struct { int hold_ms; /* -1: hold until landing */ bool seek_flip; int error_ms; } policy_t;

#define PLAN_STEP_MS 20
#define PLAN_MAX_MS 1400
static unsigned g_window_min[2][3]; /* [rock/chasm][slow/medium/fast]: narrowest window of tap press times that clears it */
static unsigned g_window_max[2][3];
static unsigned speed_bucket(float speed) { return speed < 85.0f ? 0U : speed < 98.0f ? 1U : 2U; }

/* Copies the game, presses `delay_ms` from now and holds for `hold_ms`; true if the obstacle ends up behind. */
static bool simulate(const ski_game_t *g, unsigned delay_ms, int hold_ms, double clear_x, unsigned *flips_gained)
{
    ski_game_t c = *g;
    uint64_t start = c.clock_ms, press_at = start + delay_ms;
    bool pressed = false, released = false;
    for (uint64_t t = start + 1; t <= start + 3500; ++t) {
        ski_tick(&c, t);
        if (c.phase != SKI_PLAY) return false;
        if (!pressed && t >= press_at) { ski_press(&c, t); pressed = true; }
        if (pressed && !released) {
            if (hold_ms >= 0 ? t >= press_at + (uint64_t)hold_ms : (c.grounded && t > press_at + 5)) { ski_release(&c, t); released = true; }
        }
        if (c.x > clear_x && c.grounded) { *flips_gained = c.flips - g->flips; return true; }
    }
    return false;
}
/* Press delay in the middle of the longest window that works; -1 if no press time clears it. */
static int plan(const ski_game_t *g, double clear_x, int hold_ms, bool seek_flip, unsigned *width_ms)
{
    int best_start = -1, best_len = 0, run_start = -1, run_len = 0;
    for (unsigned d = 0; d <= PLAN_MAX_MS; d += PLAN_STEP_MS) {
        unsigned flips = 0;
        bool ok = simulate(g, d, hold_ms, clear_x, &flips) && (!seek_flip || flips > 0);
        if (ok) {
            if (run_len == 0) run_start = (int)d;
            ++run_len;
            if (run_len > best_len) { best_len = run_len; best_start = run_start; }
        } else {
            run_len = 0;
        }
    }
    *width_ms = (unsigned)best_len * PLAN_STEP_MS;
    return best_len ? best_start + (best_len - 1) * (int)PLAN_STEP_MS / 2 : -1;
}
static void run_bot(uint32_t seed, uint64_t limit_ms, const policy_t *policy, bot_result_t *out)
{
    static const int flip_holds[] = {540, 600, 660, 720};
    ski_game_t g;
    memset(out, 0, sizeof *out);
    ski_start(&g, 0, seed);
    ski_press(&g, 0);
    ski_release(&g, 60);
    double handled = -1e9;
    uint64_t press_at = 0, press_t = 0, release_t = 0;
    int hold = policy->hold_ms;
    bool pressed = false;
    for (uint64_t t = 61; t <= limit_ms; ++t) {
        ski_tick(&g, t);
        if (g.phase != SKI_PLAY) { out->died = true; out->cause = g.crash; break; }
        out->ms = t;
        if (press_at && t == press_at) {
            ski_press(&g, t);
            ++out->jumps;
            pressed = true;
            press_t = t;
            release_t = hold >= 0 ? t + (uint64_t)hold : 0;
            press_at = 0;
        } else if (pressed && (hold >= 0 ? t >= release_t : (g.grounded && t > press_t + 5))) {
            ski_release(&g, t);
            pressed = false;
        }
        if (press_at || pressed || !g.grounded) continue;
        for (unsigned i = 0; i < g.count; ++i) {
            const ski_thing_t *o = &g.things[i];
            double at = g.x + (double)o->rel;
            if (o->kind == SKI_GEM || at <= handled + 0.5 || o->rel + (float)o->w < -2.0f) continue;
            if (o->rel > 100.0f) break;
            handled = at;
            double clear = at + (double)o->w + 8.0;
            unsigned width = 0;
            int d = -1;
            hold = policy->hold_ms;
            if (policy->seek_flip) { /* try a few release moments, keep the one with the widest window */
                unsigned widest = 0;
                for (unsigned k = 0; k < 4; ++k) {
                    unsigned w = 0;
                    int dd = plan(&g, clear, flip_holds[k], true, &w);
                    if (dd >= 0 && w > widest) { widest = w; d = dd; hold = flip_holds[k]; width = w; }
                }
                if (d < 0) { hold = 60; d = plan(&g, clear, hold, false, &width); } /* no safe flip here: just hop */
            } else {
                d = plan(&g, clear, hold, false, &width);
            }
            if (d < 0) { ++out->unsolved; d = 0; }
            else if (!policy->seek_flip && hold >= 0 && hold <= 60) {
                unsigned kind = o->kind == SKI_CHASM, bucket = speed_bucket(g.speed);
                if (width < g_window_min[kind][bucket]) g_window_min[kind][bucket] = width;
                if (width > g_window_max[kind][bucket]) g_window_max[kind][bucket] = width;
            }
            int when = d + policy->error_ms;
            press_at = t + (uint64_t)(when > 1 ? when : 1);
            break;
        }
    }
    ski_view_t v;
    ski_snapshot(&g, &v);
    out->score = v.score; out->flips = v.flips; out->gems = v.gems;
}
static void tap_bot_tests(void)
{
    memset(g_window_min, 0xFF, sizeof g_window_min);
    unsigned unsolved = 0, jumps = 0;
    for (uint32_t seed = 0; seed < 6; ++seed) {
        bot_result_t r;
        run_bot(seed, 100000, &(policy_t){.hold_ms = 60}, &r);
        unsolved += r.unsolved;
        jumps += r.jumps;
        assert(!r.died && r.score > 550); /* every generated slope can be ridden with plain, well timed taps for 100 s */
    }
    assert(unsolved == 0 && jumps > 100);
    static const char *const kind_name[] = {"rock", "chasm"}, *const bucket_name[] = {"slow", "medium", "fast"};
    for (unsigned k = 0; k < 2; ++k)
        for (unsigned b = 0; b < 3; ++b) {
            if (g_window_min[k][b] == 0xFFFFFFFFU) continue;
            printf("ski tap window %s %s: %u..%u ms\n", kind_name[k], bucket_name[b], g_window_min[k][b], g_window_max[k][b]);
            assert(g_window_min[k][b] >= 200); /* never a pixel-perfect jump: at least +-100 ms of play */
        }
}
static void timing_tolerance_tests(void)
{
    static const int errors[] = {-120, 120, -180, 180};
    for (unsigned i = 0; i < 4; ++i) {
        unsigned deaths = 0;
        for (uint32_t seed = 0; seed < 6; ++seed) {
            bot_result_t r;
            run_bot(seed, 45000, &(policy_t){.hold_ms = 60, .error_ms = errors[i]}, &r);
            deaths += r.died;
        }
        printf("ski timing error %+d ms: %u/6 seeds died within 45 s\n", errors[i], deaths);
        if (abs(errors[i]) <= 120) assert(deaths == 0); /* larger errors are printed for tuning only */
    }
}
static void flip_bot_tests(void)
{
    unsigned flips = 0, deaths = 0, held_deaths = 0, held_landing = 0, jumps = 0;
    for (uint32_t seed = 0; seed < 4; ++seed) {
        bot_result_t r;
        run_bot(seed, 90000, &(policy_t){.seek_flip = true}, &r); /* spins, releases to level, lands safely */
        flips += r.flips;
        deaths += r.died;
        jumps += r.jumps;
    }
    printf("ski flip bot: %u flips in %u jumps, deaths %u\n", flips, jumps, deaths);
    assert(deaths == 0 && flips * 2 >= jumps); /* flipping is within reach of nearly every jump, with release timing alone */
    for (uint32_t seed = 0; seed < 6; ++seed) {
        bot_result_t r;
        run_bot(seed, 60000, &(policy_t){.hold_ms = -1}, &r); /* never lets go: spins the whole way */
        held_deaths += r.died;
        if (r.died && r.cause == SKI_CRASH_LANDING) ++held_landing;
    }
    printf("ski hold-all bot: %u/6 died, %u of them on a landing\n", held_deaths, held_landing);
}
static void naive_bot_tests(void)
{
    bot_result_t r; /* Never jumping meets the first rock; a bot that jumps at the wrong time meets it too. */
    ski_game_t g;
    ski_start(&g, 4, 4);
    ski_press(&g, 4);
    ski_release(&g, 50);
    uint64_t t = 50;
    while (g.phase == SKI_PLAY && t < 60000) ski_tick(&g, ++t);
    assert(g.phase == SKI_OVER && g.crash == SKI_CRASH_ROCK);
    run_bot(2, 60000, &(policy_t){.hold_ms = 60, .error_ms = 600}, &r);
    assert(r.died);
}

int main(void)
{
    terrain_tests();
    start_tests();
    jump_tests();
    spin_tests();
    landing_tests();
    obstacle_tests();
    determinism_tests();
    stall_tests();
    crash_and_restart_tests();
    tap_bot_tests();
    timing_tolerance_tests();
    flip_bot_tests();
    naive_bot_tests();
    puts("ski tests passed");
    return 0;
}
