#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 僵尸围城: a first-person stand against waves of zombies. You stand still and turn on the spot (the radar, or the
 * K2/K3 keys, turns the view), the gun fires straight ahead of the crosshair while the key is held, and every zombie that
 * reaches you costs one of three lives. Waves are bigger and faster as they go; the lost life comes back between
 * waves. Pure C, no ESP/LVGL dependency, fixed 20 ms steps, so the result never depends on how often it is ticked.
 * World units: x is the bearing (ZB_UNITS_PX of them per screen pixel, so the view turns by whole pixels), z is the
 * distance; a zombie bites when it gets to z = ZB_ZN. */
#define ZB_STEP_MS 20U
#define ZB_MAX_ADVANCE_MS 200U /* a longer gap between calls is a pause, not a backlog */
#define ZB_ZN 130
#define ZB_ZF 1500
#define ZB_X_MAX 1000          /* the view turns between -ZB_X_MAX and +ZB_X_MAX */
#define ZB_LANE 1050           /* zombies come from within this much of straight ahead */
#define ZB_UNITS_PX 4
#define ZB_FOC 100
#define ZB_HP 3
#define ZB_Z 12
#define ZB_FX 12
#define ZB_MAG 8
#define ZB_FIRE_STEPS 11       /* between shots while firing: 220 ms */
#define ZB_RELOAD_STEPS 62     /* the magazine is empty: 1.24 s */
#define ZB_CLOSE_Z 420         /* inside this the shot hits twice as hard */
#define ZB_DYING_MS 380U
#define ZB_INVULN_STEPS 60     /* after a bite */
#define ZB_INTRO_MS 1900U
#define ZB_LOST_STEPS 70
#define ZB_CAM_STEP 44         /* the most the view turns per step (2200 per second) */
#define ZB_STREAK_STEPS 150
#define ZB_AGE_MAX 60000U

typedef enum { ZB_TITLE, ZB_INTRO, ZB_PLAY, ZB_DYING, ZB_OVER } zb_phase_t;
typedef enum { ZK_WALKER, ZK_RUNNER, ZK_BRUTE, ZK_KINDS } zb_kind_t;
typedef enum { ZX_BLOOD, ZX_BITE, ZX_SPLAT } zb_fx_kind_t;

typedef struct {
    int16_t x, z, x0;
    uint16_t age, dying_ms, stun;
    uint8_t alive, kind, hp, flash, seed, amp, zf;
} zb_z_t;
typedef struct { int16_t x, z; uint8_t alive, kind, age; } zb_fx_t;
typedef struct {
    zb_z_t z[ZB_Z];
    zb_fx_t fx[ZB_FX];
    uint32_t rng, clock_ms, carry_ms, play_steps, score, best, kills, shots_fired, hits;
    uint32_t shot_age_ms, hitmark_age_ms, bite_age_ms, kill_age_ms, over_age_ms, phase_ms;
    int16_t cam_x, target_x;
    uint16_t cooldown, reload, idle_steps, invuln, streak_steps, dying, spawn_wait;
    uint8_t phase, hp, firing, hit_pending, ammo, wave, streak, to_spawn, wave_total, new_best;
    int16_t wave_bonus;
} zb_t;

/* What each kind is: the hit points, the speed (z per step) and the points a kill is worth, and half its width. */
typedef struct { uint8_t hp, speed; uint16_t points, half_w, height; } zb_info_t;
const zb_info_t *zb_info(unsigned kind);

void zb_init(zb_t *s, uint32_t seed);
void zb_start(zb_t *s); /* from the title or the game over screen */
/* steer: -256 (hard left) .. +256 (hard right), where the view wants to be turned. */
void zb_steer(zb_t *s, int steer);
/* The fire key: down fires at once and keeps firing while held; up stops. */
void zb_fire(zb_t *s, bool down);
void zb_advance(zb_t *s, uint32_t dt_ms);
/* True once for every life lost since the last call. */
bool zb_take_hit(zb_t *s);
unsigned zb_alive(const zb_t *s);
/* Zombies still to come in this wave, on screen or not yet spawned. */
unsigned zb_remaining(const zb_t *s);
/* Whether the shot at the crosshair now would hit zombie `i` (the test the gun itself uses). */
bool zb_on_target(const zb_t *s, unsigned i);
