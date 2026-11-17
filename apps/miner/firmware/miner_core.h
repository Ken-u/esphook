#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 黄金矿工: a claw swings under the miner, one key fires it, and whatever it touches is hauled back: gold, rocks,
 * diamonds, mystery bags and dynamite barrels. Pure C on a 160 x 64 field, driven by timestamps, so the tests can
 * play whole levels with a bot that aims by the numbers. Coordinates are canvas pixels; the claw's length is kept in
 * sixteenths of a pixel. */
#define MN_W 160
#define MN_H 64
#define MN_ORIGIN_X 80
#define MN_ORIGIN_Y 12
#define MN_MAX_ITEMS 14U
#define MN_SWING_MS 2600U     /* one full swing there and back */
#define MN_EXTEND_PPS 120     /* pixels a second */
#define MN_EMPTY_PPS 150
#define MN_INTRO_MS 1700U
#define MN_RESULT_MS 2400U
#define MN_BLAST_RADIUS 26
#define MN_MAX_DYNAMITE 3U
#define MN_MAX_LEVEL 20U

typedef enum { MN_GOLD_S, MN_GOLD_M, MN_GOLD_L, MN_STONE_S, MN_STONE_L, MN_DIAMOND, MN_BAG, MN_TNT, MN_KINDS } mn_kind_t;
typedef enum { MN_TITLE, MN_INTRO, MN_PLAY, MN_CLEAR, MN_OVER } mn_phase_t;
typedef enum { MN_SWING, MN_EXTEND, MN_RETRACT } mn_claw_t;

typedef struct { uint8_t radius; uint8_t pull_pps; int16_t value; } mn_info_t;
typedef struct { int16_t x, y; uint8_t kind, alive; } mn_item_t;
typedef struct {
    uint32_t rng, phase_ms, swing_ms, time_ms, score, level_base, goal, best, caught, frames;
    mn_item_t items[MN_MAX_ITEMS];
    int32_t len16;
    int16_t pop_value, pop_x, pop_y, boom_x, boom_y;
    uint32_t pop_ms, boom_ms; /* how long ago: 0xFFFFFFFF = never */
    uint16_t level_total, rem; /* what the level's items are worth, for the goal; the sub-pixel remainder of the claw */
    uint8_t phase, claw, count, level, dynamite, hooked_kind, pop_kind, timeup, new_best;
    int8_t hooked;
} mn_t;

const mn_info_t *mn_info(unsigned kind);
void mn_init(mn_t *g, uint32_t seed);
/* One key: starts the game / skips a result screen, fires the claw while it swings, throws dynamite at a load. */
void mn_press(mn_t *g);
void mn_advance(mn_t *g, uint32_t dt_ms);
/* The direction of the rope as a unit vector in 1/256 (dy is always positive: downwards). */
void mn_dir(const mn_t *g, int *dx256, int *dy256);
void mn_tip(const mn_t *g, int *x, int *y);
/* Seconds left, rounded up. */
unsigned mn_seconds(const mn_t *g);
unsigned mn_level_time_ms(unsigned level);
