#pragma once
#include "remote_game.h"
bool stock_transport_init(void);
bool stock_transport_command(const char *line);
void stock_transport_view(uint64_t now, remote_game_view_t *view);
void stock_transport_event(const char *action, uint64_t ms);
