#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Offline-dinosaur style runner. One button: press to jump, hold to jump higher, release to cut it short.
 * The field is 160 px wide; heights are pixels above the ground line. The core has no ESP/LVGL dependency
 * and advances in fixed 1 ms steps, so the result depends only on the seed and the timestamped inputs,
 * never on how often it is ticked. */
#define DINO_FIELD_W 160
#define DINO_X 14.0f               /* left edge of the dino sprite */
#define DINO_SPRITE_W 16
#define DINO_BOX_LEFT 3.0f         /* hit box inside the sprite */
#define DINO_BOX_RIGHT 14.0f
#define DINO_BOX_H 13.0f
#define DINO_JUMP_V 165.0f         /* px/s: full jump is ~24 px high and ~0.58 s long */
#define DINO_GRAVITY 571.0f
#define DINO_MIN_HOP_Y 7.0f        /* releasing earlier still climbs this far, then is cut to DINO_DROP_V */
#define DINO_DROP_V 82.0f
#define DINO_SPEED_START 90.0f     /* px/s */
#define DINO_SPEED_MAX 190.0f
#define DINO_SPEED_GAIN 1.2f       /* px/s per second of running */
#define DINO_GAP_MIN_S 0.9f        /* time between consecutive obstacles reaching the dino */
#define DINO_GAP_MAX_S 2.0f
#define DINO_BIRD_AFTER_PX 1500.0f /* no birds before this much distance */
#define DINO_RESTART_DELAY_MS 500U
#define DINO_MAX_OBSTACLES 6

typedef enum { DINO_READY, DINO_PLAY, DINO_OVER } dino_phase_t;
typedef enum { DINO_CACTUS_SMALL, DINO_CACTUS_LARGE, DINO_CACTUS_PAIR, DINO_BIRD_LOW, DINO_BIRD_HIGH,
               DINO_KINDS } dino_kind_t;
typedef struct { dino_kind_t kind; float x; } dino_obstacle_t; /* x: left edge */

typedef struct {
    dino_phase_t phase;
    uint64_t clock_ms, run_ms, over_ms;
    uint32_t random, best;
    double distance;           /* px travelled this run */
    float speed, y, vy, travel_left; /* y: height above ground; travel_left: px until the next obstacle spawns */
    bool held, cut, record; /* record: the finished run beat an earlier best */
    unsigned count;
    dino_obstacle_t obstacles[DINO_MAX_OBSTACLES];
} dino_game_t;

typedef struct {
    dino_phase_t phase;
    unsigned score, best, speed_pps, frame, scroll, count; /* frame: run animation step; scroll: px travelled */
    float y;
    bool airborne, new_best, can_restart;
    dino_obstacle_t obstacles[DINO_MAX_OBSTACLES];
} dino_view_t;

/* Obstacle size and bottom height above ground. */
void dino_kind_size(dino_kind_t kind, float *width, float *height, float *bottom);
/* Prepares a new session in READY. `best` survives restarts inside one session. */
void dino_start(dino_game_t *game, uint64_t now_ms, uint32_t seed);
void dino_tick(dino_game_t *game, uint64_t now_ms);
/* Press starts a run, jumps when grounded, or restarts after a crash. Returns true if it did anything. */
bool dino_press(dino_game_t *game, uint64_t now_ms);
void dino_release(dino_game_t *game, uint64_t now_ms);
void dino_snapshot(const dino_game_t *game, dino_view_t *view);
