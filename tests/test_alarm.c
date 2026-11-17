/* Host tests for the alarm app's side of the framework alarm API (apps/alarm/firmware/alarm_core.c): the settings tables,
 * the registry entries the settings turn into, and what rings when once those entries are in the real registry
 * (framework/core/app_alarm.c). The registry itself is covered by test_app_alarm.c. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "alarm_core.h"

/* 2025-11-17 (a Monday) 00:00 in UTC+8, as a UTC epoch. */
#define MONDAY_LOCAL_MIDNIGHT 1763308800U
static app_time_t at(unsigned day, unsigned hour, unsigned minute, unsigned second)
{
    app_time_t t;
    app_time_from_epoch(MONDAY_LOCAL_MIDNIGHT + day * 86400U + hour * 3600U + minute * 60U + second, 480, &t);
    assert(t.valid && (unsigned)t.hour == hour && (unsigned)t.minute == minute && (unsigned)t.second == second);
    return t;
}
static al_config_t config(bool on, unsigned hour, unsigned minute, unsigned repeat)
{
    al_config_t c;
    al_defaults(&c);
    c.alarm_on = on; c.hour = (uint8_t)hour; c.minute = (uint8_t)minute; c.repeat = (uint8_t)repeat;
    assert(al_config_valid(&c));
    return c;
}
/* What the app does at start-up and after every edit. */
static unsigned s_alarm, s_chime;
static void sync(const al_config_t *c)
{
    app_alarm_spec_t spec;
    al_alarm_spec(c, &spec);
    if (!s_alarm || !app_alarm_update(s_alarm, &spec)) s_alarm = app_alarm_add(&spec);
    al_chime_spec(c, &spec);
    if (!s_chime || !app_alarm_update(s_chime, &spec)) s_chime = app_alarm_add(&spec);
    assert(s_alarm && s_chime);
}
static void fresh(const al_config_t *c)
{
    app_alarm_reset();
    s_alarm = s_chime = 0;
    sync(c);
}
static unsigned s_ms;
/* One look from the framework: the id that starts ringing, or 0. */
static unsigned look(const app_time_t *t, bool idle)
{
    unsigned id = 0;
    s_ms += 250;
    return app_alarm_poll(t, s_ms, idle, &id) ? id : 0U;
}
/* A ring that is answered at once. */
static al_kind_t rung(const app_time_t *t, bool idle)
{
    const unsigned id = look(t, idle);
    if (!id) return AL_NONE;
    app_alarm_info_t info;
    assert(app_alarm_get(id, &info));
    app_alarm_done(id);
    return al_kind_of(&info);
}

static void test_tables(void)
{
    al_config_t c;
    al_defaults(&c);
    assert(al_config_valid(&c) && !c.alarm_on && !c.chime_on && !c.servo_on); /* nothing rings until it is asked for */
    for (unsigned i = 0; i < sizeof c; ++i) { /* a damaged blob is refused */
        al_config_t d = c;
        ((uint8_t *)&d)[i] = 99;
        assert(!al_config_valid(&d));
    }
    assert(!strcmp(al_repeat_name(AL_ONCE), "仅一次") && !strcmp(al_repeat_name(AL_DAILY), "每天") &&
           !strcmp(al_repeat_name(AL_WEEKDAYS), "工作日") && !al_repeat_name(9)[0]);
    assert(!strcmp(al_range_name(AL_RANGE_ALL), "全天") && !strcmp(al_range_name(AL_RANGE_DAY), "8-22点") &&
           !strcmp(al_range_name(AL_RANGE_OFFICE), "9-18点"));
    for (unsigned d = 0; d < 7; ++d) {
        assert(al_repeat_has(AL_DAILY, d) && al_repeat_has(AL_ONCE, d));
        assert(al_repeat_has(AL_WEEKDAYS, d) == (d >= 1 && d <= 5)); /* 0 is Sunday */
    }
    assert(!al_repeat_has(AL_DAILY, 7));
    for (unsigned h = 0; h < 24; ++h) {
        assert(al_range_has(AL_RANGE_ALL, h));
        assert(al_range_has(AL_RANGE_DAY, h) == (h >= 8 && h <= 22));
        assert(al_range_has(AL_RANGE_OFFICE, h) == (h >= 9 && h <= 18));
        assert(al_beats(h) >= 1 && al_beats(h) <= 12 && al_beats(h) == al_beats(h + 12 > 23 ? h - 12 : h + 12));
    }
    assert(al_beats(0) == 12 && al_beats(12) == 12 && al_beats(1) == 1 && al_beats(13) == 1 && al_beats(23) == 11);
    assert(!al_range_has(AL_RANGE_ALL, 24));
}

