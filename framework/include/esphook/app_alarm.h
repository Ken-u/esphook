#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app_time.h"

/* The global alarm registry: any app can ask the framework to open an app at a time, whatever is on screen then.
 *
 * An app registers an entry (a time of day, a UTC instant or "in N seconds"), names the app that should be opened
 * (its `target`, by default itself) and goes on with its life. When the entry is due the framework, not the app,
 * notices: it opens the target (over a game, a menu, anything, unless the entry is `idle_only`) and delivers
 * APP_EVENT_ALARM to it, with `key` = the entry id and `text` = its label. The target then rings, plays or just shows
 * a note, and answers with app_alarm_done() or app_alarm_snooze(). The 闹钟 app is exactly that: a UI over this
 * registry, and a game can use the same calls for its own reminders (a fishing bite while the player is elsewhere,
 * a cool-down that is over, a daily bonus).
 *
 * Rules worth knowing:
 *  - Everything here belongs to the UI task and to the apps' init() (before the UI starts). Do not call it from a
 *    USB command callback: copy and enqueue like for any other UI state.
 *  - The registry lives in RAM. An app that wants its entries after a reboot keeps its own settings (its storage key)
 *    and registers them again in init(): ids are not stable across boots, settings are.
 *  - One entry rings at a time. An entry that becomes due while another rings waits for it to be answered and still
 *    starts if it is inside its window by then; otherwise it is missed, never made up for later.
 *  - A ring that is never answered is closed by the framework after APP_ALARM_RING_MAX_MS, and so is the ring of an
 *    app that is left (the triple-press exit, another alarm): leaving counts as answering with done.
 *  - A target without an on_event callback is only opened: its entry is closed at once.
 *  - Time of day needs the wall clock (USB `time` op or SNTP); "in N seconds" runs on the device's own uptime and
 *    works without it. Resolution is APP_ALARM_POLL_MS. */
#define APP_ALARM_SLOTS 8U
#define APP_ALARM_LABEL_MAX 15U     /* bytes, UTF-8, without the terminator */
#define APP_ALARM_POLL_MS 250U      /* how often the framework looks at the registry */
#define APP_ALARM_RING_MAX_MS 120000U
#define APP_ALARM_EVERY_DAY 0x7FU   /* day_mask: bit d is weekday d, 0 = Sunday */
#define APP_ALARM_WEEKDAYS 0x3EU    /* Monday to Friday */
#define APP_ALARM_EVERY_HOUR 0x00FFFFFFU /* hour_mask: bit h is hour h */
#define APP_ALARM_DEFAULT_WINDOW_S 60U
#define APP_ALARM_DEFAULT_SNOOZE_S 300U

typedef enum {
    APP_ALARM_CLOCK = 1, /* every matching local time: minute, hour_mask, day_mask */
    APP_ALARM_AT,        /* once, at a UTC epoch */
    APP_ALARM_AFTER,     /* once, after_s seconds from now (uptime, no wall clock needed) */
} app_alarm_kind_t;
typedef enum { APP_ALARM_WAITING = 0, APP_ALARM_RINGING, APP_ALARM_SNOOZED } app_alarm_state_t;

typedef struct {
    const char *owner;   /* the registering app's id (required) */
    const char *target;  /* the app opened and told when due; NULL means the owner */
    const char *label;   /* at most APP_ALARM_LABEL_MAX bytes, copied; NULL means none */
    app_alarm_kind_t kind;
    bool enabled;        /* a disabled entry keeps its place but never fires */
    bool idle_only;      /* due only while the device sits on its standby app (a chime); otherwise it takes the screen */
    bool once;           /* CLOCK: disables itself after it fired. AT and AFTER are always once and free their slot when answered */
    uint8_t minute;      /* CLOCK: 0..59 */
    uint8_t day_mask;    /* CLOCK: non-zero subset of APP_ALARM_EVERY_DAY */
    uint32_t hour_mask;  /* CLOCK: non-zero subset of APP_ALARM_EVERY_HOUR */
    uint32_t epoch;      /* AT: UTC seconds, not before 2025 */
    uint32_t after_s;    /* AFTER: 1..604800 */
    uint16_t window_s;   /* how late it may still start, 0 = 60. A CLOCK entry can only start inside its own minute, so 60 is its limit */
    uint8_t max_snoozes; /* how many times app_alarm_snooze() may postpone it; 0 = cannot be snoozed */
    uint16_t snooze_s;   /* 0 = 300 */
    uint32_t tag;        /* free for the owner: which of its entries this is */
} app_alarm_spec_t;

