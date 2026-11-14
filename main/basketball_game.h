#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BASKETBALL_BUTTON_GPIO 6
#define BASKETBALL_DURATION_MS 120000U
#define BASKETBALL_TRIPLE_MS 800U
#define BASKETBALL_SHOT_MS 650U
#define BASKETBALL_HIT_PX 10.0f
#define BASKETBALL_MIN_SPEED 36U
#define BASKETBALL_MAX_SPEED 100U

typedef enum { BASKETBALL_OFF, BASKETBALL_PLAY, BASKETBALL_RESULT } basketball_phase_t;
typedef struct {
    basketball_phase_t phase;
    uint64_t started_ms, round_ms, shot_ms, click_first_ms;
    uint32_t random, shots, made;
    unsigned speed_pps, clicks;
    bool in_flight, last_hit;
    float shot_x;
} basketball_game_t;
typedef struct {
    basketball_phase_t phase;
    unsigned seconds_left, speed_pps;
    uint32_t shots, made, score;
    float hoop_x, ball_x, ball_y;
    bool in_flight, last_hit;
} basketball_view_t;
typedef struct {
    bool initialized, raw_pressed, stable_pressed, may_press;
    uint64_t changed_ms;
} basketball_button_t;

/* All timestamps are monotonic milliseconds. Core has no ESP/LVGL dependency. */
void basketball_start(basketball_game_t *game, uint64_t now_ms, uint32_t seed);
void basketball_stop(basketball_game_t *game);
void basketball_tick(basketball_game_t *game, uint64_t now_ms);
void basketball_press(basketball_game_t *game, uint64_t now_ms, uint32_t seed);
bool basketball_shoot(basketball_game_t *game, uint64_t now_ms);
void basketball_snapshot(const basketball_game_t *game, uint64_t now_ms, basketball_view_t *view);
/* True once per debounced press. A button held at boot must first be released. */
bool basketball_button_sample(basketball_button_t *button, bool pressed, uint64_t now_ms);
