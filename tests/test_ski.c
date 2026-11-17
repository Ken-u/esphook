#include "ski_game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void inject(ski_game_t *g, float x, float dy)
{
    g->count = 1;
    g->obstacles[0] = (ski_obstacle_t){.x = x, .wy = (float)(g->distance + dy), .kind = SKI_TREE};
}
static void basic_tests(void)
{
    ski_game_t g;
    ski_view_t v;
    assert(sizeof g < 1100);
    ski_start(&g, 1000, 3);
    ski_tick(&g, 9000); /* Waiting on the title screen changes nothing. */
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_READY && v.meters == 0 && v.lives == 3 && v.dir == 1 && v.count == 0);
    assert(ski_press(&g, 9000)); /* The first press starts, it does not turn. */
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_PLAY && v.dir == 1 && v.skier_x == 80.0f);
    ski_tick(&g, 10000);
    ski_snapshot(&g, &v);
    assert(v.skier_x > 100.0f && v.meters > 5); /* Down-right at SKI_SLOPE of the descent. */
    float travelled = (float)g.distance;
    assert(fabsf((v.skier_x - 80.0f) / travelled - SKI_SLOPE) < 0.001f);
    float x = v.skier_x;
    assert(ski_press(&g, 10000));
    ski_tick(&g, 11000);
    ski_snapshot(&g, &v);
    assert(v.dir == -1 && v.skier_x < x - 20.0f);
    /* Held against a wall the skier slides along it instead of leaving the screen. */
    ski_game_t wall;
    ski_start(&wall, 0, 4);
    ski_press(&wall, 0);
    for (unsigned t = 5; t < 9000; t += 5) { ski_tick(&wall, t); wall.count = 0; wall.invuln_until = ~0ULL; }
    ski_snapshot(&wall, &v);
    assert(v.skier_x == SKI_X_MAX);
}
static void determinism_tests(void)
{
    ski_game_t games[3];
    for (unsigned i = 0; i < 3; ++i) ski_start(&games[i], 0, 77);
    static const unsigned presses[] = {0, 1500, 2600, 4100, 5200, 7000};
    for (unsigned i = 0; i < sizeof presses / sizeof presses[0]; ++i)
        for (unsigned n = 0; n < 3; ++n) ski_press(&games[n], presses[i]);
    for (unsigned t = 0; t <= 9000; t += 5) ski_tick(&games[0], t);
    for (unsigned t = 0; t <= 9000; t += 33) ski_tick(&games[1], t);
    ski_tick(&games[1], 9000);
    ski_tick(&games[2], 9000);
    assert(memcmp(&games[0], &games[2], sizeof games[0]) == 0 && memcmp(&games[1], &games[2], sizeof games[1]) == 0);
    ski_game_t other;
    ski_start(&other, 0, 78);
    assert(memcmp(games[0].obstacles, other.obstacles, sizeof other.obstacles) != 0 || games[0].count != other.count);
}
static void stall_tests(void)
{
    ski_game_t g;
    ski_view_t v;
    ski_start(&g, 0, 5);
    ski_press(&g, 0);
    ski_tick(&g, 900000);
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_PLAY && v.meters < 2);
}
static void crash_tests(void)
{
    ski_game_t g;
    ski_view_t v;
    ski_start(&g, 0, 9);
    ski_press(&g, 0);
    ski_tick(&g, 100);
    float before = (float)g.distance;
    inject(&g, g.x, 0.0f);
    ski_tick(&g, 110);
    ski_snapshot(&g, &v);
    assert(v.lives == 2 && v.invulnerable && v.stunned && g.obstacles[0].hit);
    ski_tick(&g, 1000);
    ski_snapshot(&g, &v);
    assert(!v.stunned && v.invulnerable);
    /* The stun slows the skier to under half speed. */
    ski_game_t slow, fast;
    ski_start(&slow, 0, 9); ski_start(&fast, 0, 9);
    ski_press(&slow, 0); ski_press(&fast, 0);
    ski_tick(&slow, 100); ski_tick(&fast, 100);
    inject(&slow, slow.x, 0.0f);
    ski_tick(&slow, 400); ski_tick(&fast, 400);
    assert(slow.distance < fast.distance - 5.0 && slow.distance > (double)before);
    /* Invulnerable: a second tree changes nothing. */
    inject(&g, g.x, 0.0f);
    ski_tick(&g, 1010);
    ski_snapshot(&g, &v);
    assert(v.lives == 2 && !g.obstacles[0].hit);
    /* Trees right beside the skier are not hits; trees on it are. */
    ski_game_t side;
    ski_start(&side, 0, 9);
    ski_press(&side, 0);
    ski_tick(&side, 100);
    inject(&side, side.x + SKI_SKIER_HALF_W + 3.0f + 0.5f, 0.0f);
    ski_tick(&side, 110);
    assert(side.lives == 3);
    inject(&side, side.x + SKI_SKIER_HALF_W + 3.0f - 0.5f, 0.0f);
    ski_tick(&side, 120);
    assert(side.lives == 2);
    /* Wait out the invulnerability, crash twice more: caught by the yeti. */
    ski_tick(&g, 3000);
    inject(&g, g.x, 0.0f);
    ski_tick(&g, 3010);
    ski_snapshot(&g, &v);
    assert(v.lives == 1 && v.phase == SKI_PLAY);
    ski_tick(&g, 3300);
    inject(&g, g.x, 0.0f); /* Still inside the 1.4 s after the crash: no further life lost. */
    ski_tick(&g, 3400);
    ski_snapshot(&g, &v);
    assert(v.lives == 1 && v.invulnerable);
    ski_tick(&g, 4500);
    ski_snapshot(&g, &v);
    assert(fabsf(v.yeti_y - 5.0f) < 0.01f && fabsf(v.yeti_x - v.skier_x) < 1.0f); /* Right behind the skier. */
    ski_tick(&g, 6000);
    inject(&g, g.x, 0.0f);
    ski_tick(&g, 6010);
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_OVER && v.caught && v.lives == 0 && v.best == v.meters && !v.new_best && !v.can_restart);
    unsigned meters = v.meters;
    assert(!ski_press(&g, 6100)); /* A panicked double tap must not skip the result. */
    ski_tick(&g, 6400);
    ski_snapshot(&g, &v);
    assert(v.meters == meters);
    ski_tick(&g, 6700);
    ski_snapshot(&g, &v);
    assert(v.can_restart);
    assert(ski_press(&g, 6700));
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_PLAY && v.lives == 3 && v.meters == 0 && v.best == meters && v.skier_x == 80.0f && v.dir == 1);
    /* Beating an earlier best is a record; falling short of it is not and keeps the old best. */
    uint64_t now = 6700;
    g.best_m = 1;
    for (unsigned crashes = 0; crashes < 3; ++crashes) {
        now += SKI_INVULN_MS + 10;
        ski_tick(&g, now);
        inject(&g, g.x, 0.0f);
        ski_tick(&g, now + 10);
        now += 10;
    }
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_OVER && v.new_best && v.best == v.meters && v.meters > 1);
    now += SKI_RESTART_DELAY_MS + 10;
    assert(ski_press(&g, now));
    g.best_m = 100000;
    for (unsigned crashes = 0; crashes < 3; ++crashes) {
        now += SKI_INVULN_MS + 10;
        ski_tick(&g, now);
        inject(&g, g.x, 0.0f);
        ski_tick(&g, now + 10);
        now += 10;
    }
    ski_snapshot(&g, &v);
    assert(v.phase == SKI_OVER && !v.new_best && v.best == 100000);
}

