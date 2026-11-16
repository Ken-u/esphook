#pragma once
#include "basketball_game.h"
#include "remote_game.h"
#include "app_input.h"
#include "esp_err.h"
esp_err_t game_runtime_init(void);
void game_post_button(bool pressed, uint64_t now_ms);
void game_runtime_tick(uint64_t now_ms);
void game_get_basketball_view(basketball_view_t *view);
void game_get_remote_view(remote_game_view_t *view);
void game_get_app_view(app_view_t *view);
bool game_command(const char *line);
