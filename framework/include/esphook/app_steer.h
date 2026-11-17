#pragma once
/* Left/right steering for games: the radar when it sees someone, otherwise the K2 (left) and K3 (right) keys.
 *
 * Radar: the first person found in front of the sensor defines the centre (lean left or right from where you
 * started), the nearest person wins, and a person who steps out of view keeps their last steering for a moment
 * before it relaxes to the centre. Output is steer in -256 (hard left) .. +256 (hard right), smoothed.
 * The player faces the machine, so the sensor's +X (its own right) is the player's left: positions are mirrored here
 * and leaning to your own right steers right.
 * Everything is a function of the timestamps passed in. No hardware access: feed it the radar snapshot. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "ld2450.h"

#define STEER_MAX 256
#define STEER_REACH_MM 350    /* leaning this far from the centre is full lock */
#define STEER_DEADZONE_MM 25
#define STEER_HOLD_MS 1200U
#define STEER_SMOOTH_MS 70U
#define STEER_NEAR_MM 400
#define STEER_FAR_MM 3500
#define STEER_SIDE_MM 1600
#define STEER_MATCH_MM 500
#define STEER_KEY_RAMP_MS 300U
#define STEER_KEY_RELAX_MS 180U

typedef enum { STEER_NONE, STEER_RADAR, STEER_KEYS } steer_source_t;
typedef struct {
    int steer, centre_mm, target_mm, key_value;
    bool have_centre, locked, left, right;
    uint64_t seen_ms, last_ms;
    steer_source_t source;
} steer_t;

static inline void steer_init(steer_t *s) { memset(s, 0, sizeof *s); }
/* Makes the next person the radar sees the new centre. */
static inline void steer_recentre(steer_t *s) { s->have_centre = false; s->locked = false; }
static inline void steer_radar(steer_t *s, uint64_t now_ms, bool online, const ld2450_target_t *targets)
{
    if (!online || !targets) return;
    const ld2450_target_t *best = NULL;
    int best_score = 0x7FFFFFFF;
    for (int i = 0; i < LD2450_TARGETS; ++i) {
        const ld2450_target_t *t = &targets[i];
        const int px = -(int)t->x_mm; /* the player's own left/right */
        if (!t->valid || t->y_mm < STEER_NEAR_MM || t->y_mm > STEER_FAR_MM || t->x_mm < -STEER_SIDE_MM || t->x_mm > STEER_SIDE_MM)
            continue;
        int score;
        if (s->locked) {
            int d = px - s->target_mm;
            score = d < 0 ? -d : d;
            if (score > STEER_MATCH_MM) continue; /* somebody else walking by */
        } else score = t->y_mm; /* the nearest person is the player */
        if (score < best_score) { best_score = score; best = t; }
    }
    if (!best) return;
    if (!s->have_centre) { s->centre_mm = -(int)best->x_mm; s->have_centre = true; }
    s->target_mm = -(int)best->x_mm;
    s->locked = true;
    s->seen_ms = now_ms;
}
/* key: 1 = K2 (left), 2 = K3 (right). */
static inline void steer_key(steer_t *s, unsigned key, bool down)
{
    if (key == 1) s->left = down;
    else if (key == 2) s->right = down;
}
static inline void steer_release_keys(steer_t *s) { s->left = s->right = false; }
static inline int steer_tick(steer_t *s, uint64_t now_ms)
{
    unsigned dt = s->last_ms && now_ms > s->last_ms ? (unsigned)(now_ms - s->last_ms) : 0U;
    if (dt > 100U) dt = 100U;
    s->last_ms = now_ms ? now_ms : 1U;
    int raw = 0;
    if (s->locked && now_ms - s->seen_ms > STEER_HOLD_MS) s->locked = false; /* gone for good */
    if (s->locked) {
        int d = s->target_mm - s->centre_mm, a = d < 0 ? -d : d;
        if (a > STEER_DEADZONE_MM) {
            a = (a - STEER_DEADZONE_MM) * STEER_MAX / (STEER_REACH_MM - STEER_DEADZONE_MM);
            raw = d < 0 ? -a : a;
            if (raw > STEER_MAX) raw = STEER_MAX;
            if (raw < -STEER_MAX) raw = -STEER_MAX;
        }
    }
    const int dir = (s->right ? 1 : 0) - (s->left ? 1 : 0);
    if (dir) {
        s->key_value += dir * (int)(dt * STEER_MAX / STEER_KEY_RAMP_MS);
        if (s->key_value > STEER_MAX) s->key_value = STEER_MAX;
        if (s->key_value < -STEER_MAX) s->key_value = -STEER_MAX;
    } else if (s->key_value) {
        int relax = (int)(dt * STEER_MAX / STEER_KEY_RELAX_MS);
        s->key_value = s->key_value > 0 ? (s->key_value > relax ? s->key_value - relax : 0)
                                        : (s->key_value < -relax ? s->key_value + relax : 0);
    }
    if (dir || s->key_value) { /* the keys win while they are in use */
        s->steer = s->key_value;
        s->source = STEER_KEYS;
        return s->steer;
    }
    s->source = s->locked ? STEER_RADAR : STEER_NONE;
    s->steer += (raw - s->steer) * (int)dt / (int)(dt + STEER_SMOOTH_MS);
    if (raw == 0 && s->steer > -5 && s->steer < 5) s->steer = 0; /* settle on the exact centre */
    return s->steer;
}
