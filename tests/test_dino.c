#include "dino_game.h"
#include "dino_sprites.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define AIR_TIME_S (2.0f * DINO_JUMP_V / DINO_GRAVITY)
#define TAP_AIR_S 0.644f /* a tap released right away: climbs to DINO_MIN_HOP_Y, then rises on DINO_DROP_V */

static void sprite_tests(void)
{
    assert(DINO_ALL_SPRITES_COUNT == 8);
    for (size_t i = 0; i < DINO_ALL_SPRITES_COUNT; ++i) {
        const dino_sprite_t *s = DINO_ALL_SPRITES[i];
        assert(s->w > 0 && s->h > 0);
        bool any = false;
        for (unsigned row = 0; row < s->h; ++row) {
            assert(strlen(s->rows[row]) == s->w); /* a missing dot would shear the sprite */
            for (const char *c = s->rows[row]; *c; ++c) {
                assert(*c == '.' || strchr(DINO_SPRITE_KEYS, *c));
                any |= *c != '.';
            }
        }
        assert(any);
        /* Ground sprites stand on their last row, so the hit-box heights line up with what is drawn. */
        bool flying = s == &DINO_SPRITE_BIRD_UP || s == &DINO_SPRITE_BIRD_DOWN || s == &DINO_SPRITE_JUMP;
        assert(flying || strspn(s->rows[s->h - 1], ".") < s->w);
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
    assert(w == DINO_SPRITE_BIRD_UP.w && h == DINO_SPRITE_BIRD_UP.h && b == 4);
    dino_kind_size(DINO_BIRD_HIGH, &w, &h, &b);
    assert(w == DINO_SPRITE_BIRD_DOWN.w && h == DINO_SPRITE_BIRD_DOWN.h && b == 22);
    const dino_sprite_t *dinos[] = {&DINO_SPRITE_RUN_A, &DINO_SPRITE_RUN_B, &DINO_SPRITE_JUMP, &DINO_SPRITE_DEAD};
    for (unsigned i = 0; i < 4; ++i) assert(dinos[i]->w == DINO_SPRITE_W && dinos[i]->h == DINO_SPRITE_H);
    assert(DINO_BOX_RIGHT <= DINO_SPRITE_W && DINO_BOX_LEFT >= 0 && DINO_BOX_H <= DINO_SPRITE_H);
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
/* Records the apex and the airtime of one jump on an empty field (the first obstacle is far away). */
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
    assert(apex > 37.5f && apex < 38.3f);
    assert(fabsf(air - AIR_TIME_S) < 0.01f);
    jump_profile(5, &apex, &air); /* A tap is a real jump: well above every cactus and the low bird. */
    assert(apex > 26.0f && apex < 28.0f);
    assert(air > 0.61f && air < 0.68f);
    jump_profile(100, &apex, &air); /* Released mid-rise: in between. */
    assert(apex > 30.0f && apex < 37.0f);
    jump_profile(400, &apex, &air); /* Released after the apex: no effect. */
    assert(apex > 37.5f && fabsf(air - AIR_TIME_S) < 0.01f);
    printf("dino jump: tap %.1f px / %.2f s, hold %.1f px / %.2f s\n", 27.0f, TAP_AIR_S, 38.0f, AIR_TIME_S);

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
    dino_snapshot(&g, &v);
    assert(!v.airborne && v.since_land_ms < v.since_jump_ms); /* landing happened after take-off: the renderer shows dust */
    assert(v.since_jump_ms == 1000 && v.since_land_ms > 300 && v.since_land_ms < 400);
    assert(dino_press(&g, 1000)); /* Grounded again. */
    dino_snapshot(&g, &v);
    assert(v.since_jump_ms == 0);
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
    /* Heights: the dino clears an obstacle only with its feet above the obstacle's solid top (2 px below the sprite top). */
    assert(collides(DINO_CACTUS_SMALL, over, 0.0f) && collides(DINO_CACTUS_SMALL, over, 9.5f));
    assert(!collides(DINO_CACTUS_SMALL, over, 10.5f));
    assert(collides(DINO_CACTUS_PAIR, over, 9.5f) && !collides(DINO_CACTUS_PAIR, over, 10.5f));
    assert(collides(DINO_CACTUS_LARGE, over, 14.5f) && !collides(DINO_CACTUS_LARGE, over, 15.5f));
    assert(collides(DINO_BIRD_LOW, over, 0.0f) && collides(DINO_BIRD_LOW, over, 10.5f) && !collides(DINO_BIRD_LOW, over, 11.5f));
    /* A high bird passes overhead while the dino stays low; a tap-sized jump runs into it, a full jump goes over it. */
    assert(!collides(DINO_BIRD_HIGH, over, 0.0f) && !collides(DINO_BIRD_HIGH, over, 8.5f));
    assert(collides(DINO_BIRD_HIGH, over, 9.5f) && collides(DINO_BIRD_HIGH, over, 27.0f));
    assert(collides(DINO_BIRD_HIGH, over, 28.5f) && !collides(DINO_BIRD_HIGH, over, 29.5f));
    /* Widths: the sprite's soft edges (1.5 px each side) are not solid. */
    const float cactus_w = (float)DINO_SPRITE_CACTUS_S.w;
    float ahead = DINO_X + DINO_BOX_RIGHT; /* right edge of the dino's box */
    assert(!collides(DINO_CACTUS_SMALL, ahead - 1.5f + 0.3f, 0.0f) && collides(DINO_CACTUS_SMALL, ahead - 1.5f - 0.3f, 0.0f));
    float behind = DINO_X + DINO_BOX_LEFT; /* left edge of the dino's box */
    assert(!collides(DINO_CACTUS_SMALL, behind - cactus_w + 1.5f - 0.3f, 0.0f) &&
           collides(DINO_CACTUS_SMALL, behind - cactus_w + 1.5f + 0.3f, 0.0f));
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

/* ---- Press-timing windows -------------------------------------------------------------------------------------- */
/* One scenario: the dino stands still on the ground while an obstacle `lead` px ahead of its box runs towards it at
 * `speed`. It presses `delay_ms` after the start (0: never) and releases `hold_ms` later (0: holds the whole jump).
 * Returns true when it makes it past the obstacle. */
static bool clears(dino_kind_t kind, float speed, unsigned delay_ms, unsigned hold_ms)
{
    dino_game_t g;
    dino_start(&g, 0, 1);
    assert(dino_press(&g, 0));
    dino_release(&g, 1);
    g.y = g.vy = 0.0f;
    g.cut = g.held = false;
    g.run_ms = (uint64_t)((speed - DINO_SPEED_START) * 1000.0f);
    g.travel_left = 1e9f;
    g.count = 1;
    g.obstacles[0] = (dino_obstacle_t){kind, DINO_X + DINO_BOX_RIGHT + 120.0f};
    const uint64_t t0 = g.clock_ms;
    for (unsigned ms = 1; ms < 5000; ++ms) {
        uint64_t now = t0 + ms;
        if (delay_ms && ms == delay_ms) (void)dino_press(&g, now);
        if (delay_ms && hold_ms && ms == delay_ms + hold_ms) dino_release(&g, now);
        dino_tick(&g, now);
        if (g.phase != DINO_PLAY) return false;
        if (g.count == 0) return true; /* it ran off the left edge */
    }
    return false;
}
/* Scans press times in 5 ms steps; returns the width of the clearing window in ms and checks that it is one piece. */
static unsigned window_ms(dino_kind_t kind, float speed, unsigned hold_ms)
{
    unsigned first = 0, last = 0, count = 0;
    for (unsigned d = 5; d < 3000; d += 5) {
        if (!clears(kind, speed, d, hold_ms)) continue;
        if (!first) first = d;
        last = d;
        ++count;
    }
    if (count) assert(count == (last - first) / 5 + 1); /* no holes */
    return count * 5;
}
static void window_tests(void)
{
    static const dino_kind_t ground[] = {DINO_CACTUS_SMALL, DINO_CACTUS_LARGE, DINO_CACTUS_PAIR, DINO_BIRD_LOW};
    static const char *const names[] = {"small", "large", "pair", "low bird"};
    static const float speeds[] = {DINO_SPEED_START, 125.0f, DINO_SPEED_MAX};
    unsigned worst_tap = 100000, worst_hold = 100000;
    for (unsigned k = 0; k < 4; ++k) {
        for (unsigned i = 0; i < 3; ++i) {
            unsigned tap = window_ms(ground[k], speeds[i], 40), hold = window_ms(ground[k], speeds[i], 0);
            printf("dino window %-8s @%3.0f px/s: tap %3u ms, hold %3u ms\n", names[k], speeds[i], tap, hold);
            assert(tap >= 180); /* a plain tap clears every ground obstacle with at least +-90 ms of slack */
            assert(hold >= tap);
            if (tap < worst_tap) worst_tap = tap;
            if (hold < worst_hold) worst_hold = hold;
        }
    }
    /* High bird: a tap-sized jump is exactly the height that meets it, standing still passes under it, and a held jump
     * can still go over it when timed well. */
    for (unsigned i = 0; i < 3; ++i) {
        unsigned first_fail = 0, last_fail = 0;
        for (unsigned d = 5; d < 3000; d += 5) {
            if (clears(DINO_BIRD_HIGH, speeds[i], d, 40)) continue;
            if (!first_fail) first_fail = d;
            last_fail = d;
        }
        assert(first_fail && last_fail - first_fail >= 150); /* tapping into it is a crash for a long stretch */
        assert(clears(DINO_BIRD_HIGH, speeds[i], 0, 0));
        unsigned hold_over = 0;
        for (unsigned d = first_fail; d <= last_fail; d += 5) hold_over += clears(DINO_BIRD_HIGH, speeds[i], d, 0) ? 5U : 0U;
        printf("dino high bird @%3.0f px/s: a tap crashes for %3u ms of press times, holding clears %3u ms of them\n", speeds[i],
               last_fail - first_fail + 5, hold_over);
        assert(hold_over >= 90);
    }
    /* Without any input the first ground obstacle is fatal. */
    for (unsigned k = 0; k < 4; ++k) assert(!clears(ground[k], DINO_SPEED_START, 0, 0));
    printf("dino windows: narrowest tap %u ms, narrowest hold %u ms\n", worst_tap, worst_hold);
}

/* ---- Bots ------------------------------------------------------------------------------------------------------ */
typedef enum { BOT_PERFECT, BOT_TAPPER, BOT_TAP_EVERYTHING, BOT_HOLD_EVERYTHING } bot_mode_t;
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
    return kind != DINO_BIRD_HIGH || mode == BOT_TAP_EVERYTHING || mode == BOT_HOLD_EVERYTHING;
}
static bool taps(bot_mode_t mode)
{
    return mode == BOT_TAPPER || mode == BOT_TAP_EVERYTHING;
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
        if (taps(mode) && v.airborne && t - pressed_at >= 40) dino_release(&g, t);
        if (v.airborne) continue;
        /* Press when the middle of the hit window will line up with the middle of the jump. */
        float half_air = (taps(mode) ? TAP_AIR_S : AIR_TIME_S) / 2.0f + g_timing_error_s;
        for (unsigned i = 0; i < v.count; ++i) {
            float w, h, b;
            dino_kind_size(v.obstacles[i].kind, &w, &h, &b);
            float ahead = v.obstacles[i].x + 1.5f - (DINO_X + DINO_BOX_RIGHT);
            float window = (DINO_BOX_RIGHT - DINO_BOX_LEFT) + (w - 3.0f);
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
    for (uint32_t seed = 0; seed < 12; ++seed) {
        bot_result_t r;
        run_bot(seed, 130000, BOT_PERFECT, &r);
        assert(!r.died); /* Every generated field can be cleared with well timed full jumps. */
        assert(r.score > 1700);
        assert(r.top_speed == 170); /* Speed ramps up to the cap within the run. */
        assert(r.bird_before_limit == 0);
        for (unsigned k = 0; k < DINO_KINDS; ++k) totals[k] += r.spawned[k];
        if (r.min_gap_s < min_gap) min_gap = r.min_gap_s;
        if (r.max_gap_s > max_gap) max_gap = r.max_gap_s;
    }
    for (unsigned k = 0; k < DINO_KINDS; ++k) assert(totals[k] > 15); /* Every obstacle type shows up. */
    assert(min_gap > 1.05 && max_gap < 2.35);
    printf("dino perfect bot: gaps %.2f..%.2f s, spawned small=%u large=%u pair=%u low=%u high=%u\n", min_gap, max_gap,
           totals[DINO_CACTUS_SMALL], totals[DINO_CACTUS_LARGE], totals[DINO_CACTUS_PAIR], totals[DINO_BIRD_LOW],
           totals[DINO_BIRD_HIGH]);
}
/* The headline change: a sloppy player who only ever taps still plays on, because a tap clears every ground obstacle. */
static void tapper_tests(void)
{
    for (uint32_t seed = 0; seed < 12; ++seed) {
        bot_result_t r;
        run_bot(seed, 130000, BOT_TAPPER, &r);
        assert(!r.died);
        assert(r.top_speed == DINO_SPEED_MAX);
    }
}
/* How sloppy may a player be? Every seed must survive 60 s with a consistent timing error. */
static void timing_tolerance_tests(void)
{
    static const float errors[] = {0.04f, -0.04f, 0.07f, -0.07f, 0.10f, -0.10f, 0.14f, -0.14f};
    for (unsigned mode = 0; mode < 2; ++mode) {
        for (unsigned i = 0; i < sizeof errors / sizeof errors[0]; ++i) {
            unsigned deaths = 0;
            g_timing_error_s = errors[i];
            for (uint32_t seed = 0; seed < 12; ++seed) {
                bot_result_t r;
                run_bot(seed, 60000, mode ? BOT_TAPPER : BOT_PERFECT, &r);
                deaths += r.died;
            }
            printf("dino %s timing error %+4.0f ms: %u/12 seeds died within 60 s\n", mode ? "tap " : "hold", errors[i] * 1000.0f, deaths);
            if (fabsf(errors[i]) <= 0.101f) assert(deaths == 0); /* The larger errors are printed for tuning only. */
        }
    }
    g_timing_error_s = 0.0f;
}
static void decision_tests(void)
{
    unsigned died_on_high = 0, hold_everything_died = 0;
    for (uint32_t seed = 0; seed < 12; ++seed) {
        bot_result_t r;
        /* Tapping at a high bird is the mistake: a tap-sized jump is exactly the height that meets it. */
        run_bot(seed, 300000, BOT_TAP_EVERYTHING, &r);
        assert(r.died);
        if (r.killer == DINO_BIRD_HIGH) ++died_on_high;
        /* Holding the jump at every obstacle, high birds included, goes over them and survives. */
        run_bot(seed, 60000, BOT_HOLD_EVERYTHING, &r);
        hold_everything_died += r.died;
    }
    assert(died_on_high == 12);
    assert(hold_everything_died == 0);
    printf("dino decisions: tap-at-everything died on a high bird %u/12, hold-at-everything died %u/12\n", died_on_high,
           hold_everything_died);
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
    window_tests();
    perfect_bot_tests();
    tapper_tests();
    timing_tolerance_tests();
    decision_tests();
    puts("dino tests passed");
    return 0;
}
