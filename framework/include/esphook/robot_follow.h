#pragma once
#include <stddef.h>
#include "ld2450.h"

typedef enum { ROBOT_IDLE, ROBOT_ACQUIRING, ROBOT_TRACKING, ROBOT_LOST, ROBOT_FAULT } robot_state_t;
#define ROBOT_FOLLOW_TRIM_MAX_DEG 25
static inline bool robot_follow_trim_valid(int degrees)
{
    return degrees >= -ROBOT_FOLLOW_TRIM_MAX_DEG && degrees <= ROBOT_FOLLOW_TRIM_MAX_DEG;
}
typedef struct {
    int center_us, limit_deg, speed_dps;
    bool reverse;
} robot_config_t;
typedef struct {
    robot_config_t config;
    robot_state_t state;
    bool have_frame, locked;
    uint64_t started_ms, frame_ms, seen_ms, step_ms, turn_since_ms;
    uint64_t track_started_ms, dodge_until_ms;
    float x_mm, y_mm, candidate_x, candidate_y;
    float bearing_last, bearing_previous;
    float aim_deg, goal_deg, target_deg, angle_deg, velocity_dps;
    unsigned candidate_hits, near_hits;
    int turn_direction, dodge_direction, trim_deg;
} robot_follow_t;

robot_config_t robot_default_config(void);
bool robot_config_valid(const robot_config_t *config);
/* "cal:<center_us>,<limit>,<speed>,<reverse>[,save]": the queue line of a calibration request. Parse fails for anything
 * malformed or outside robot_config_valid(); format returns snprintf's length. */
bool robot_cal_parse(const char *text, robot_config_t *out, bool *save);
int robot_cal_format(char *buf, size_t size, const robot_config_t *config, bool save);
void robot_follow_init(robot_follow_t *f, const robot_config_t *config, uint64_t now_ms);
/* Positive trim moves the head left from the player's perspective. Applied before limiting travel. */
void robot_follow_set_trim(robot_follow_t *f, int degrees);
void robot_follow_frame(robot_follow_t *f, const ld2450_frame_t *frame, uint64_t now_ms);
void robot_follow_tick(robot_follow_t *f, uint64_t now_ms);
float robot_follow_speed_dps(const robot_config_t *config);
float robot_follow_slew(const robot_config_t *config, float current, float *velocity, float goal, float seconds);
float robot_slew(float current, float goal, float speed_dps, float seconds);
int robot_pulse_us(const robot_config_t *config, float angle_deg);
const char *robot_state_name(robot_state_t state);
