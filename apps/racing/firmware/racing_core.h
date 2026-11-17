#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 极速赛车: a first-person race against the clock on a winding road with traffic. The key is the accelerator, the
 * radar (or the left/right keys) steers, and curves push the car outwards the faster it goes. A nitro tank fills on a
 * timer and burns for a few seconds when it is full and the player asks for it. Checkpoints add time; when the time is
 * up the run ends. Pure C, no ESP/LVGL dependency, fixed 20 ms steps. Positions are millimetres along the road,
 * speeds are km/h (the car in 1/16 km/h), lateral positions are permille of the road's half width (+-1000 are the road
 * edges; the verge goes on to +-1500, where the car hits the barrier). */
#define RC_STEP_MS 20U
#define RC_MAX_ADVANCE_MS 200U
#define RC_CARS 6
#define RC_COUNT_MS 2800U            /* 3, 2, 1 */
#define RC_START_TIME_MS 45000U
#define RC_CHECKPOINT_BONUS_MS 24000U
#define RC_CHECKPOINT_MM 1400000     /* a checkpoint every 1.4 km */
#define RC_TOP_KMH 200
#define RC_NITRO_TOP_KMH 290
#define RC_NITRO_FILL_MS 10000U      /* from empty to full */
#define RC_NITRO_BURN_MS 3000U
#define RC_NITRO_FULL 1000
#define RC_ROAD_EDGE 1000
#define RC_WALL 1500
#define RC_CAR_LONG_MM 5000          /* how close along the road is a collision */
#define RC_CAR_WIDE 480              /* and across it, in permille */
#define RC_FINISH_MS 1800U           /* coasting after the clock runs out */
#define RC_AGE_MAX 60000U
#define RC_DIFF_MM 8000000           /* the track reaches full difficulty after 8 km */

typedef enum { RC_TITLE, RC_COUNT, RC_RACE, RC_FINISH, RC_OVER } rc_phase_t;
typedef struct {
    int32_t pos_mm;
    int16_t px, speed; /* lateral permille, km/h */
    uint8_t alive, color, passed, flash;
} rc_car_t;
typedef struct {
    rc_car_t car[RC_CARS];
    uint32_t rng, track_seed, clock_ms, carry_ms, count_ms, time_ms, finish_ms;
    uint32_t best, overtakes, checkpoints, crashes;
    uint32_t cp_age_ms, crash_age_ms, nitro_age_ms, over_age_ms;
    int32_t pos_mm, pos_acc, next_cp_mm;
    int speed16, px16; /* the player's own, with extra precision */
    int steer, nitro, nitro_ms, stun_ms;
    uint16_t spawn_wait;
    uint8_t phase, throttle, crash_pending, offroad;
} rc_t;

void rc_init(rc_t *r, uint32_t seed);
void rc_start(rc_t *r); /* from the title or the game over screen */
void rc_steer(rc_t *r, int steer); /* -256 (full left) .. +256 (full right) */
void rc_throttle(rc_t *r, bool down);
/* Fires the nitro if the tank is full. True when it did. */
bool rc_nitro(rc_t *r);
void rc_advance(rc_t *r, uint32_t dt_ms);
/* True once for every crash (traffic or barrier) since the last call. */
bool rc_take_crash(rc_t *r);
/* The bend of the road at a distance along it: signed, 0.01 mm of sideways drift per metre per metre (a gentle bend
 * is about 150, a hard one 300); positive bends to the right. */
int rc_curve_at(const rc_t *r, int32_t pos_mm);
int rc_difficulty(int32_t pos_mm); /* 0..256 */
unsigned rc_speed_kmh(const rc_t *r);
unsigned rc_distance_m(const rc_t *r);
unsigned rc_score(const rc_t *r); /* distance plus a bonus for every car passed */
unsigned rc_time_s(const rc_t *r); /* rounded up */
int rc_px(const rc_t *r);
