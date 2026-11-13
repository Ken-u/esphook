#pragma once
#include "ld2450.h"
#include <stdint.h>

#define RADAR_VIEW_WIDTH 108
#define RADAR_VIEW_HEIGHT 64

/* RGB565 native-endian image; origin bottom centre, +X right, +Y forward. */
bool radar_view_project(const ld2450_target_t *target, unsigned range_mm, int *x, int *y);
void radar_view_draw(uint16_t *pixels, const ld2450_frame_t *raw, bool online,
                     unsigned range_mm, unsigned phase_ms);
uint16_t radar_target_color(unsigned index);
