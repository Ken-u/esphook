#pragma once
#include "basketball_game.h"
#include "esp_err.h"

esp_err_t basketball_runtime_init(void);
void basketball_post_press(uint64_t now_ms);
/* The display task is the sole game-state writer. */
void basketball_runtime_tick(uint64_t now_ms);
void basketball_get_view(basketball_view_t *view);
bool basketball_command(const char *line);
