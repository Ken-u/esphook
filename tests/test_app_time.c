/* Host tests for the wall-clock maths (framework/core/app_time.c). */
#include <assert.h>
#include <stdio.h>
#include "app_time.h"

static app_time_t at(uint32_t epoch, int tz)
{
    app_time_t t;
    app_time_from_epoch(epoch, tz, &t);
    return t;
}
int main(void)
{
    /* 2025-11-17 00:00:00 UTC is a Monday. */
    app_time_t t = at(1763337600U, 0);
    assert(t.valid && t.year == 2025 && t.month == 11 && t.day == 17 && t.hour == 0 && t.minute == 0 && t.second == 0 && t.weekday == 1);
    assert(t.epoch == 1763337600U && t.tz_min == 0);
    t = at(1763337600U, 480); /* Beijing: the same instant is 08:00 */
    assert(t.hour == 8 && t.day == 17 && t.weekday == 1 && t.tz_min == 480);
    t = at(1763337600U, -300); /* New York: still the evening of Sunday the 16th */
    assert(t.day == 16 && t.hour == 19 && t.weekday == 0);
    t = at(1763337600U + 86399U, 0);
    assert(t.day == 17 && t.hour == 23 && t.minute == 59 && t.second == 59);
    t = at(1763337600U + 86400U, 0);
    assert(t.day == 18 && t.hour == 0 && t.weekday == 2);
    /* Month, year and leap day boundaries. */
    t = at(1767225599U, 0); /* 2025-12-31 23:59:59 */
    assert(t.year == 2025 && t.month == 12 && t.day == 31 && t.hour == 23);
    t = at(1767225600U, 0); /* 2026-01-01 00:00:00, a Thursday */
    assert(t.year == 2026 && t.month == 1 && t.day == 1 && t.weekday == 4);
    t = at(1709164800U + 86400U, 0); /* 2024-02-29 is a leap day, but 2024 is before the epoch floor */
    assert(!t.valid);
    t = at(1835395200U, 0); /* 2028-02-29 00:00:00 */
    assert(t.valid && t.year == 2028 && t.month == 2 && t.day == 29 && t.weekday == 2);
    t = at(1835395200U + 86400U, 0);
    assert(t.month == 3 && t.day == 1 && t.weekday == 3);
    t = at(4102444800U, 0); /* 2100-01-01: 2100 is not a leap year */
    assert(t.year == 2100 && t.month == 1 && t.day == 1);
    t = at(4107542400U, 0); /* 2100-03-01 */
    assert(t.year == 2100 && t.month == 3 && t.day == 1);
    /* A clock that was never set (or an impossible offset) is not valid. */
    t = at(0, 480);
    assert(!t.valid && t.year == 0 && t.hour == 0);
    t = at(1763337600U, 841);
    assert(!t.valid);
    assert(app_time_tz_valid(-720) && app_time_tz_valid(840) && !app_time_tz_valid(-721) && !app_time_tz_valid(841));
    puts("app time: calendar fields, weekdays, offsets, leap days and the unset clock passed");
    return 0;
}
