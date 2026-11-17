#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Wall-clock time as apps see it. The device has no battery clock: the time is valid once the PC has set it over USB
 * (`app` op `time`) or Wi-Fi has fetched it over SNTP, and is lost at the next power cycle. */
typedef struct {
    bool valid;    /* false until the clock was set; every other field is then 0 */
    uint32_t epoch; /* UTC seconds since 1970 */
    int tz_min;     /* offset from UTC the local fields below include, in minutes */
    int year, month, day, hour, minute, second;
    int weekday; /* 0 = Sunday */
} app_time_t;

#define APP_TZ_DEFAULT_MIN 480 /* UTC+8 until a PC says otherwise */
#define APP_EPOCH_MIN 1735689600U /* 2025-01-01: anything earlier is a clock that was never set */

/* Local calendar fields of a UTC epoch for a fixed offset: pure integer maths, no libc time zone database. */
void app_time_from_epoch(uint32_t epoch, int tz_min, app_time_t *out);
/* The offset is bounded by the real world (-12:00 .. +14:00). */
bool app_time_tz_valid(int tz_min);
