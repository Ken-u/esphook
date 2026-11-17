#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esphook/app_time.h"

/* 闹钟 and 整点报时: what is due when, with no screen, servo or storage in sight. The app asks al_due() with the wall
 * clock a few times a second (also from the framework's wake poll while it is not on screen) and calls al_fire() when
 * it starts to ring, so the same minute never rings twice. */
#define AL_VERSION 1U
#define AL_SNOOZE_S 300U       /* 贪睡: ring again after five minutes */
#define AL_MAX_SNOOZES 3U
#define AL_RING_MS 60000U      /* an unanswered alarm rings for a minute, then snoozes */
#define AL_CHIME_WINDOW_S 15U  /* a chime is only for the first seconds of the hour: opening the app later is silent */
#define AL_BEAT_MS 700U        /* one stroke of the hourly chime */

typedef enum { AL_ONCE, AL_DAILY, AL_WEEKDAYS, AL_REPEATS } al_repeat_t;     /* 一次 每天 工作日 */
typedef enum { AL_RANGE_ALL, AL_RANGE_DAY, AL_RANGE_OFFICE, AL_RANGES } al_range_t; /* 全天 8-22点 9-18点 */
typedef enum { AL_NONE, AL_ALARM, AL_CHIME } al_kind_t;

/* Saved as a blob under the app's storage key: keep it small and fixed. */
typedef struct {
    uint8_t version, alarm_on, hour, minute, repeat, chime_on, range, servo_on;
} al_config_t;
/* Only in RAM: when each thing last rang, and a pending snooze. Keys are minutes of local time since 1970, plus one. */
typedef struct {
    uint32_t alarm_key, chime_key, snooze_epoch;
    uint8_t snoozes;
} al_sched_t;

void al_defaults(al_config_t *c);
/* False for a blob that is not ours (wrong version or a field out of range). */
bool al_config_valid(const al_config_t *c);
const char *al_repeat_name(unsigned repeat);
const char *al_range_name(unsigned range);
bool al_repeat_has(unsigned repeat, unsigned weekday); /* weekday 0 = Sunday */
bool al_range_has(unsigned range, unsigned hour);
unsigned al_beats(unsigned hour);                       /* 12-hour strokes: 0 and 12 strike twelve */
uint32_t al_minute_key(const app_time_t *t);
/* What should start ringing now: an alarm (its minute, or a snooze that has run out) wins over a chime. */
al_kind_t al_due(const al_config_t *c, const al_sched_t *s, const app_time_t *t);
/* Records that `kind` has started, so al_due() goes quiet. Returns true when the configuration changed (an alarm set
 * for once switches itself off) and should be saved. */
bool al_fire(al_config_t *c, al_sched_t *s, al_kind_t kind, const app_time_t *t);
/* 贪睡: false when it has used up its snoozes (the alarm is then over). */
bool al_snooze(al_sched_t *s, const app_time_t *t);
void al_dismiss(al_sched_t *s);
/* Minutes until the alarm rings next (1 or more), or -1 when it is off or there is no clock. */
int al_minutes_until(const al_config_t *c, const app_time_t *t);
