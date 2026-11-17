#include "alarm_core.h"
#include <string.h>

void al_defaults(al_config_t *c)
{
    memset(c, 0, sizeof *c);
    c->version = AL_VERSION;
    c->hour = 7;
    c->repeat = AL_DAILY;
    c->range = AL_RANGE_DAY; /* off until asked for: a new app must not start ringing on its own */
}
bool al_config_valid(const al_config_t *c)
{
    return c->version == AL_VERSION && c->alarm_on <= 1U && c->hour < 24U && c->minute < 60U && c->repeat < AL_REPEATS &&
           c->chime_on <= 1U && c->range < AL_RANGES && c->servo_on <= 1U;
}
const char *al_repeat_name(unsigned repeat)
{
    static const char *const names[AL_REPEATS] = {"仅一次", "每天", "工作日"};
    return repeat < AL_REPEATS ? names[repeat] : "";
}
const char *al_range_name(unsigned range)
{
    static const char *const names[AL_RANGES] = {"全天", "8-22点", "9-18点"};
    return range < AL_RANGES ? names[range] : "";
}
bool al_repeat_has(unsigned repeat, unsigned weekday)
{
    if (weekday > 6U) return false;
    return repeat == AL_WEEKDAYS ? weekday >= 1U && weekday <= 5U : repeat < AL_REPEATS;
}
bool al_range_has(unsigned range, unsigned hour)
{
    if (hour > 23U) return false;
    switch (range) {
    case AL_RANGE_ALL: return true;
    case AL_RANGE_DAY: return hour >= 8U && hour <= 22U;
    case AL_RANGE_OFFICE: return hour >= 9U && hour <= 18U;
    default: return false;
    }
}
unsigned al_beats(unsigned hour)
{
    const unsigned h = hour % 12U;
    return h ? h : 12U;
}
uint32_t al_minute_key(const app_time_t *t)
{
    const int64_t local = (int64_t)t->epoch + (int64_t)t->tz_min * 60;
    return local > 0 ? (uint32_t)(local / 60) + 1U : 1U;
}
al_kind_t al_due(const al_config_t *c, const al_sched_t *s, const app_time_t *t)
{
    if (!t->valid) return AL_NONE;
    const uint32_t key = al_minute_key(t);
    if (s->snooze_epoch && t->epoch >= s->snooze_epoch) return AL_ALARM;
    if (c->alarm_on && t->hour == c->hour && t->minute == c->minute && al_repeat_has(c->repeat, (unsigned)t->weekday) &&
        s->alarm_key != key)
        return AL_ALARM;
    if (c->chime_on && t->minute == 0U && t->second < (int)AL_CHIME_WINDOW_S && al_range_has(c->range, (unsigned)t->hour) &&
        s->chime_key != key)
        return AL_CHIME;
    return AL_NONE;
}
bool al_fire(al_config_t *c, al_sched_t *s, al_kind_t kind, const app_time_t *t)
{
    bool changed = false;
    const uint32_t key = al_minute_key(t);
    if (kind == AL_ALARM) {
        if (s->snooze_epoch && t->epoch >= s->snooze_epoch) s->snooze_epoch = 0; /* the snooze has run out: this is it */
        else if (c->alarm_on && t->hour == c->hour && t->minute == c->minute) {
            s->alarm_key = key;
            s->snoozes = 0;
            s->snooze_epoch = 0;
            if (c->repeat == AL_ONCE) { c->alarm_on = 0; changed = true; }
        }
    }
    if (t->minute == 0U) s->chime_key = key; /* an alarm on the hour has the hour to itself: no chime on top of it */
    if (kind == AL_CHIME) s->chime_key = key;
    return changed;
}
bool al_snooze(al_sched_t *s, const app_time_t *t)
{
    if (s->snoozes >= AL_MAX_SNOOZES || !t->valid) { al_dismiss(s); return false; }
    ++s->snoozes;
    s->snooze_epoch = t->epoch + AL_SNOOZE_S;
    return true;
}
void al_dismiss(al_sched_t *s)
{
    s->snooze_epoch = 0;
    s->snoozes = 0;
}
int al_minutes_until(const al_config_t *c, const app_time_t *t)
{
    if (!t->valid || !c->alarm_on) return -1;
    const int now = (int)t->hour * 60 + (int)t->minute, at = (int)c->hour * 60 + (int)c->minute;
    for (int day = 0; day <= 7; ++day) {
        if (!al_repeat_has(c->repeat, ((unsigned)t->weekday + (unsigned)day) % 7U)) continue;
        const int wait = day * 1440 + at - now;
        if (wait > 0) return wait;
    }
    return -1;
}
