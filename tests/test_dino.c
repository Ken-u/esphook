#include "dino_game.h"
#include "dino_sprites.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define AIR_TIME_S (2.0f * DINO_JUMP_V / DINO_GRAVITY)

static void sprite_tests(void)
{
    assert(DINO_ALL_SPRITES_COUNT == 6);
    for (size_t i = 0; i < DINO_ALL_SPRITES_COUNT; ++i) {
        const dino_sprite_t *s = DINO_ALL_SPRITES[i];
        assert(s->w > 0 && s->h > 0);
        for (unsigned row = 0; row < s->h; ++row) {
            assert(strlen(s->rows[row]) == s->w);
            for (const char *c = s->rows[row]; *c; ++c) assert(*c == '#' || *c == '.');
        }
    }
    /* Sprite sizes match the physics sizes the hit boxes are derived from. */
    float w, h, b;
    dino_kind_size(DINO_CACTUS_SMALL, &w, &h, &b);
    assert(w == DINO_SPRITE_CACTUS_S.w && h == DINO_SPRITE_CACTUS_S.h);
    dino_kind_size(DINO_CACTUS_LARGE, &w, &h, &b);
    assert(w == DINO_SPRITE_CACTUS_L.w && h == DINO_SPRITE_CACTUS_L.h);
    dino_kind_size(DINO_CACTUS_PAIR, &w, &h, &b);
    assert(w == 2 * DINO_SPRITE_CACTUS_S.w + 2 && h == DINO_SPRITE_CACTUS_S.h);
    dino_kind_size(DINO_BIRD_LOW, &w, &h, &b);
    assert(w == DINO_SPRITE_BIRD_UP.w && h == DINO_SPRITE_BIRD_UP.h && b == 5);
    dino_kind_size(DINO_BIRD_HIGH, &w, &h, &b);
    assert(w == DINO_SPRITE_BIRD_DOWN.w && h == DINO_SPRITE_BIRD_DOWN.h && b == 18);
    assert(DINO_SPRITE_RUN_A.w == DINO_SPRITE_W && DINO_SPRITE_RUN_B.w == DINO_SPRITE_W);
    assert(DINO_SPRITE_RUN_A.h == DINO_SPRITE_RUN_B.h);
}
static void ready_and_start_tests(void)
{
    dino_game_t g;
    dino_view_t v;
    dino_start(&g, 1000, 3);
    dino_tick(&g, 5000); /* Waiting on the title screen changes nothing. */
    dino_snapshot(&g, &v);
    assert(v.phase == DINO_READY && v.score == 0 && v.count == 0 && !v.airborne);
    assert(dino_press(&g, 5000)); /* The first press both starts the run and jumps. */
    dino_snapshot(&g, &v);
    assert(v.phase == DINO_PLAY && v.airborne);
    assert(sizeof g < 256);
}
/* Records the apex and the airtime of one jump on an empty field (the first obstacle is 120 px away). */
static void jump_profile(uint64_t release_after_ms, float *apex, float *air_s)
{
    dino_game_t g;
    dino_view_t v;
    dino_start(&g, 0, 11);
    assert(dino_press(&g, 0));
    *apex = 0;
    unsigned landed_ms = 0;
    for (unsigned t = 1; t < 1200; ++t) {
        if (t == release_after_ms) dino_release(&g, t);
        dino_tick(&g, t);
        dino_snapshot(&g, &v);
        assert(v.phase == DINO_PLAY);
        if (v.y > *apex) *apex = v.y;
        if (!v.airborne && !landed_ms) landed_ms = t;
    }
    assert(landed_ms > 0);
    *air_s = (float)landed_ms / 1000.0f;
}
static void jump_tests(void)
{
    float apex, air;
    jump_profile(10000, &apex, &air); /* Never released: full jump. */
    assert(apex > 23.2f && apex < 24.2f);
    assert(fabsf(air - AIR_TIME_S) < 0.01f);
    jump_profile(5, &apex, &air); /* A tap still hops, but low. */
    assert(apex > 12.0f && apex < 13.6f);
    assert(air > 0.35f && air < 0.45f);
    jump_profile(100, &apex, &air); /* Released mid-rise: in between. */
    assert(apex > 14.0f && apex < 23.0f);
    jump_profile(400, &apex, &air); /* Released after the apex: no effect. */
    assert(apex > 23.2f && fabsf(air - AIR_TIME_S) < 0.01f);

    dino_game_t g;
    dino_view_t v;
    dino_start(&g, 0, 11);
    assert(dino_press(&g, 0));
    dino_release(&g, 20);
    dino_tick(&g, 30);
    dino_snapshot(&g, &v);
    float y = v.y;
    assert(!dino_press(&g, 30)); /* No double jump. */
    dino_snapshot(&g, &v);
    assert(v.y == y);
    dino_tick(&g, 1000);
    assert(dino_press(&g, 1000)); /* Grounded again. */
}
/* Puts the dino at height `y` with one obstacle at left edge `x`, steps 1 ms and says whether it crashed. */
static bool collides(dino_kind_t kind, float x, float y)
{
    dino_game_t g;
    dino_start(&g, 0, 1);
    assert(dino_press(&g, 0));
    g.y = y;
    g.vy = 0.0f;
    g.travel_left = 1e9f;
    g.count = 1;
    g.obstacles[0] = (dino_obstacle_t){kind, x};
    dino_tick(&g, 1);
    return g.phase == DINO_OVER;
}
static void hitbox_tests(void)
{
    const float over = DINO_X + 4.0f; /* an obstacle sitting inside the dino's columns */
    /* Heights: the dino clears an obstacle only with its feet above the obstacle's solid top. */
    assert(collides(DINO_CACTUS_SMALL, over, 0.0f) && collides(DINO_CACTUS_SMALL, over, 8.5f));
    assert(!collides(DINO_CACTUS_SMALL, over, 9.5f)); /* top of a small cactus: 10 px minus a 1 px margin */
    assert(collides(DINO_CACTUS_PAIR, over, 8.5f) && !collides(DINO_CACTUS_PAIR, over, 9.5f));
    assert(collides(DINO_CACTUS_LARGE, over, 13.5f) && !collides(DINO_CACTUS_LARGE, over, 14.5f));
    assert(collides(DINO_BIRD_LOW, over, 0.0f) && collides(DINO_BIRD_LOW, over, 11.5f) && !collides(DINO_BIRD_LOW, over, 12.5f));
    /* A high bird passes overhead only while the dino stays low; jumping into it is a crash. */
    assert(!collides(DINO_BIRD_HIGH, over, 0.0f) && !collides(DINO_BIRD_HIGH, over, 5.5f));
    assert(collides(DINO_BIRD_HIGH, over, 7.0f) && collides(DINO_BIRD_HIGH, over, 20.0f));
    assert(collides(DINO_BIRD_HIGH, over, 24.0f) && !collides(DINO_BIRD_HIGH, over, 25.5f));
    /* Widths: the sprite's empty margins are not solid. */
    float ahead = DINO_X + DINO_BOX_RIGHT; /* right edge of the dino's box */
    assert(!collides(DINO_CACTUS_SMALL, ahead - 1.0f + 0.3f, 0.0f) && collides(DINO_CACTUS_SMALL, ahead - 1.0f - 0.3f, 0.0f));
    float behind = DINO_X + DINO_BOX_LEFT; /* left edge of the dino's box; a small cactus is 6 wide, solid from +1 to +5 */
    assert(!collides(DINO_CACTUS_SMALL, behind - 5.0f - 0.3f, 0.0f) && collides(DINO_CACTUS_SMALL, behind - 5.0f + 0.3f, 0.0f));
}
static void determinism_tests(void)
{
    dino_game_t games[3];
    dino_game_t *a = &games[0], *b = &games[1], *c = &games[2];
    for (unsigned i = 0; i < 3; ++i) dino_start(&games[i], 0, 99);
    static const unsigned presses[][2] = {{0, 300}, {1700, 1900}, {3200, 3700}};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned n = 0; n < 3; ++n) {
            dino_press(&games[n], presses[i][0]);
            dino_release(&games[n], presses[i][1]);
        }
    }
    for (unsigned t = 0; t <= 6000; t += 5) dino_tick(a, t);
    for (unsigned t = 0; t <= 6000; t += 33) dino_tick(b, t);
    dino_tick(b, 6000);
    dino_tick(c, 6000);
    assert(memcmp(a, c, sizeof *a) == 0 && memcmp(b, c, sizeof *b) == 0);
    dino_game_t d;
    dino_start(&d, 0, 100); /* A different seed must not replay the same field. */
    dino_press(&d, 0);
    dino_tick(&d, 6000);
    assert(a->distance != d.distance || memcmp(a->obstacles, d.obstacles, sizeof a->obstacles) != 0);
}
static void stall_tests(void)
{
    dino_game_t g;
    dino_view_t v;
    dino_start(&g, 0, 5);
    dino_press(&g, 0);
    dino_tick(&g, 600000); /* Something froze the clock for ten minutes: treated as a pause. */
    dino_snapshot(&g, &v);
    assert(v.phase == DINO_PLAY && v.score < 5);
}
static void crash_and_restart_tests(void)
{
    dino_game_t g;
    dino_view_t v;
    dino_start(&g, 0, 21);
    dino_press(&g, 0);
    dino_release(&g, 40);
    uint64_t t = 0;
    while (g.phase == DINO_PLAY && t < 20000) dino_tick(&g, ++t); /* One hop, then straight into the first obstacle. */
    assert(g.phase == DINO_OVER);
    dino_snapshot(&g, &v);
    assert(v.score > 5 && v.best == v.score && !v.new_best && !v.can_restart); /* The first run has no record to beat. */
    unsigned score = v.score;
    assert(!dino_press(&g, t + 100)); /* A panicking double tap must not skip the result. */
    dino_snapshot(&g, &v);
    assert(v.phase == DINO_OVER && v.score == score);
    float x0 = v.obstacles[0].x;
    dino_tick(&g, t + 300);
    dino_snapshot(&g, &v);
    assert(v.obstacles[0].x == x0); /* The world is frozen after a crash. */
    dino_tick(&g, t + DINO_RESTART_DELAY_MS + 10);
    dino_snapshot(&g, &v);
    assert(v.can_restart);
    assert(dino_press(&g, t + DINO_RESTART_DELAY_MS + 10));
    dino_snapshot(&g, &v);
    assert(v.phase == DINO_PLAY && v.score == 0 && v.count == 0 && v.best == score && v.airborne);
    /* Beating an earlier best is a record; falling short of it is not and keeps the old best. */
    uint64_t now = t + DINO_RESTART_DELAY_MS + 10;
    g.best = 1;
    while (g.phase == DINO_PLAY && now < t + 60000) dino_tick(&g, ++now);
    dino_snapshot(&g, &v);
    assert(v.phase == DINO_OVER && v.new_best && v.best == v.score && v.score > 1);
    now += DINO_RESTART_DELAY_MS + 10;
    assert(dino_press(&g, now));
    g.best = 100000;
    while (g.phase == DINO_PLAY && now < t + 120000) dino_tick(&g, ++now);
    dino_snapshot(&g, &v);
    assert(v.phase == DINO_OVER && !v.new_best && v.best == 100000);
}

