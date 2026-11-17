#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 跳一跳: hold the key and the little pawn squats, let go and it jumps as far as it was charged, onto the next box.
 * The boxes run along the two diagonals of an isometric floor (up and to the right, or up and to the left), so a
 * path of them is a zigzag. Landing in the middle of a box counts double, and the bonus grows with every one in a
 * row. Positions are floor coordinates in units, kept in sixteenths; a box covers a square of 2 * hw units around its
 * centre. The pawn stays exactly where it landed, and every jump is aimed from there at the middle of the next box,
 * so only its length can be wrong. Pure C and driven by
 * timestamps, so a bot can play it in the tests. */
#define JP_PLATS 6U
#define JP_MIN_D16 48                /* a tap still hops three units */
#define JP_RATE_D16_PER_S 448        /* 28 units a second of holding */
#define JP_MAX_CHARGE_MS 1400U       /* held longer, it jumps by itself */
#define JP_PERFECT_D16 24            /* within 1.5 units of the middle */
#define JP_LAND_MS 260U
#define JP_FALL_MS 900U
#define JP_RESTART_MS 700U

typedef enum { JP_TITLE, JP_READY, JP_CHARGE, JP_FLY, JP_LAND, JP_FALL, JP_OVER } jp_phase_t;
typedef enum { JP_NONE, JP_STAYED, JP_GOOD, JP_PERFECT, JP_MISSED } jp_result_t;
typedef struct { int16_t a, b; uint8_t hw, color; } jp_plat_t; /* centre on the two floor axes; hw = 0 means none */
typedef struct {
    uint32_t rng, phase_ms, charge_ms, score, best, jumps, perfects, frames;
    jp_plat_t plat[JP_PLATS];
    int32_t pa16, pb16; /* where the pawn stands */
    int32_t la16, lb16; /* where the current jump ends */
    int16_t dist16, popup, ua, ub; /* the jump's direction as a unit vector in 1/256 */
    uint32_t popup_ms; /* how long ago the last bonus was earned; 0xFFFFFFFF = none */
    uint16_t fly_ms;
    uint8_t phase, cur, combo, result, new_best;
} jp_t;

void jp_init(jp_t *g, uint32_t seed);
void jp_press(jp_t *g);
void jp_release(jp_t *g);
void jp_advance(jp_t *g, uint32_t dt_ms);
/* The boxes: `which` is 0 for the one the pawn stands on, 1 the next, 2 the one after, -1 and -2 behind. */
const jp_plat_t *jp_plat(const jp_t *g, int which);
/* Floor units from the pawn to the middle of the next box (x16): the jump is always aimed at it. */
int jp_next_distance16(const jp_t *g);
/* The hold, in milliseconds, that jumps `dist16`. */
unsigned jp_charge_for(int dist16);
/* Where the pawn is drawn: floor position (x16) and height above the floor in pixels (x16, negative while falling). */
void jp_pawn(const jp_t *g, int *a16, int *b16, int *lift16);
/* How squashed the pawn is, 0..256 (charging, and a moment after landing). */
unsigned jp_squash(const jp_t *g);
/* The largest half-size of a new box at this score. */
int jp_hw_max(unsigned score);