static void test_specs(void)
{
    app_alarm_spec_t a, h;
    al_config_t c = config(true, 7, 30, AL_DAILY);
    al_alarm_spec(&c, &a);
    assert(!strcmp(a.owner, "alarm") && !strcmp(a.label, "闹钟") && a.kind == APP_ALARM_CLOCK && a.enabled && !a.once && !a.idle_only);
    assert(a.minute == 30 && a.hour_mask == 1U << 7 && a.day_mask == APP_ALARM_EVERY_DAY && a.tag == AL_TAG_ALARM);
    assert(a.max_snoozes == AL_MAX_SNOOZES && a.snooze_s == AL_SNOOZE_S && a.window_s == AL_ALARM_WINDOW_S);
    c.repeat = AL_WEEKDAYS;
    al_alarm_spec(&c, &a);
    assert(a.day_mask == APP_ALARM_WEEKDAYS && !a.once);
    c.repeat = AL_ONCE;
    al_alarm_spec(&c, &a);
    assert(a.day_mask == APP_ALARM_EVERY_DAY && a.once); /* once: any day, until it rings */
    c.alarm_on = 0;
    al_alarm_spec(&c, &a);
    assert(!a.enabled);
    for (unsigned hour = 0; hour < 24; ++hour) { /* every hour of the day makes a valid entry */
        c = config(true, hour, hour % 60, AL_DAILY);
        al_alarm_spec(&c, &a);
        app_alarm_reset();
        assert(app_alarm_add(&a) && a.hour_mask == 1U << hour);
    }
    al_defaults(&c);
    al_chime_spec(&c, &h);
    assert(!strcmp(h.owner, "alarm") && !strcmp(h.label, "整点报时") && h.kind == APP_ALARM_CLOCK && !h.enabled && h.idle_only);
    assert(h.minute == 0 && h.day_mask == APP_ALARM_EVERY_DAY && h.window_s == AL_CHIME_WINDOW_S && h.max_snoozes == 0 && h.tag == AL_TAG_CHIME);
    assert(h.hour_mask == 0x7FFF00U); /* 8 to 22 */
    c.chime_on = 1; c.range = AL_RANGE_OFFICE;
    al_chime_spec(&c, &h);
    assert(h.enabled && h.hour_mask == 0x07FE00U); /* 9 to 18 */
    c.range = AL_RANGE_ALL;
    al_chime_spec(&c, &h);
    assert(h.hour_mask == APP_ALARM_EVERY_HOUR);
    /* the specs are accepted by the registry, and the entries are told apart by tag */
    app_alarm_reset();
    const unsigned ia = app_alarm_add(&a), ic = app_alarm_add(&h);
    app_alarm_info_t info;
    assert(ia && ic && ia != ic);
    assert(app_alarm_get(ia, &info) && al_kind_of(&info) == AL_ALARM);
    assert(app_alarm_get(ic, &info) && al_kind_of(&info) == AL_CHIME);
    app_alarm_spec_t other = {.owner = "game", .kind = APP_ALARM_AFTER, .after_s = 5, .enabled = true, .tag = AL_TAG_ALARM};
    const unsigned io = app_alarm_add(&other);
    assert(io && app_alarm_get(io, &info) && al_kind_of(&info) == AL_NONE); /* somebody else's alarm, even with a familiar tag */
    other.owner = "alarm"; other.tag = 9;
    assert(app_alarm_get(app_alarm_add(&other), &info) && al_kind_of(&info) == AL_NONE);
    assert(al_kind_of(NULL) == AL_NONE);
}

