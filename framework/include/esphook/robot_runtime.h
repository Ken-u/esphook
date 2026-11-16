#pragma once
#include "esp_err.h"
#include "robot_follow.h"

typedef struct {
    robot_state_t state;
    bool armed, following, hardware_fault, radar_ready, servo_ready;
    float angle_deg, target_deg;
    int x_mm, y_mm;
    uint32_t frames, rejected;
    ld2450_frame_t raw;
    uint32_t radar_age_ms;
    bool radar_online;
    robot_config_t config;
} robot_status_t;

esp_err_t robot_start(void);
void robot_get_status(robot_status_t *status);
/* True if this is a robot command (including an invalid robot command). */
bool robot_command(const char *line);

/* Port adapter: action values are app_servo_action_t; enqueue only. */
bool robot_servo_request(unsigned action, int value);
