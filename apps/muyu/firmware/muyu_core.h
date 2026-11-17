#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 电子木鱼: every knock is worth one 功德. Knocks come from the button, or by themselves in auto mode at a calm
 * one per second. The core has no ESP/LVGL dependency and keeps time only through the timestamps it is given, so
 * the result never depends on how often it is ticked. */
#define MUYU_AUTO_MS 1000U       /* auto mode: one knock this often */
#define MUYU_ROUND 108U          /* every 108th knock completes a round of the prayer beads */
#define MUYU_COUNT_MAX 99999999U /* the counter stops here; 8 digits fit the HUD */
#define MUYU_STALL_MS 3000U      /* a longer gap between ticks is a pause (the app was stopped), not a backlog */
#define MUYU_AGE_MAX 60000U
/* One knock as the mallet plays it: it swings down and lands (SWING), stays pressed into the fish (HOLD), then
 * springs back up (REBOUND). Everything that happens "on the hit" (ripple, sparks, squash, the floating 功德+1)
 * is timed from the landing, MUYU_SWING_MS after the button went down; the counter itself moves at once. */
#define MUYU_SWING_MS 100U
#define MUYU_HOLD_MS 70U
#define MUYU_REBOUND_MS 190U
#define MUYU_KNOCK_MS (MUYU_SWING_MS + MUYU_HOLD_MS + MUYU_REBOUND_MS)
#define MUYU_LIFT_REST 256 /* mallet fully raised; 0 = head touching the fish; the rebound overshoots a little */

typedef struct {
    uint64_t clock_ms, auto_ms, slot_ms[2];
    uint32_t count;
    bool auto_on, slot_used[2], slot_round[2];
} muyu_t;

typedef struct {
    uint32_t count;
    unsigned rounds;               /* completed rounds of 108 */
    bool auto_on;
    /* The two latest knocks alternate between two slots (index = knock number & 1) so a float-up label can overlap
     * the next one. Age saturates at MUYU_AGE_MAX; `round` marks the knock that completed a round. */
    unsigned age_ms[2];
    bool shown[2], round[2];
    unsigned since_knock_ms;       /* time since the latest knock, saturating; MUYU_AGE_MAX before the first */
    unsigned impact_ms;            /* time since the latest knock that has already landed; MUYU_AGE_MAX if none */
    int swing_from;                /* mallet lift the latest swing started from (not at rest when knocks come fast) */
} muyu_view_t;

void muyu_start(muyu_t *m, uint64_t now_ms);
/* Advances the clock and fires an auto knock when one is due (at most one per tick). */
void muyu_tick(muyu_t *m, uint64_t now_ms);
/* One knock; false only when the counter is already at MUYU_COUNT_MAX. */
bool muyu_knock(muyu_t *m, uint64_t now_ms);
/* Switches auto mode; turning it on makes the first auto knock one interval later. */
void muyu_toggle_auto(muyu_t *m, uint64_t now_ms);
void muyu_snapshot(const muyu_t *m, muyu_view_t *view);
/* Mallet lift `age_ms` after a knock that started from lift `from` (0..~280): accelerates down to 0 at the landing,
 * stays there, then rebounds to MUYU_LIFT_REST with a small overshoot. Pure: the same inputs give the same pose. */
int muyu_lift(unsigned age_ms, int from);
