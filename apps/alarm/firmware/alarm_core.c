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
void al_alarm_spec(const al_config_t *c, app_alarm_spec_t *out)
{
    memset(out, 0, sizeof *out);
    out->owner = "alarm";
    out->label = "闹钟";
    out->kind = APP_ALARM_CLOCK;
    out->enabled = c->alarm_on != 0U;
    out->once = c->repeat == AL_ONCE;
    out->minute = c->minute;
    out->hour_mask = 1UL << (c->hour % 24U);
    for (unsigned day = 0; day < 7U; ++day)
        if (al_repeat_has(c->repeat, day)) out->day_mask = (uint8_t)(out->day_mask | (1U << day));
    out->window_s = AL_ALARM_WINDOW_S;
    out->max_snoozes = AL_MAX_SNOOZES;
    out->snooze_s = AL_SNOOZE_S;
    out->tag = AL_TAG_ALARM;
}
void al_chime_spec(const al_config_t *c, app_alarm_spec_t *out)
{
    memset(out, 0, sizeof *out);
    out->owner = "alarm";
    out->label = "整点报时";
    out->kind = APP_ALARM_CLOCK;
    out->enabled = c->chime_on != 0U;
    out->idle_only = true;
    out->minute = 0;
    for (unsigned hour = 0; hour < 24U; ++hour)
        if (al_range_has(c->range, hour)) out->hour_mask |= 1UL << hour;
    if (!out->hour_mask) out->hour_mask = APP_ALARM_EVERY_HOUR; /* a range of nothing cannot happen; the entry must still be valid */
    out->day_mask = APP_ALARM_EVERY_DAY;
    out->window_s = AL_CHIME_WINDOW_S;
    out->tag = AL_TAG_CHIME;
}
al_kind_t al_kind_of(const app_alarm_info_t *info)
{
    if (!info || strcmp(info->owner, "alarm")) return AL_NONE;
    return info->tag == AL_TAG_ALARM ? AL_ALARM : info->tag == AL_TAG_CHIME ? AL_CHIME : AL_NONE;
}
