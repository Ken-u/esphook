/* Host tests for what rings when (apps/alarm/firmware/alarm_core.c). */
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

static void test_alarm(void)
{
    al_config_t c = config(true, 7, 30, AL_DAILY);
    al_sched_t s = {0};
    assert(al_due(&c, &s, &(app_time_t){0}) == AL_NONE); /* no clock, no alarm */
    assert(al_due(&c, &s, &(app_time_t){.valid = false}) == AL_NONE);
    app_time_t t = at(0, 7, 29, 59);
    assert(al_due(&c, &s, &t) == AL_NONE);
    t = at(0, 7, 30, 0);
    assert(al_due(&c, &s, &t) == AL_ALARM);
    t = at(0, 7, 30, 45);
    assert(al_due(&c, &s, &t) == AL_ALARM); /* anywhere in its minute */
    assert(!al_fire(&c, &s, AL_ALARM, &t) && c.alarm_on);
    assert(al_due(&c, &s, &t) == AL_NONE); /* it has rung: the rest of the minute is quiet */
    t = at(0, 7, 31, 0);
    assert(al_due(&c, &s, &t) == AL_NONE);
    t = at(1, 7, 30, 0);
    assert(al_due(&c, &s, &t) == AL_ALARM); /* and it comes back tomorrow */
    c.alarm_on = 0;
    assert(al_due(&c, &s, &t) == AL_NONE);
    /* weekdays only: Friday rings, Saturday and Sunday do not, Monday does again */
    c = config(true, 6, 0, AL_WEEKDAYS);
    memset(&s, 0, sizeof s);
    for (unsigned day = 0; day < 8; ++day) {
        t = at(day, 6, 0, 3);
        const unsigned weekday = (1U + day) % 7U;
        assert((unsigned)t.weekday == weekday);
        assert((al_due(&c, &s, &t) == AL_ALARM) == (weekday >= 1U && weekday <= 5U));
    }
    /* once: rings, then switches itself off and asks to be saved */
    c = config(true, 8, 15, AL_ONCE);
    memset(&s, 0, sizeof s);
    t = at(2, 8, 15, 0);
    assert(al_due(&c, &s, &t) == AL_ALARM && al_fire(&c, &s, AL_ALARM, &t) && !c.alarm_on);
    assert(al_due(&c, &s, &t) == AL_NONE);
    /* a different time zone is the same instant: the alarm follows the local clock */
    c = config(true, 7, 30, AL_DAILY);
    memset(&s, 0, sizeof s);
    app_time_from_epoch(MONDAY_LOCAL_MIDNIGHT + 7 * 3600U + 30 * 60U, 480, &t);
    assert(al_due(&c, &s, &t) == AL_ALARM);
    app_time_from_epoch(MONDAY_LOCAL_MIDNIGHT + 7 * 3600U + 30 * 60U, 0, &t); /* the same instant, London */
    assert(t.hour == 23 && al_due(&c, &s, &t) == AL_NONE);
}

static void test_snooze(void)
{
    al_config_t c = config(true, 7, 0, AL_DAILY);
    al_sched_t s = {0};
    app_time_t t = at(0, 7, 0, 10);
    assert(al_due(&c, &s, &t) == AL_ALARM);
    al_fire(&c, &s, AL_ALARM, &t);
    assert(al_snooze(&s, &t) && s.snoozes == 1 && s.snooze_epoch == t.epoch + AL_SNOOZE_S);
    t = at(0, 7, 4, 59);
    assert(al_due(&c, &s, &t) == AL_NONE);
    t = at(0, 7, 5, 10);
    assert(al_due(&c, &s, &t) == AL_ALARM); /* five minutes later it rings again */
    al_fire(&c, &s, AL_ALARM, &t);
    assert(s.snooze_epoch == 0 && al_due(&c, &s, &t) == AL_NONE && s.snoozes == 1); /* the count survives the snooze */
    unsigned more = 0;
    while (al_snooze(&s, &t)) ++more;
    assert(more == AL_MAX_SNOOZES - 1U && !s.snooze_epoch && !s.snoozes); /* out of snoozes: over */
    assert(!al_snooze(&s, &(app_time_t){0}) && !s.snooze_epoch);
    /* a snooze outlives the alarm being switched off by an unrelated edit only until dismissed */
    t = at(0, 7, 0, 20);
    assert(al_snooze(&s, &t));
    al_dismiss(&s);
    t = at(0, 7, 6, 0);
    assert(al_due(&c, &s, &t) == AL_NONE);
    /* a snooze that is still pending when the next alarm minute comes is taken over by it */
    al_config_t d = config(true, 7, 3, AL_DAILY);
    memset(&s, 0, sizeof s);
    t = at(0, 7, 0, 5);
    assert(al_snooze(&s, &t));
    t = at(0, 7, 3, 0);
    assert(al_due(&d, &s, &t) == AL_ALARM);
    al_fire(&d, &s, AL_ALARM, &t);
    assert(s.snooze_epoch == 0 && s.snoozes == 0);
    /* a once-alarm that rang keeps its snoozes even though it is already off */
    c = config(true, 9, 0, AL_ONCE);
    memset(&s, 0, sizeof s);
    t = at(0, 9, 0, 0);
    assert(al_fire(&c, &s, AL_ALARM, &t) && !c.alarm_on && al_snooze(&s, &t));
    t = at(0, 9, 5, 0);
    assert(al_due(&c, &s, &t) == AL_ALARM);
}

