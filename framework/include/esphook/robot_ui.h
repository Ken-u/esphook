#pragma once
#include "esp_err.h"
#include "robot_ui_protocol.h"

/* Shared USB/HTTP display input. Applying a view never calls servo control. */
esp_err_t robot_ui_submit(const char *json, size_t len, char *error, size_t error_len);
void robot_ui_get(robot_ui_t *view);
bool robot_ui_set_radar_range(unsigned range_mm);
bool robot_ui_command(const char *line);