typedef enum { BOT_PERFECT, BOT_JUMP_AT_EVERYTHING, BOT_TAPPER } bot_mode_t;
typedef struct {
    uint64_t survived_ms;
    unsigned score, spawned[DINO_KINDS], bird_before_limit;
    double min_gap_s, max_gap_s;
    unsigned top_speed;
    bool died;
    dino_kind_t killer;
} bot_result_t;

static float g_timing_error_s; /* Positive: the bot presses that much too early. */
static bool needs_jump(dino_kind_t kind, bot_mode_t mode)
{
    return kind != DINO_BIRD_HIGH || mode == BOT_JUMP_AT_EVERYTHING;
}
static void run_bot(uint32_t seed, uint64_t limit_ms, bot_mode_t mode, bot_result_t *out)
{
    dino_game_t g;
    dino_view_t v;
    memset(out, 0, sizeof *out);
    out->min_gap_s = 1e9;
    dino_start(&g, 0, seed);
    dino_press(&g, 0);
    uint64_t pressed_at = 0, last_spawn = 0;
    for (uint64_t t = 1; t <= limit_ms; ++t) {
        dino_tick(&g, t);
        dino_snapshot(&g, &v);
        if (v.speed_pps > out->top_speed) out->top_speed = v.speed_pps;
        if (v.phase != DINO_PLAY) {
            out->died = true;
            out->killer = v.count ? v.obstacles[0].kind : DINO_CACTUS_SMALL;
            for (unsigned i = 0; i < v.count; ++i) {
                float w, h, b;
                dino_kind_size(v.obstacles[i].kind, &w, &h, &b);
                if (v.obstacles[i].x < DINO_X + DINO_BOX_RIGHT && v.obstacles[i].x + w > DINO_X + DINO_BOX_LEFT)
                    out->killer = v.obstacles[i].kind;
            }
            break;
        }
        out->survived_ms = t;
        /* A spawn is the only moment an obstacle sits exactly at the spawn x. */
        if (g.count && g.obstacles[g.count - 1].x == DINO_FIELD_W + 4.0f) {
            const dino_obstacle_t *last = &g.obstacles[g.count - 1];
            out->spawned[last->kind]++;
            if (last->kind >= DINO_BIRD_LOW && g.distance < DINO_BIRD_AFTER_PX) out->bird_before_limit++;
            if (last_spawn) {
                double gap = (double)(t - last_spawn) / 1000.0;
                if (gap < out->min_gap_s) out->min_gap_s = gap;
                if (gap > out->max_gap_s) out->max_gap_s = gap;
            }
            last_spawn = t;
        }
        if (mode == BOT_TAPPER && v.airborne && t - pressed_at >= 40) dino_release(&g, t);
        if (v.airborne) continue;
        /* Press when the middle of the hit window will line up with the top of the jump. */
        float half_air = (mode == BOT_TAPPER ? 0.2f : AIR_TIME_S / 2.0f) + g_timing_error_s;
        for (unsigned i = 0; i < v.count; ++i) {
            float w, h, b;
            dino_kind_size(v.obstacles[i].kind, &w, &h, &b);
            float ahead = v.obstacles[i].x + 1.0f - (DINO_X + DINO_BOX_RIGHT);
            float window = (DINO_BOX_RIGHT - DINO_BOX_LEFT) + (w - 2.0f);
            if (ahead + window < 0.0f || !needs_jump(v.obstacles[i].kind, mode)) continue;
            if ((ahead + window / 2.0f) / (float)v.speed_pps <= half_air) {
                dino_press(&g, t);
                pressed_at = t;
            }
            break;
        }
    }
    dino_snapshot(&g, &v);
    out->score = v.score;
}
static void perfect_bot_tests(void)
{
    unsigned totals[DINO_KINDS] = {0};
    double min_gap = 1e9, max_gap = 0;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        bot_result_t r;
        run_bot(seed, 200000, BOT_PERFECT, &r);
        assert(!r.died); /* Every generated field can be cleared with well timed full jumps. */
        assert(r.score > 2400);
        assert(r.top_speed == 190); /* Speed ramps up to the cap within the run. */
        assert(r.bird_before_limit == 0);
        for (unsigned k = 0; k < DINO_KINDS; ++k) totals[k] += r.spawned[k];
        if (r.min_gap_s < min_gap) min_gap = r.min_gap_s;
        if (r.max_gap_s > max_gap) max_gap = r.max_gap_s;
    }
    for (unsigned k = 0; k < DINO_KINDS; ++k) assert(totals[k] > 30); /* Every obstacle type shows up. */
    assert(min_gap > 0.85 && max_gap < 2.1);
    printf("dino perfect bot: gaps %.2f..%.2f s, spawned small=%u large=%u pair=%u low=%u high=%u\n", min_gap, max_gap,
           totals[DINO_CACTUS_SMALL], totals[DINO_CACTUS_LARGE], totals[DINO_CACTUS_PAIR], totals[DINO_BIRD_LOW],
           totals[DINO_BIRD_HIGH]);
}
/* How sloppy may a player be? Every seed must survive 60 s with a consistent timing error. */
static void timing_tolerance_tests(void)
{
    static const float errors[] = {0.05f, -0.05f, 0.08f, -0.08f, 0.12f, -0.12f, 0.16f, -0.16f};
    for (unsigned i = 0; i < sizeof errors / sizeof errors[0]; ++i) {
        unsigned deaths = 0;
        g_timing_error_s = errors[i];
        for (uint32_t seed = 0; seed < 20; ++seed) {
            bot_result_t r;
            run_bot(seed, 60000, BOT_PERFECT, &r);
            deaths += r.died;
        }
        printf("dino timing error %+.0f ms: %u/20 seeds died within 60 s\n", errors[i] * 1000.0f, deaths);
        if (fabsf(errors[i]) <= 0.081f) assert(deaths == 0); /* The larger errors are printed for tuning only. */
    }
    g_timing_error_s = 0.0f;
}
static void decision_tests(void)
{
    unsigned died_on_high = 0, total_everything = 0, tapper_died = 0, tapper_on_big = 0;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        bot_result_t r;
        run_bot(seed, 300000, BOT_JUMP_AT_EVERYTHING, &r);
        assert(r.died); /* Jumping at a high bird is a mistake: birds are not just decoration. */
        ++total_everything;
        if (r.killer == DINO_BIRD_HIGH) ++died_on_high;
        run_bot(seed, 300000, BOT_TAPPER, &r);
        if (r.died) {
            ++tapper_died;
            if (r.killer == DINO_CACTUS_LARGE || r.killer == DINO_CACTUS_PAIR || r.killer == DINO_BIRD_LOW) ++tapper_on_big;
        }
    }
    assert(died_on_high >= total_everything * 8 / 10);
    assert(tapper_died == 20 && tapper_on_big >= 17); /* Short hops only clear small cacti: holding matters. */
    printf("dino decisions: jump-at-everything died on a high bird %u/20, tapper died on tall obstacles %u/20\n",
           died_on_high, tapper_on_big);
}

int main(void)
{
    sprite_tests();
    ready_and_start_tests();
    jump_tests();
    hitbox_tests();
    determinism_tests();
    stall_tests();
    crash_and_restart_tests();
    perfect_bot_tests();
    timing_tolerance_tests();
    decision_tests();
    puts("dino tests passed");
    return 0;
}
