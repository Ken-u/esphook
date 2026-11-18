#pragma once
/* Integer maths for apps and their pure game cores.
 *
 * Header-only, allocation free and free of LVGL/FreeRTOS, so it can be included by `*_core.c` files that are
 * compiled for the host test suite as well as by firmware code. The device has no FPU, hence everything here is
 * integer: a game that needs a root, a wave or a hash calls these instead of growing a private copy. */
#include <stdint.h>

static inline int app_abs(int v) { return v < 0 ? -v : v; }
static inline int app_min(int a, int b) { return a < b ? a : b; }
static inline int app_max(int a, int b) { return a > b ? a : b; }
/* v limited to lo..hi (lo <= hi). */
static inline int app_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* a + (b - a) * num / den, truncating toward zero. den must not be 0 and (b - a) * num must fit in an int. */
static inline int app_lerp(int a, int b, int num, int den) { return a + (b - a) * num / den; }

/* Saturating add that never wraps: v + d, held at `max`. Used for "milliseconds since ..." counters that may sit
 * idle for hours. */
static inline uint32_t app_sat_add(uint32_t v, uint32_t d, uint32_t max)
{
    return d >= max || v >= max - d ? max : v + d;
}
/* Upper bound of every `*_age_ms` counter: long enough for any animation, short enough to never overflow. */
#define APP_AGE_MAX 60000U
static inline uint32_t app_age_add(uint32_t age_ms, uint32_t dt_ms) { return app_sat_add(age_ms, dt_ms, APP_AGE_MAX); }

/* Square root rounded down. */
static inline uint32_t app_isqrt(uint32_t v)
{
    uint32_t root = 0, bit = 1U << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= root + bit) { v -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}

/* Sine look-alikes from a parabola, 256 phase steps per period (any unsigned phase wraps). The error is within
 * ~6%, which is invisible for swaying plants, strafing enemies and bobbing floats. */
static inline int app_wave127(unsigned phase) /* -127 .. 127 */
{
    const int x = (int)(phase & 127U), v = x * (128 - x) * 127 / 4096;
    return (phase & 128U) ? -v : v;
}
static inline int app_sin256(unsigned phase) /* -256 .. 256 */
{
    const int x = (int)(phase & 127U), v = x * (128 - x) / 16;
    return (phase & 128U) ? -v : v;
}

/* The angle whose tangent is num/den, in 256ths of a turn (45 degrees is 32, 90 degrees is 64): what turn makes an object
 * at a sideways offset `num` at distance `den` face the viewer. den must be positive; the result is -64..64 and the error
 * stays within a step (about 1.4 degrees). */
static inline int app_atan256(int num, int den)
{
    if (den <= 0) return num < 0 ? -64 : num > 0 ? 64 : 0;
    const int sign = num < 0 ? -1 : 1, a = num < 0 ? -num : num;
    const int64_t big = a > den ? a : den, small = a > den ? den : a;
    const int64_t t = small * 256 / big; /* 0..256: the shorter side over the longer one */
    int turn = (int)((t * (8192 + 2847 * (256 - t) / 256) + 32768) >> 16); /* atan(t) = t (pi/4 + 0.273 (1 - t)), rounded */
    if (a > den) turn = 64 - turn;
    return sign * turn;
}

/* A well mixed 32 bit hash (murmur finalizer): value-in/value-out noise for scenery, track shapes and salts. */
static inline uint32_t app_hash32(uint32_t z)
{
    z += 0x9E3779B9U; z ^= z >> 16; z *= 0x85EBCA6BU; z ^= z >> 13; z *= 0xC2B2AE35U;
    return z ^ (z >> 16);
}

/* Fixed-step accumulator for simulation cores.
 *
 * Adds `dt_ms` (capped to `max_advance_ms` when that is non-zero, so a stalled frame cannot trigger a burst) to
 * `*carry_ms` and returns how many whole `step_ms` steps are now due; the remainder stays in `*carry_ms`.
 *
 *     for (unsigned n = app_step_take(&g->carry_ms, dt_ms, MY_STEP_MS, MY_MAX_ADVANCE_MS); n; --n) step(g);
 *
 * Because the simulation advances in fixed steps, a replay with the same inputs is bit-for-bit identical no matter
 * how the real frames were sliced. */
static inline unsigned app_step_take(uint32_t *carry_ms, uint32_t dt_ms, uint32_t step_ms, uint32_t max_advance_ms)
{
    if (!step_ms) return 0U;
    if (max_advance_ms && dt_ms > max_advance_ms) dt_ms = max_advance_ms;
    const uint32_t total = *carry_ms + dt_ms;
    *carry_ms = total % step_ms;
    return (unsigned)(total / step_ms);
}
