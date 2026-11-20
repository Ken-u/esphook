#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BASKETBALL_DURATION_MS 120000U
/* Release -> next shot allowed: the whole flight and drop animation. */
#define BASKETBALL_SHOT_MS 650U
/* Release -> the ball reaches the rim. The shot is judged against where the hoop is then,
 * so the player has to lead a moving hoop by this long. */
#define BASKETBALL_RIM_MS 450U
#define BASKETBALL_CENTER_X 80.0f
#define BASKETBALL_HIT_PX 10.0f
/* The hoop centre stays inside this range; it bounces off both ends. */
#define BASKETBALL_HOOP_MIN_X 16.0f
#define BASKETBALL_HOOP_MAX_X 144.0f
#define BASKETBALL_MIN_SPEED 36U
#define BASKETBALL_MAX_SPEED 100U
/* A leg lasts this long before the hoop draws a new speed and maybe turns around. */
#define BASKETBALL_LEG_MIN_MS 800U
#define BASKETBALL_LEG_MAX_MS 2200U
/* basketball_hoop_at() is exact for any time from this long before the latest tick onwards. */
#define BASKETBALL_COMMIT_LAG_MS 1000U

typedef enum { BASKETBALL_OFF, BASKETBALL_PLAY, BASKETBALL_RESULT } basketball_phase_t;

/* The hoop moves in straight legs. A leg ends when it reaches a wall (bounce) or when its random
 * duration expires (new random speed, 50% chance to turn around). Every leg boundary is an integer
 * millisecond that depends only on the seed, so the position is a pure function of time: it never
 * depends on how often the game is ticked, and the future can be read ahead. */
typedef struct {
    uint64_t leg_ms, redraw_ms;
    float x0, v; /* hoop centre at leg_ms and its signed speed in px/s */
    uint32_t random;
} basketball_path_t;

typedef struct {
    basketball_phase_t phase;
    uint64_t started_ms, ready_ms, shot_ms;
    uint32_t shots, made;
    bool in_flight, last_hit;
    float rim_x; /* hoop centre at the moment the last ball reached the rim */
    basketball_path_t path;
} basketball_game_t;

typedef struct {
    basketball_phase_t phase;
    unsigned seconds_left, speed_pps;
    uint32_t shots, made, score;
    float hoop_x, ball_x, ball_y;
    bool in_flight, reached_rim, last_hit;
} basketball_view_t;

/* All timestamps are monotonic milliseconds. Core has no ESP/LVGL dependency. */
void basketball_start(basketball_game_t *game, uint64_t now_ms, uint32_t seed);
void basketball_stop(basketball_game_t *game);
void basketball_tick(basketball_game_t *game, uint64_t now_ms);
/* Accepts a shot at now_ms. The outcome is decided here from the hoop's predetermined path at
 * now_ms + BASKETBALL_RIM_MS, but the view only reveals it once the ball reaches the rim. */
bool basketball_shoot(basketball_game_t *game, uint64_t now_ms);
void basketball_snapshot(const basketball_game_t *game, uint64_t now_ms, basketball_view_t *view);
/* Hoop centre at any time (also in the future) along the predetermined path. */
float basketball_hoop_at(const basketball_game_t *game, uint64_t at_ms);
/* Horizontal pan target in tenths of a degree, positive toward screen right. */
int basketball_pan(const basketball_game_t *game, uint64_t now_ms, int limit_deg, int speed_dps);
