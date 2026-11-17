#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Downhill skiing, SkiFree style, on the 160x80 screen with a single button: every press flips the skiing
 * direction between down-left and down-right. The slope is generated from a hidden "safe line" that only
 * ever turns at points the player can reach, and every tree is kept clear of it, so a course can always be
 * skied without a crash. The core is plain C and advances in fixed 1 ms steps, so a run depends only on the
 * seed and the timestamped presses, never on the tick rate. */
#define SKI_FIELD_W 160
#define SKI_VIEW_H 80
#define SKI_SKIER_Y 18.0f          /* screen y of the skier; trees scroll upwards past it */
#define SKI_SKIER_HALF_W 2.5f
#define SKI_SKIER_HALF_H 4.0f
#define SKI_X_MIN 5.0f
#define SKI_X_MAX 155.0f
#define SKI_SLOPE 0.7f             /* sideways px per px of descent */
#define SKI_SPEED_START 46.0f      /* px/s of descent */
#define SKI_SPEED_MAX 100.0f
#define SKI_SPEED_GAIN 0.5f        /* px/s per second of skiing */
#define SKI_STUN_MS 600U           /* slowed after a crash */
#define SKI_INVULN_MS 1400U        /* no further crash for this long after one */
#define SKI_REGEN_MS 20000U        /* a life comes back after this long without a crash */
#define SKI_LIVES 3
#define SKI_PX_PER_METER 6.0
#define SKI_ROW_PX 14.0f           /* spacing of generated tree rows */
#define SKI_ROUTE_ROWS 16          /* safe-line points kept around the skier */
#define SKI_ROUTE_MIN 30.0f
#define SKI_ROUTE_MAX 130.0f
#define SKI_CORRIDOR_HALF 22.0f    /* tree centres stay at least this far from the safe line */
#define SKI_SAFE_START 90.0f       /* first trees show up this far downhill */
#define SKI_AHEAD 100.0f           /* course generated this far below the skier */
#define SKI_BEHIND 24.0f
#define SKI_MAX_OBSTACLES 40
#define SKI_RESTART_DELAY_MS 600U

typedef enum { SKI_READY, SKI_PLAY, SKI_OVER } ski_phase_t;
typedef enum { SKI_TREE, SKI_ROCK } ski_kind_t;
typedef struct { float x, wy; uint8_t kind, variant; bool hit; } ski_obstacle_t; /* wy: downhill position */

typedef struct {
    ski_phase_t phase;
    uint64_t clock_ms, run_ms, over_ms, invuln_until, stun_until, last_crash_ms;
    uint32_t random, best_m;
    double distance;
    float speed, x, yeti_x, yeti_y, route_x;
    int8_t dir, route_dir;
    uint8_t lives, seg_rows_left;
    bool record; /* the finished run beat an earlier best */
    uint32_t gen_row;
    float route[SKI_ROUTE_ROWS];
    unsigned count;
    ski_obstacle_t obstacles[SKI_MAX_OBSTACLES];
} ski_game_t;

typedef struct { int16_t x, y; uint8_t kind, variant; } ski_sprite_t; /* screen centre of a visible obstacle */
typedef struct {
    ski_phase_t phase;
    unsigned meters, best, lives, speed_pps, scroll, count; /* scroll: px skied, for scenery that moves with the slope */
    int dir;
    float skier_x, yeti_x, yeti_y;
    bool invulnerable, stunned, new_best, can_restart, caught;
    ski_sprite_t sprites[SKI_MAX_OBSTACLES];
} ski_view_t;

void ski_obstacle_half_size(ski_kind_t kind, float *half_w, float *half_h);
void ski_start(ski_game_t *game, uint64_t now_ms, uint32_t seed);
void ski_tick(ski_game_t *game, uint64_t now_ms);
/* Press starts a run, flips direction while skiing, or restarts after being caught. */
bool ski_press(ski_game_t *game, uint64_t now_ms);
void ski_snapshot(const ski_game_t *game, ski_view_t *view);
/* Sideways position of the guaranteed safe line at downhill position `wy` (valid around the skier). */
float ski_route_x(const ski_game_t *game, float wy);