/* What the registry knows about an entry: the spec (copied) and where it stands. */
typedef struct {
    unsigned id;         /* 1..255 */
    app_alarm_state_t state;
    unsigned snoozes;    /* used so far in this ring */
    char owner[24], target[24], label[APP_ALARM_LABEL_MAX + 1];
    app_alarm_kind_t kind;
    bool enabled, idle_only, once;
    uint8_t minute, day_mask, max_snoozes;
    uint16_t window_s, snooze_s;
    uint32_t hour_mask, epoch, after_s, tag;
} app_alarm_info_t;

/* ---- for apps ---------------------------------------------------------------------------------------------------- */
/* Registers an entry. Returns its id (1..255), or 0 when the spec is not valid or all APP_ALARM_SLOTS are taken. */
unsigned app_alarm_add(const app_alarm_spec_t *spec);
/* Replaces the entry's spec in place (same owner required). Saying the same thing again changes nothing, so an app may
 * push all its settings after any edit. A real change ends a ring or snooze that was under way; the minute the entry
 * last fired in is remembered, so setting the time again does not ring a second time. */
bool app_alarm_update(unsigned id, const app_alarm_spec_t *spec);
bool app_alarm_cancel(unsigned id);
unsigned app_alarm_cancel_owner(const char *owner); /* how many were removed */
bool app_alarm_get(unsigned id, app_alarm_info_t *out);
/* Ids of the entries of `owner` (NULL = everybody), at most `max`; returns how many there are. */
unsigned app_alarm_list(const char *owner, unsigned *ids, unsigned max);
/* Seconds until the entry is due next (0 = ringing, or an AT/AFTER entry that is due), or -1 when it is disabled, gone
 * or needs a clock that is not set. A CLOCK entry counts to its next time after now, so inside its own minute that is
 * the following occurrence. A snoozed entry counts to the end of its snooze. `now` is app_time_read() and `now_ms` the context's clock. */
int32_t app_alarm_seconds_until(unsigned id, const app_time_t *now, uint64_t now_ms);
/* Answers to a ring (the entry has to be ringing):
 *  - snooze: rings again after snooze_s. False, and the ring is over, when it has no snoozes left.
 *  - done: the ring is over. A CLOCK entry waits for its next time, AT and AFTER entries disappear. */
bool app_alarm_snooze(unsigned id);
void app_alarm_done(unsigned id);
/* Gives up the occurrence of a CLOCK entry that is due in this very minute (an alarm on the hour has the hour to
 * itself: its owner skips the chime). False when there is nothing to skip or no clock. */
bool app_alarm_skip(unsigned id);

/* ---- for the framework and the tests ---------------------------------------------------------------------------- */
/* Looks for what is due. `idle` is true while the device sits on its standby app. When something starts, returns
 * true with its id and the entry is ringing. Called by the manager every APP_ALARM_POLL_MS. */
bool app_alarm_poll(const app_time_t *now, uint64_t now_ms, bool idle, unsigned *id);
/* The app was left: the rings that were waiting for it are over. */
void app_alarm_leaving(const char *app_id);
typedef struct { uint32_t fired, missed, timeouts; } app_alarm_stats_t;
void app_alarm_stats(app_alarm_stats_t *out);
void app_alarm_reset(void); /* forgets everything: for tests */
