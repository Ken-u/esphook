#pragma once
/* Servo "feel" for games: short shakes (a hit, a fish pulling) and a base angle the servo follows (a steering
 * wheel). The effect is a pure function of the timestamps it is given; the glue at the bottom sends it through the
 * public servo service, so the motion service keeps enforcing calibration, limits and its slew rate.
 *
 * The servo is never armed here on its own: haptic_arm() is meant to be called from an explicit confirmation by the
 * player (a long press), and everything stays silent while the servo is missing, disarmed or faulted. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "app_services.h"

#define HAPTIC_DEFAULT_SPEED_DPS 35
#define HAPTIC_MIN_GAP_MS 60U   /* at most one angle request per this long: the service queue is shared and small */
#define HAPTIC_MAX_HALF_MS 700U

typedef struct {
    bool shaking, sent_valid;
    uint64_t start_ms, end_ms, last_tx_ms;
    unsigned half_ms;
    int amp_x10;  /* peak amplitude of the running shake, 0.1 degree */
    int base_x10; /* centre of the motion, 0.1 degree */
    int sent_deg; /* the last whole-degree angle the servo was asked for */
} haptic_t;

/* A disarmed servo is assumed centred, which is where arming leaves it. */
static inline void haptic_init(haptic_t *h)
{
    memset(h, 0, sizeof *h);
    h->sent_valid = true;
}
static inline int haptic_amp_now(const haptic_t *h, uint64_t now)
{
    if (!h->shaking || now >= h->end_ms) return 0;
    return (int)((int64_t)h->amp_x10 * (int64_t)(h->end_ms - now) / (int64_t)(h->end_ms - h->start_ms));
}
/* Shake around the base angle: swing +-amp_deg every half_ms, fading out over duration_ms. A swing takes time at the
 * service's slew rate, so the half period is stretched until the servo can actually reach the amplitude. A weaker
 * shake never cuts a stronger one short. */
static inline void haptic_shake(haptic_t *h, uint64_t now_ms, int amp_deg, unsigned half_ms, unsigned duration_ms,
                                int speed_dps)
{
    if (amp_deg < 1 || duration_ms == 0) return;
    if (amp_deg > 30) amp_deg = 30;
    if (speed_dps <= 0) speed_dps = HAPTIC_DEFAULT_SPEED_DPS;
    unsigned need = (unsigned)(2 * amp_deg * 1000 / speed_dps) + 15U;
    if (half_ms < need) half_ms = need;
    if (half_ms < 60U) half_ms = 60U;
    if (half_ms > HAPTIC_MAX_HALF_MS) half_ms = HAPTIC_MAX_HALF_MS;
    if (haptic_amp_now(h, now_ms) > amp_deg * 10) return;
    h->shaking = true;
    h->start_ms = now_ms;
    h->end_ms = now_ms + duration_ms;
    h->half_ms = half_ms;
    h->amp_x10 = amp_deg * 10;
}
static inline void haptic_base(haptic_t *h, int angle_x10) { h->base_x10 = angle_x10; }
static inline void haptic_stop(haptic_t *h)
{
    h->shaking = false;
    h->base_x10 = 0;
}
/* What the servo should be told now, if anything: true plus the whole-degree angle. */
static inline bool haptic_step(haptic_t *h, uint64_t now_ms, int limit_deg, int speed_dps, int *angle_deg)
{
    (void)speed_dps;
    if (limit_deg < 2) return false;
    int target = h->base_x10;
    if (h->shaking) {
        if (now_ms >= h->end_ms) h->shaking = false;
        else {
            unsigned step = (unsigned)((now_ms - h->start_ms) / h->half_ms);
            int amp = haptic_amp_now(h, now_ms);
            target += (step & 1U) ? -amp : amp;
        }
    }
    const int reach = (limit_deg - 1) * 10; /* stay a degree away from the configured stops */
    if (target > reach) target = reach;
    if (target < -reach) target = -reach;
    const int deg = (target + (target >= 0 ? 5 : -5)) / 10; /* the servo takes whole degrees */
    if (h->sent_valid && deg == h->sent_deg) return false;
    if (h->last_tx_ms && now_ms - h->last_tx_ms < HAPTIC_MIN_GAP_MS) return false;
    h->last_tx_ms = now_ms ? now_ms : 1U;
    h->sent_deg = deg;
    h->sent_valid = true;
    *angle_deg = deg;
    return true;
}

/* ---- glue to the public servo service ---------------------------------------------------------------------- */
typedef enum { HAPTIC_NONE, HAPTIC_READY, HAPTIC_ARMED, HAPTIC_FAULT } haptic_state_t;
/* NONE: no servo driver (screen-only hardware). READY: present but disarmed. */
static inline haptic_state_t haptic_state(void)
{
    app_servo_t s;
    app_servo_read(&s);
    if (!s.available) return HAPTIC_NONE;
    if (s.fault) return HAPTIC_FAULT;
    return s.armed ? HAPTIC_ARMED : HAPTIC_READY;
}
/* Arms the servo (it centres first). Only call this from a deliberate confirmation by the player. */
static inline bool haptic_arm(app_context_t *ctx, haptic_t *h)
{
    app_servo_t s;
    app_servo_read(&s);
    if (!s.available || s.fault) return false;
    h->sent_valid = true; /* arming centres the servo first */
    h->sent_deg = 0;
    return s.armed || app_servo_request(ctx, APP_SERVO_ARM, 0);
}
/* Call every frame: sends the next angle when one is due. */
static inline void haptic_pump(app_context_t *ctx, haptic_t *h)
{
    app_servo_t s;
    app_servo_read(&s);
    if (!s.available || !s.armed || s.fault) {
        h->sent_valid = true; /* the next arm starts from the centre */
        h->sent_deg = 0;
        return;
    }
    int angle;
    if (haptic_step(h, ctx->now_ms, s.limit_deg, s.speed_dps, &angle) &&
        !app_servo_request(ctx, APP_SERVO_ANGLE, angle))
        h->sent_valid = false; /* the queue was full or the angle refused: try again later */
}
