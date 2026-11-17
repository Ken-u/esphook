#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esphook/app_alarm.h"

/* 闹钟 and 整点报时: what the player chose, and the registry entries (framework/include/esphook/app_alarm.h) that make
 * it happen. Nothing in here reads a clock or knows about the screen: the framework notices when an entry is due and
 * opens this app, which then only shows the ring and answers it. */
#define AL_VERSION 1U
#define AL_SNOOZE_S 300U       /* 贪睡: ring again after five minutes */
#define AL_MAX_SNOOZES 3U
#define AL_RING_MS 60000U      /* an unanswered alarm rings for a minute in the app, then snoozes */
#define AL_ALARM_WINDOW_S 60U  /* a clock alarm that is missed by more than its own minute is not made up for */
#define AL_CHIME_WINDOW_S 15U  /* a chime is only for the first seconds of the hour: opening the app later is silent */
#define AL_BEAT_MS 700U        /* one stroke of the hourly chime */
#define AL_TAG_ALARM 1U        /* app_alarm_spec_t.tag: which of this app's two entries a ring belongs to */
#define AL_TAG_CHIME 2U

typedef enum { AL_ONCE, AL_DAILY, AL_WEEKDAYS, AL_REPEATS } al_repeat_t;     /* 一次 每天 工作日 */
typedef enum { AL_RANGE_ALL, AL_RANGE_DAY, AL_RANGE_OFFICE, AL_RANGES } al_range_t; /* 全天 8-22点 9-18点 */
typedef enum { AL_NONE, AL_ALARM, AL_CHIME } al_kind_t;

/* Saved as a blob under the app's storage key: keep it small and fixed. */
typedef struct {
    uint8_t version, alarm_on, hour, minute, repeat, chime_on, range, servo_on;
} al_config_t;

void al_defaults(al_config_t *c);
/* False for a blob that is not ours (wrong version or a field out of range). */
bool al_config_valid(const al_config_t *c);
const char *al_repeat_name(unsigned repeat);
const char *al_range_name(unsigned range);
bool al_repeat_has(unsigned repeat, unsigned weekday); /* weekday 0 = Sunday */
bool al_range_has(unsigned range, unsigned hour);
unsigned al_beats(unsigned hour);                       /* 12-hour strokes: 0 and 12 strike twelve */

/* The settings as registry entries. Register once with app_alarm_add and push again with app_alarm_update after every
 * edit (an update that changes nothing leaves a ring or a snooze alone). The alarm is a CLOCK entry at hour:minute that
 * can be snoozed; a once-alarm sets `once`, which the framework answers by switching it off when it rings. The chime is
 * a CLOCK entry at minute 0 in the chosen hours that only starts while the device sits on its standby app. */
void al_alarm_spec(const al_config_t *c, app_alarm_spec_t *out);
void al_chime_spec(const al_config_t *c, app_alarm_spec_t *out);
/* Which of this app's entries a ring is (by owner and tag); AL_NONE for anybody else's alarm that opened this app. */
al_kind_t al_kind_of(const app_alarm_info_t *info);