static void test_chime(void)
{
    al_config_t c;
    al_defaults(&c);
    c.chime_on = 1;
    al_sched_t s = {0};
    app_time_t t = at(0, 9, 0, 0);
    assert(al_due(&c, &s, &t) == AL_CHIME);
    assert(!al_fire(&c, &s, AL_CHIME, &t) && al_due(&c, &s, &t) == AL_NONE);
    t = at(0, 9, 0, 14);
    assert(al_due(&c, &s, &t) == AL_NONE);
    t = at(0, 10, 0, 1);
    assert(al_due(&c, &s, &t) == AL_CHIME); /* the next hour */
    memset(&s, 0, sizeof s);
    t = at(0, 10, 0, AL_CHIME_WINDOW_S);
    assert(al_due(&c, &s, &t) == AL_NONE); /* opening the app late does not chime */
    t = at(0, 10, 1, 0);
    assert(al_due(&c, &s, &t) == AL_NONE);
    t = at(0, 7, 0, 1);
    assert(al_due(&c, &s, &t) == AL_NONE && c.range == AL_RANGE_DAY); /* the night is quiet by default */
    t = at(0, 23, 0, 1);
    assert(al_due(&c, &s, &t) == AL_NONE);
    t = at(0, 22, 0, 1);
    assert(al_due(&c, &s, &t) == AL_CHIME);
    c.range = AL_RANGE_ALL;
    t = at(0, 0, 0, 1);
    assert(al_due(&c, &s, &t) == AL_CHIME && al_beats((unsigned)t.hour) == 12);
    c.chime_on = 0;
    assert(al_due(&c, &s, &t) == AL_NONE);
    /* an alarm on the hour wins and the chime stays quiet behind it */
    c = config(true, 12, 0, AL_DAILY);
    c.chime_on = 1;
    c.range = AL_RANGE_ALL;
    memset(&s, 0, sizeof s);
    t = at(0, 12, 0, 2);
    assert(al_due(&c, &s, &t) == AL_ALARM);
    al_fire(&c, &s, AL_ALARM, &t);
    assert(al_due(&c, &s, &t) == AL_NONE);
    /* the key of a minute is unique and increasing */
    uint32_t last = 0;
    for (unsigned m = 0; m < 3000; m += 7) {
        t = at(m / 1440, (m % 1440) / 60, m % 60, 0);
        assert(al_minute_key(&t) > last);
        last = al_minute_key(&t);
    }
    assert(al_minute_key(&(app_time_t){.epoch = 0, .tz_min = -60}) == 1U);
}

static void test_until(void)
{
    al_config_t c = config(true, 7, 30, AL_DAILY);
    app_time_t t = at(0, 7, 0, 0);
    assert(al_minutes_until(&c, &t) == 30);
    t = at(0, 7, 30, 0);
    assert(al_minutes_until(&c, &t) == 1440); /* it rings now: the next one is tomorrow's */
    t = at(0, 8, 0, 0);
    assert(al_minutes_until(&c, &t) == 23 * 60 + 30);
    c.repeat = AL_WEEKDAYS;
    t = at(4, 8, 0, 0); /* Friday morning, after the alarm: next is Monday */
    assert(t.weekday == 5 && al_minutes_until(&c, &t) == 3 * 1440 - 30);
    c.alarm_on = 0;
    assert(al_minutes_until(&c, &t) == -1 && al_minutes_until(&c, &(app_time_t){0}) == -1);
}

int main(void)
{
    test_tables();
    test_alarm();
    test_snooze();
    test_chime();
    test_until();
    puts("alarm core: tables, repeat and hour windows, snooze, hourly chime, next-ring distance passed");
    return 0;
}