typedef enum { BOT_ROUTE, BOT_IDLE, BOT_ZIGZAG } bot_mode_t;
typedef struct {
    bool over;
    uint64_t survived_ms;
    unsigned meters, crashes, max_count, sum_count, samples, triple_presses;
    uint64_t min_press_gap_ms;
    unsigned top_speed;
} bot_result_t;

static void check_course(const ski_game_t *g)
{
    assert(g->count <= SKI_MAX_OBSTACLES);
    for (unsigned i = 0; i < g->count; ++i) {
        float deviation = fabsf(g->obstacles[i].x - ski_route_x(g, g->obstacles[i].wy));
        assert(deviation >= SKI_CORRIDOR_HALF - 0.01f); /* Nothing grows on the safe line. */
        assert(g->obstacles[i].x >= 8.0f && g->obstacles[i].x <= 153.0f);
    }
    assert(ski_route_x(g, (float)g->distance) >= SKI_ROUTE_MIN - 0.01f && ski_route_x(g, (float)g->distance) <= SKI_ROUTE_MAX + 0.01f);
}
/* `period_ms` drives the zig-zag bot; `error_s` shifts the route bot's timing (positive: turns early). */
static void run_bot(uint32_t seed, uint64_t limit_ms, bot_mode_t mode, unsigned period_ms, float error_s, bot_result_t *out)
{
    ski_game_t g;
    ski_view_t v;
    memset(out, 0, sizeof *out);
    out->min_press_gap_ms = ~0ULL;
    ski_start(&g, 0, seed);
    ski_press(&g, 0);
    uint64_t recent[3] = {0, 0, 0}, last_press = 0, next_zig = period_ms;
    unsigned lives = SKI_LIVES;
    for (uint64_t t = 1; t <= limit_ms; ++t) {
        ski_tick(&g, t);
        ski_snapshot(&g, &v);
        if (v.lives < lives) ++out->crashes;
        lives = v.lives;
        if (v.speed_pps > out->top_speed) out->top_speed = v.speed_pps;
        if (v.phase != SKI_PLAY) { out->over = true; break; }
        out->survived_ms = t;
        if (t % 50 == 0) {
            check_course(&g);
            if (v.count > out->max_count) out->max_count = v.count;
            out->sum_count += v.count;
            ++out->samples;
        }
        bool press = false;
        if (mode == BOT_ROUTE) {
            /* A late player reacts to where the safe line was a moment ago, an early one to where it will be. */
            float lead = 8.0f + g.speed * error_s;
            if (lead < -SKI_BEHIND + 2.0f) lead = -SKI_BEHIND + 2.0f;
            float delta = ski_route_x(&g, (float)g.distance + lead) - g.x;
            press = (delta > 1.5f && g.dir < 0) || (delta < -1.5f && g.dir > 0);
        } else if (mode == BOT_ZIGZAG && t >= next_zig) {
            press = true;
            next_zig += period_ms;
        }
        if (press) {
            ski_press(&g, t);
            if (last_press && t - last_press < out->min_press_gap_ms) out->min_press_gap_ms = t - last_press;
            if (recent[0] && t - recent[0] < 800) ++out->triple_presses; /* Three presses inside the menu gesture window. */
            recent[0] = recent[1]; recent[1] = recent[2]; recent[2] = t;
            if (recent[0] && recent[2] - recent[0] < 800) ++out->triple_presses;
            last_press = t;
        }
    }
    ski_snapshot(&g, &v);
    out->meters = v.meters;
}
static void route_bot_tests(void)
{
    unsigned total_meters = 0, max_count = 0, density_sum = 0, density_samples = 0;
    uint64_t min_gap = ~0ULL;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        bot_result_t r;
        run_bot(seed, 200000, BOT_ROUTE, 0, 0.0f, &r);
        assert(!r.over && r.crashes == 0); /* The generated course can always be skied cleanly. */
        assert(r.meters > 2000);
        assert(r.top_speed == 100);
        assert(r.triple_presses == 0); /* Never needs the menu gesture's three presses in 0.8 s. */
        total_meters += r.meters;
        if (r.max_count > max_count) max_count = r.max_count;
        density_sum += r.sum_count;
        density_samples += r.samples;
        if (r.min_press_gap_ms < min_gap) min_gap = r.min_press_gap_ms;
    }
    assert(max_count < SKI_MAX_OBSTACLES);
    double density = (double)density_sum / (double)density_samples;
    assert(density > 8.0 && density < 30.0 && min_gap >= 600);
    printf("ski route bot: 20 seeds x 200 s, no crash, %u m each, visible obstacles avg %.1f max %u, shortest turn gap %llu ms\n",
           total_meters / 20, density, max_count, (unsigned long long)min_gap);
}
static void timing_tolerance_tests(void)
{
    static const float errors[] = {0.06f, -0.06f, 0.10f, -0.10f, 0.14f, -0.14f, 0.20f, -0.20f, -0.24f};
    for (unsigned i = 0; i < sizeof errors / sizeof errors[0]; ++i) {
        unsigned crashed_seeds = 0, over = 0, crashes = 0;
        for (uint32_t seed = 0; seed < 20; ++seed) {
            bot_result_t r;
            run_bot(seed, 120000, BOT_ROUTE, 0, errors[i], &r);
            crashed_seeds += r.crashes > 0;
            crashes += r.crashes;
            over += r.over;
        }
        printf("ski timing error %+.0f ms: %u/20 seeds crashed at least once in 120 s (%u crashes), %u game overs\n",
               errors[i] * 1000.0f, crashed_seeds, crashes, over);
        if (fabsf(errors[i]) <= 0.101f) assert(over == 0); /* Larger errors are printed for tuning only. */
    }
}
static void bad_player_tests(void)
{
    unsigned idle_over = 0, zig_over = 0;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        bot_result_t r;
        run_bot(seed, 180000, BOT_IDLE, 0, 0.0f, &r);
        assert(r.over && r.survived_ms < 90000); /* Never turning ends in the yeti's arms. */
        idle_over += r.over;
        run_bot(seed, 180000, BOT_ZIGZAG, 1000, 0.0f, &r);
        zig_over += r.over;
    }
    printf("ski bad players: never turning lost %u/20, blind 1 s zig-zag lost %u/20 within 180 s\n", idle_over, zig_over);
    assert(zig_over >= 15);
}
static void regen_tests(void)
{
    ski_game_t g;
    ski_view_t v;
    ski_start(&g, 0, 31);
    ski_press(&g, 0);
    g.lives = 1;
    g.last_crash_ms = 0;
    for (uint64_t t = 1; t <= 45000; ++t) {
        ski_tick(&g, t);
        ski_snapshot(&g, &v);
        float delta = ski_route_x(&g, (float)g.distance + 8.0f) - g.x;
        if ((delta > 1.5f && g.dir < 0) || (delta < -1.5f && g.dir > 0)) ski_press(&g, t);
        if (t == SKI_REGEN_MS - 1) assert(v.lives == 1);
        if (t == SKI_REGEN_MS + 5) assert(v.lives == 2);
        if (t == 2 * SKI_REGEN_MS - 1) assert(v.lives == 2);
    }
    assert(v.lives == 3); /* ...and never above the maximum. */
}

int main(void)
{
    basic_tests();
    determinism_tests();
    stall_tests();
    crash_tests();
    route_bot_tests();
    timing_tolerance_tests();
    bad_player_tests();
    regen_tests();
    puts("ski tests passed");
    return 0;
}
