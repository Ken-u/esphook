#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LD2450_TARGETS 3
#define LD2450_FRAME_SIZE 30

typedef struct {
    int16_t x_mm, y_mm, speed_cms;
    uint16_t resolution_mm;
    bool valid;
} ld2450_target_t;
typedef struct { ld2450_target_t targets[LD2450_TARGETS]; } ld2450_frame_t;
typedef struct {
    uint8_t bytes[LD2450_FRAME_SIZE];
    size_t used;
    uint32_t frames, rejected;
} ld2450_parser_t;

/* Feed every byte; true means one complete, structurally valid report. */
bool ld2450_feed(ld2450_parser_t *parser, uint8_t byte, ld2450_frame_t *frame);