static void test_alarm(void)
{
    al_config_t c = config(true, 7, 30, AL_DAILY);
    fresh(&c);
    assert(rung(&(app_time_t){0}, false) == AL_NONE); /* no clock, no alarm */
    app_time_t t = at(0, 7, 29, 59);
    assert(rung(&t, false) == AL_NONE);
    t = at(0, 7, 30, 0);
    assert(rung(&t, false) == AL_ALARM);
    t = at(0, 7, 30, 45);
    assert(rung(&t, false) == AL_NONE); /* it has rung: the rest of the minute is quiet */
    t = at(0, 7, 31, 0);
    assert(rung(&t, false) == AL_NONE);
    t = at(1, 7, 30, 0);
    assert(rung(&t, false) == AL_ALARM); /* and it comes back tomorrow */
    /* the app is opened late in its minute: still inside the window; later than that is not made up for */
    fresh(&c);
    t = at(2, 7, 30, 45);
    assert(rung(&t, false) == AL_ALARM);
    fresh(&c);
    t = at(2, 7, 31, 0);
    assert(rung(&t, false) == AL_NONE);
    /* switching it off in the settings */
    fresh(&c);
    c.alarm_on = 0; sync(&c);
    t = at(3, 7, 30, 0);
    assert(rung(&t, false) == AL_NONE);
    c.alarm_on = 1; sync(&c);
    assert(rung(&t, false) == AL_ALARM);
    /* weekdays only: Friday rings, Saturday and Sunday do not, Monday does again */
    c = config(true, 6, 0, AL_WEEKDAYS);
    fresh(&c);
    for (unsigned day = 0; day < 8; ++day) {
        t = at(day, 6, 0, 3);
        const unsigned weekday = (1U + day) % 7U;
        assert((unsigned)t.weekday == weekday);
        assert((rung(&t, false) == AL_ALARM) == (weekday >= 1U && weekday <= 5U));
    }
    /* once: rings, then is off in the registry (the app mirrors that into its settings) and does not ring again */
    c = config(true, 8, 15, AL_ONCE);
    fresh(&c);
    t = at(2, 8, 15, 0);
    unsigned id = look(&t, false);
    app_alarm_info_t info;
    assert(id == s_alarm && app_alarm_get(id, &info) && !info.enabled && info.state == APP_ALARM_RINGING);
    c.alarm_on = 0; sync(&c); /* the app's mirror: nothing changes for the ring in progress */
    assert(app_alarm_get(id, &info) && info.state == APP_ALARM_RINGING);
    app_alarm_done(id);
    t = at(3, 8, 15, 0);
    assert(rung(&t, false) == AL_NONE);
    c.alarm_on = 1; sync(&c); /* switched on again by hand */
    assert(rung(&t, false) == AL_ALARM);
    /* a different time zone is the same instant: the alarm follows the local clock */
    c = config(true, 7, 30, AL_DAILY);
    fresh(&c);
    app_time_from_epoch(MONDAY_LOCAL_MIDNIGHT + 7 * 3600U + 30 * 60U, 0, &t); /* the same instant, London */
    assert(t.hour == 23 && rung(&t, false) == AL_NONE);
    app_time_from_epoch(MONDAY_LOCAL_MIDNIGHT + 7 * 3600U + 30 * 60U, 480, &t);
    assert(rung(&t, false) == AL_ALARM);
    /* an alarm rings whether or not the device is idle; and it is not held back by an idle-only chime ringing before it */
    c = config(true, 7, 30, AL_DAILY);
    fresh(&c);
    t = at(5, 7, 30, 0);
    assert(rung(&t, true) == AL_ALARM);
}

static void test_snooze(void)
{
    al_config_t c = config(true, 7, 0, AL_DAILY);
    fresh(&c);
    app_time_t t = at(0, 7, 0, 10);
    unsigned id = look(&t, false);
    assert(id == s_alarm && app_alarm_snooze(id));
    t = at(0, 7, 4, 59);
    s_ms += 298000;                   /* five minutes less a little: the snooze counts uptime, not the wall clock */
    assert(look(&t, false) == 0);
    s_ms += 2000;
    assert(look(&t, false) == id);    /* five minutes later it rings again */
    app_alarm_info_t info;
    assert(app_alarm_get(id, &info) && info.snoozes == 1 && info.state == APP_ALARM_RINGING);
    unsigned more = 0;
    while (app_alarm_snooze(id)) {
        ++more;
        s_ms += AL_SNOOZE_S * 1000U + 250U;
        assert(look(&t, false) == id);
    }
    assert(more == AL_MAX_SNOOZES - 1U); /* out of snoozes: over, the entry waits for tomorrow */
    assert(app_alarm_get(id, &info) && info.state == APP_ALARM_WAITING && info.snoozes == 0);
    /* answered with "done" while it snoozes: nothing rings later */
    t = at(1, 7, 0, 20);
    id = look(&t, false);
    assert(id && app_alarm_snooze(id));
    c.alarm_on = 0; sync(&c);          /* switched off in the settings while it snoozes */
    s_ms += AL_SNOOZE_S * 1000U + 250U;
    assert(look(&t, false) == 0);
    /* a snooze that is pending when the alarm is moved ends */
    c = config(true, 7, 0, AL_DAILY);
    fresh(&c);
    t = at(0, 7, 0, 5);
    id = look(&t, false);
    assert(id && app_alarm_snooze(id));
    c.minute = 3; sync(&c);
    t = at(0, 7, 3, 0);
    s_ms += 60000;
    assert(look(&t, false) == s_alarm && app_alarm_get(s_alarm, &info) && info.snoozes == 0);
    /* a once-alarm that rang keeps its snoozes even though it is already off */
    c = config(true, 9, 0, AL_ONCE);
    fresh(&c);
    t = at(0, 9, 0, 0);
    id = look(&t, false);
    assert(id && app_alarm_get(id, &info) && !info.enabled);
    c.alarm_on = 0; sync(&c);
    assert(app_alarm_snooze(id) && app_alarm_get(id, &info) && info.state == APP_ALARM_SNOOZED);
    s_ms += AL_SNOOZE_S * 1000U + 250U;
    assert(look(&(app_time_t){0}, false) == id);
}

