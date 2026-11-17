#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 钓鱼: cast, wait for a bite, press when the float goes under, then fight the fish. The fish pulls in a rhythm
 * (the servo shakes with it); each pull asks for a press on the beat (TAP) or a press-and-hold that has to be let go
 * at the right moment (HOLD). A bigger fish asks for more pulls, tighter timing and shakes harder; every miss costs
 * a life of the line, and the line breaks on the last. Pure C, no ESP/LVGL dependency; time only comes from the
 * timestamps passed in, and a stall in the caller pauses the game instead of failing the player. */
#define FS_TIERS 5
#define FS_LIVES 3
#define FS_STALL_MS 100U /* the clock never advances further than this in one update */
#define FS_CAST_MS 700U
#define FS_LAND_MS 1100U
#define FS_LOST_MS 1800U
#define FS_RESULT_MIN_MS 700U /* the result stays this long before a press casts again */
#define FS_NIBBLES 2
#define FS_WAIT_MIN_MS 1800U
#define FS_WAIT_MAX_MS 4200U
#define FS_FISH_X_MIN 66
#define FS_FISH_X_MAX 148
#define FS_FISH_Y_MIN 30
#define FS_FISH_Y_MAX 54

typedef enum { FS_IDLE, FS_CAST, FS_WAIT, FS_BITE, FS_FIGHT, FS_LAND, FS_RESULT, FS_LOST } fs_phase_t;
typedef enum { FP_TAP, FP_HOLD } fs_prompt_kind_t;
typedef enum { FP_NONE, FP_APPROACH, FP_HELD, FP_DONE } fs_prompt_state_t;
typedef enum { FR_NONE, FR_PERFECT, FR_GOOD, FR_EARLY, FR_LATE } fs_result_t;

typedef struct {
    const char *name;
    uint8_t len;          /* drawing length in px */
    uint8_t need;         /* successful pulls to land it */
    uint16_t win_ms;      /* half width of the timing window */
    uint16_t approach_ms; /* how long the ring takes to close in */
    uint16_t pause_ms;    /* breathing space between pulls */
    uint8_t hold_pct;     /* share of the pulls that are press-and-hold */
    uint16_t hold_min_ms, hold_max_ms;
    uint8_t amp_deg;      /* servo swing of a pull */
    uint16_t half_ms;     /* servo half period */
    uint8_t weight;       /* how often this one bites */
    uint32_t min_g, max_g;
} fs_tier_t;

typedef struct {
    uint8_t kind, state;
    uint16_t win_ms, hold_ms, approach_ms;
    uint32_t start_ms, target_ms, res_ms; /* on the fight clock */
    uint8_t press_quality;                /* quality of the press of a HOLD, kept until its release */
} fs_prompt_t;

typedef struct {
    uint8_t phase, tier, need, done, lives, combo, last_result, nibbles, shake_pending;
    uint8_t shake_amp;
    uint16_t shake_half, shake_dur;
    uint32_t rng, now_ms, phase_ms, weight_g;
    uint32_t wait_ms, nibble_at_ms, bite_ms, next_start_ms, result_age_ms, move_carry_ms, shake_seq;
    uint32_t casts, caught, lost, perfect, best_g, missed_bites;
    uint8_t best_tier;
    int16_t fx, fy, gx, gy; /* the fish, 1/16 px, and where it is heading */
    uint16_t move_wait;
    fs_prompt_t p;
} fs_t;

void fs_init(fs_t *f, uint32_t seed, uint32_t now_ms);
const fs_tier_t *fs_tier(unsigned tier);
/* Brings the clock up to now_ms (never more than FS_STALL_MS per call) and runs timeouts. */
void fs_update(fs_t *f, uint32_t now_ms);
/* The button went down / up. Both update the clock first. */
void fs_press(fs_t *f, uint32_t now_ms);
void fs_release(fs_t *f, uint32_t now_ms);
/* The latest servo request the game wants, if any (consumed): swing in degrees, half period and length in ms. */
bool fs_take_shake(fs_t *f, int *amp_deg, unsigned *half_ms, unsigned *duration_ms);
/* Milliseconds until the beat of the current pull (negative once it has passed); only meaningful in a FIGHT
 * while a pull is on screen. */
int fs_to_target(const fs_t *f);
/* How long a pull's window still has to run, for the on-screen ring: 0..256 closed in. */
int fs_ring(const fs_t *f);
/* How far a HOLD has been held, 0..256, from the beat to the moment to let go. */
int fs_hold_fill(const fs_t *f);
int fs_fish_x(const fs_t *f);
int fs_fish_y(const fs_t *f);
