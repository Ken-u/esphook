#pragma once
/* Deterministic pseudo random numbers for apps and their pure game cores.
 *
 * The state is a plain uint32_t inside the app's own struct, so a seeded game replays identically in a host test
 * and on the board. This is xorshift32 (13/17/5): quick, tiny and good enough for games - never use it for
 * anything that must be unpredictable. (`app_random()` in app_services.h is the hardware entropy source: use it
 * once, to pick the seed.) */
#include <stdbool.h>
#include <stdint.h>

/* Fallback state: xorshift32 is stuck at 0 forever, so a zero state is replaced by this constant. */
#define APP_RAND_FALLBACK 0x9E3779B9U

/* Turns an arbitrary seed (a clock, a score, 0) into a usable non-zero state. */
static inline uint32_t app_rand_seed(uint32_t seed) { return seed ? seed : APP_RAND_FALLBACK; }

/* Next 32 random bits; advances *state. */
static inline uint32_t app_rand_next(uint32_t *state)
{
    uint32_t x = *state ? *state : APP_RAND_FALLBACK;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}

/* Uniform-ish integer in lo..hi inclusive (lo <= hi); the tiny modulo bias is irrelevant for games. */
static inline int app_rand_range(uint32_t *state, int lo, int hi)
{
    return lo + (int)(app_rand_next(state) % (uint32_t)(hi - lo + 1));
}

/* True with probability percent/100. */
static inline bool app_rand_chance(uint32_t *state, unsigned percent) { return app_rand_next(state) % 100U < percent; }
