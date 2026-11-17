#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 星际战机: a first-person flight through an asteroid field and a patrol of enemy ships. The ship slides left and
 * right (the radar, or the left/right keys), the cannon fires straight ahead while the key is held, and every hit on
 * the ship (an enemy bolt or a collision) costs one of three shield points. Pure C, no ESP/LVGL dependency, fixed
 * 20 ms steps, so the result never depends on how often it is ticked. World units: x is sideways (the ship moves
 * between -SS_X_MAX and +SS_X_MAX), z is the distance ahead; things hit the ship when they reach z = SS_ZN. */
#define SS_STEP_MS 20U
#define SS_MAX_ADVANCE_MS 200U /* a longer gap between calls is a pause, not a backlog */
#define SS_ZN 100              /* the plane where things reach the ship */
#define SS_ZF 1700             /* where they appear */
#define SS_X_MAX 1000
#define SS_HP 3
#define SS_OBJ 8
#define SS_SHOT 10
#define SS_BOLT 9
#define SS_FX 10
#define SS_SHIP_HALF 70   /* half width of the ship against rocks and enemies */
#define SS_BOLT_HALF 130  /* an enemy bolt hits within this of the ship's centre */
#define SS_SHOT_STEP 52   /* z per step of a cannon shot (2600 per second) */
#define SS_FIRE_STEPS 9   /* between shots while firing: 180 ms */
#define SS_INVULN_STEPS 60 /* after a hit */
#define SS_DYING_STEPS 60
#define SS_SHIP_STEP 52   /* the most the ship moves sideways per step (2600 per second) */
#define SS_HOLD_STEPS 450 /* an enemy that has not been shot after this long dives at the ship */
#define SS_AGE_MAX 60000U

typedef enum { SS_TITLE, SS_PLAY, SS_DYING, SS_OVER } ss_phase_t;
typedef enum { SO_ASTEROID, SO_ENEMY } ss_kind_t;
typedef enum { SE_ENTER, SE_HOLD, SE_DIVE } ss_enemy_state_t;
typedef enum { SX_ROCK, SX_SHIP, SX_HIT } ss_fx_kind_t;

typedef struct {
    int16_t x, z, pz; /* pz: z on the previous step, for swept hits */
    int16_t vz, r, x0, amp;
    uint16_t age, fire;
    uint8_t alive, kind, hp, state, flash, seed;
} ss_obj_t;
typedef struct { int16_t x, z, pz; uint8_t alive; } ss_shot_t;
typedef struct { int16_t x, z, vz; uint8_t alive; } ss_bolt_t;
typedef struct { int16_t x, z; uint8_t alive, kind, age, size; } ss_fx_t;
typedef struct {
    ss_obj_t obj[SS_OBJ];
    ss_shot_t shot[SS_SHOT];
    ss_bolt_t bolt[SS_BOLT];
    ss_fx_t fx[SS_FX];
    uint32_t rng, clock_ms, carry_ms, play_steps, score, best, kills, shots_fired;
    uint32_t hit_age_ms, over_age_ms;
    int16_t cam_x, target_x;
    uint16_t cooldown, invuln, dying, rock_wait, enemy_wait;
    uint8_t phase, hp, firing, gun, hit_pending;
} ss_t;

void ss_init(ss_t *s, uint32_t seed);
void ss_start(ss_t *s); /* from the title or the game over screen */
/* steer: -256 (hard left) .. +256 (hard right), where the ship wants to be. */
void ss_steer(ss_t *s, int steer);
/* The fire key: down fires at once and keeps firing while held; up stops. */
void ss_fire(ss_t *s, bool down);
void ss_advance(ss_t *s, uint32_t dt_ms);
/* True once for every point of shield lost since the last call. */
bool ss_take_hit(ss_t *s);
unsigned ss_live_objects(const ss_t *s);
/* Seconds the game has been going (for difficulty and the screen). */
unsigned ss_seconds(const ss_t *s);
