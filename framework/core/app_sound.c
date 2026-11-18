#include "app_sound.h"

static const app_tone_t s_notify[] = {{3000, 120}};
static const app_tone_t s_confirm[] = {{2400, 220}};
static const app_tone_t s_key[] = {{4000, 30}};
static const app_tone_t s_error[] = {{1500, 400}};
static const app_tone_t s_alarm[] = {{3000, 110}, {0, 80}, {3000, 110}, {0, 80}, {3000, 110}};

static const struct { const app_tone_t *steps; size_t count; } s_sounds[APP_BEEP_COUNT] = {
    [APP_BEEP_NOTIFY] = {s_notify, sizeof s_notify / sizeof s_notify[0]},
    [APP_BEEP_CONFIRM] = {s_confirm, sizeof s_confirm / sizeof s_confirm[0]},
    [APP_BEEP_KEY] = {s_key, sizeof s_key / sizeof s_key[0]},
    [APP_BEEP_ERROR] = {s_error, sizeof s_error / sizeof s_error[0]},
    [APP_BEEP_ALARM] = {s_alarm, sizeof s_alarm / sizeof s_alarm[0]},
};

size_t app_sound_pattern(app_beep_t kind, const app_tone_t **steps)
{
    if ((unsigned)kind >= (unsigned)APP_BEEP_COUNT) {
        if (steps) *steps = NULL;
        return 0;
    }
    if (steps) *steps = s_sounds[kind].steps;
    return s_sounds[kind].count;
}

unsigned app_sound_length_ms(app_beep_t kind)
{
    const app_tone_t *steps;
    const size_t count = app_sound_pattern(kind, &steps);
    unsigned total = 0;
    for (size_t i = 0; i < count; ++i) total += steps[i].ms;
    return total;
}
