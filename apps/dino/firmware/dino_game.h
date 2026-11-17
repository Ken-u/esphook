#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Offline-dinosaur style runner. One button: press to jump, hold to jump higher, release to cut it short.
 * Even a quick tap hops over every cactus and the low bird; holding longer only adds height and hang time.
 * The field is 160 px wide; heights are pixels above the ground line. The core has no ESP/LVGL dependency
 * and advances in fixed 1 ms steps, so the result depends only on the seed and the timestamped inputs,
 * never on how often it is ticked. */
#define DINO_FIELD_W 160
#define DINO_X 16.0f               /* left edge of the dino sprite */
#define DINO_SPRITE_W 18
#define DINO_SPRITE_H 18
#define DINO_BOX_LEFT 4.0f         /* hit box inside the sprite */
#define DINO_BOX_RIGHT 15.0f
#define DINO_BOX_H 14.0f
#define DINO_JUMP_V 195.0f         /* px/s: full jump is ~38 px high and ~0.78 s long */
#define DINO_GRAVITY 500.0f
#define DINO_MIN_HOP_Y 10.0f       /* releasing earlier still climbs this far, then is cut to DINO_DROP_V: a ~27 px, 0.64 s tap */
#define DINO_DROP_V 130.0f
#define DINO_SPEED_START 80.0f     /* px/s */
#define DINO_SPEED_MAX 170.0f
#define DINO_SPEED_GAIN 1.0f       /* px/s per second of running */
#define DINO_GAP_MIN_S 1.15f       /* time between consecutive obstacles reaching the dino */
#define DINO_GAP_MAX_S 2.3f
#define DINO_BIRD_AFTER_PX 1500.0f /* no birds before this much distance */
#define DINO_RESTART_DELAY_MS 500U
#define DINO_MAX_OBSTACLES 6

typedef enum { DINO_READY, DINO_PLAY, DINO_OVER } dino_phase_t;
typedef enum { DINO_CACTUS_SMALL, DINO_CACTUS_LARGE, DINO_CACTUS_PAIR, DINO_BIRD_LOW, DINO_BIRD_HIGH,
               DINO_KINDS } dino_kind_t;
typedef struct { dino_kind_t kind; float x; } dino_obstacle_t; /* x: left edge */

typedef struct {
    dino_phase_t phase;
    uint64_t clock_ms, run_ms, over_ms, land_ms, jump_ms;
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
    unsigned since_land_ms, since_jump_ms;                   /* for dust puffs; saturate at 10 s */
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