static void test_chime(void)
{
    al_config_t c;
    al_defaults(&c);
    c.chime_on = 1;
    fresh(&c);
    app_time_t t = at(0, 9, 0, 0);
    assert(rung(&t, false) == AL_NONE);   /* a chime waits for the standby screen: not over a game or the menu */
    assert(rung(&t, true) == AL_CHIME);
    assert(rung(&t, true) == AL_NONE);    /* once per hour */
    t = at(0, 9, 0, 14);
    assert(rung(&t, true) == AL_NONE);
    t = at(0, 10, 0, 1);
    assert(rung(&t, true) == AL_CHIME);   /* the next hour */
    fresh(&c);
    t = at(0, 10, 0, AL_CHIME_WINDOW_S);
    assert(rung(&t, true) == AL_NONE);    /* opening the app late does not chime */
    t = at(0, 10, 1, 0);
    assert(rung(&t, true) == AL_NONE);
    t = at(0, 7, 0, 1);
    assert(rung(&t, true) == AL_NONE && c.range == AL_RANGE_DAY); /* the night is quiet by default */
    t = at(0, 23, 0, 1);
    assert(rung(&t, true) == AL_NONE);
    t = at(0, 22, 0, 1);
    assert(rung(&t, true) == AL_CHIME);
    c.range = AL_RANGE_ALL; sync(&c);
    t = at(1, 0, 0, 1);
    assert(rung(&t, true) == AL_CHIME && al_beats((unsigned)t.hour) == 12);
    c.chime_on = 0; sync(&c);
    t = at(1, 1, 0, 1);
    assert(rung(&t, true) == AL_NONE);
    /* it does not ring while an alarm is, and is not owed afterwards */
    c = config(true, 12, 0, AL_DAILY);
    c.chime_on = 1; c.range = AL_RANGE_ALL;
    fresh(&c);
    t = at(0, 12, 0, 2);
    unsigned id = look(&t, true);
    assert(id == s_alarm); /* an alarm goes before a chime, on the same look */
    assert(app_alarm_skip(s_chime)); /* the hour is the alarm's: what the app does when it starts to ring */
    app_alarm_done(id);
    assert(look(&t, true) == 0);
    t = at(0, 12, 0, 10);
    assert(look(&t, true) == 0);
    t = at(0, 13, 0, 0);
    assert(rung(&t, true) == AL_CHIME);
    /* without the skip the chime would come right after the alarm */
    fresh(&c);
    t = at(0, 12, 0, 2);
    id = look(&t, true);
    assert(id == s_alarm);
    app_alarm_done(id);
    t = at(0, 12, 0, 3);
    assert(rung(&t, true) == AL_CHIME);
}

static void test_until(void)
{
    al_config_t c = config(true, 7, 30, AL_DAILY);
    fresh(&c);
    app_time_t t = at(0, 7, 0, 0);
    assert(app_alarm_seconds_until(s_alarm, &t, 0) == 30 * 60);
    t = at(0, 7, 30, 0);
    assert(app_alarm_seconds_until(s_alarm, &t, 0) == 1440 * 60);          /* a clock entry counts to its next time after now */
    assert(rung(&t, false) == AL_ALARM);
    assert(app_alarm_seconds_until(s_alarm, &t, 0) == 1440 * 60);          /* it has rung: the next one is tomorrow's */
    t = at(0, 8, 0, 0);
    assert(app_alarm_seconds_until(s_alarm, &t, 0) == (23 * 60 + 30) * 60);
    c.repeat = AL_WEEKDAYS; sync(&c);
    t = at(4, 8, 0, 0); /* Friday morning, after the alarm: next is Monday */
    assert(t.weekday == 5 && app_alarm_seconds_until(s_alarm, &t, 0) == (3 * 1440 - 30) * 60);
    c.alarm_on = 0; sync(&c);
    assert(app_alarm_seconds_until(s_alarm, &t, 0) == -1 && app_alarm_seconds_until(s_alarm, &(app_time_t){0}, 0) == -1);
}

int main(void)
{
    test_tables();
    test_specs();
    test_alarm();
    test_snooze();
    test_chime();
    test_until();
    puts("alarm app: tables, registry entries, repeat and hour windows, snooze, hourly chime, next-ring distance passed");
    return 0;
}
