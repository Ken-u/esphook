#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 滑雪大冒险: a side-scrolling downhill run. The rider stays at one screen column while the slope scrolls past.
 * One button: press = jump, keep holding in the air = spin backwards, release = stop spinning and level off.
 * A landing within SKI_LAND_TOL_DEG of the slope is safe; every full turn completed before a safe landing
 * pays a bonus. Rocks and chasms end the run, so do landings on the head.
 * The core has no ESP/LVGL dependency and advances in fixed 1 ms steps: the result depends only on the seed and
 * the timestamped inputs, never on how often it is ticked. Heights are pixels, y grows upwards. */
#define SKI_RIDER_X 44.0f          /* screen column of the rider; the world scrolls left */
#define SKI_SPEED_START 70.0f      /* px/s of horizontal travel */
#define SKI_SPEED_MAX 110.0f
#define SKI_SPEED_GAIN 0.4f        /* px/s per second */
#define SKI_JUMP_V 125.0f          /* px/s: a jump from level ground is ~23 px high and ~0.74 s long */
#define SKI_GRAVITY 340.0f
#define SKI_SPIN_DPS 540.0f        /* degrees per second while the button is held in the air */
#define SKI_LEVEL_DPS 330.0f       /* degrees per second back to the nearest whole turn after release */
#define SKI_LAND_TOL_DEG 75.0f     /* how far from the slope angle a landing may be */
#define SKI_PERFECT_DEG 25.0f
#define SKI_FLIP_POINTS 50U        /* a landed flip pays 50 * flips * combo; a perfect landing adds half */
#define SKI_COMBO_MAX 5U           /* consecutive jumps landed with a flip; a plain landing resets it */
#define SKI_GEM_POINTS 5U
#define SKI_PX_PER_POINT 8.0        /* distance score */
#define SKI_RESTART_DELAY_MS 500U
#define SKI_MAX_THINGS 20
#define SKI_SPAWN_AHEAD 128.0f     /* things appear this far in front of the rider, just off screen */
#define SKI_FIRST_OBSTACLE_PX 160.0f
#define SKI_CHASM_AFTER_PX 600.0   /* only rocks before this much distance */
#define SKI_GAP_MIN_S 1.3f         /* time between consecutive obstacle groups reaching the rider */
#define SKI_GAP_MAX_S 2.5f

typedef enum { SKI_READY, SKI_PLAY, SKI_OVER } ski_phase_t;
typedef enum { SKI_ROCK, SKI_CHASM, SKI_GEM } ski_kind_t;
typedef enum { SKI_NO_CRASH, SKI_CRASH_ROCK, SKI_CRASH_FALL, SKI_CRASH_LANDING } ski_crash_t;
/* rel: world x of the left edge minus the rider's x, so it shrinks as the world scrolls. A rock sits on the
 * ground at y; a chasm's y is the lower of its two rims; a gem's y is its centre height. */
typedef struct {
    uint8_t kind, w, h, reserved;
    float rel, y;
} ski_thing_t;

typedef struct {
    ski_phase_t phase;
    ski_crash_t crash;
    uint64_t clock_ms, run_ms, over_ms, popup_ms, gem_ms;
    uint32_t random, seed, best;
    double x;                  /* world x of the rider */
    float speed, y, vy, cam_y, angle, base, rot, ground_y, next_rel;
    unsigned air_ms, flips, gems, bonus, combo, popup_points, popup_flips;
    bool held, grounded, record, popup_perfect;
    unsigned count;
    ski_thing_t things[SKI_MAX_THINGS];
} ski_game_t;

typedef struct {
    ski_phase_t phase;
    ski_crash_t crash;
    unsigned score, best, flips, gems, combo, speed_pps, flips_air, popup_points, popup_flips, popup_age_ms,
        gem_age_ms, over_age_ms, count;
    uint32_t seed;
    double x;
    float y, cam_y, angle, rot;
    bool grounded, held, new_best, can_restart, popup_perfect;
    ski_thing_t things[SKI_MAX_THINGS];
} ski_view_t;

/* Ground height and its slope (dy/dx) for a seed; chasms are separate objects and not part of the terrain. */
float ski_terrain(uint32_t seed, double x);
float ski_terrain_slope(uint32_t seed, double x);
/* Prepares a new session in READY. `best` survives restarts inside one session. */
void ski_start(ski_game_t *game, uint64_t now_ms, uint32_t seed);
void ski_tick(ski_game_t *game, uint64_t now_ms);
/* Press starts a run, jumps when grounded, spins in the air, or restarts after a crash. True if it did anything. */
bool ski_press(ski_game_t *game, uint64_t now_ms);
void ski_release(ski_game_t *game, uint64_t now_ms);
void ski_snapshot(const ski_game_t *game, ski_view_t *view);
