#include "app_time.h"
#include <string.h>

bool app_time_tz_valid(int tz_min) { return tz_min >= -720 && tz_min <= 840; }

void app_time_from_epoch(uint32_t epoch, int tz_min, app_time_t *out)
{
    memset(out, 0, sizeof *out);
    if (epoch < APP_EPOCH_MIN || !app_time_tz_valid(tz_min)) return;
    const int64_t local = (int64_t)epoch + (int64_t)tz_min * 60;
    int64_t days = local / 86400, secs = local % 86400;
    if (secs < 0) { secs += 86400; --days; }
    /* Civil-from-days (H. Hinnant): the proleptic Gregorian calendar, March-based years. */
    const int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153, day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    out->valid = true;
    out->epoch = epoch;
    out->tz_min = tz_min;
    out->year = (int)(yoe + era * 400 + (month <= 2 ? 1 : 0));
    out->month = (int)month;
    out->day = (int)day;
    out->hour = (int)(secs / 3600);
    out->minute = (int)(secs % 3600 / 60);
    out->second = (int)(secs % 60);
    out->weekday = (int)(((days + 4) % 7 + 7) % 7); /* 1970-01-01 was a Thursday */
}
