#pragma once
#include "ld2450.h"
#include <stdint.h>

#define RADAR_VIEW_WIDTH 108
#define RADAR_VIEW_HEIGHT 64

/* RGB565 native-endian image as seen by the person standing in front of the machine: the radar at the top centre,
 * distance growing downwards, and X mirrored (the sensor's +X is the viewer's left), so moving to your right moves
 * the marker to the right of the screen. */
bool radar_view_project(const ld2450_target_t *target, unsigned range_mm, int *x, int *y);
void radar_view_draw(uint16_t *pixels, const ld2450_frame_t *raw, bool online,
                     unsigned range_mm, unsigned phase_ms);
uint16_t radar_target_color(unsigned index);
