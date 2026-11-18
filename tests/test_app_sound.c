#include <assert.h>
#include <stdio.h>
#include "app_sound.h"
#include "hw_pwm.h"

/* Every sound the buzzer can be asked for is a short, audible, bounded list of notes. */
int main(void)
{
    for (unsigned kind = 0; kind < APP_BEEP_COUNT; ++kind) {
        const app_tone_t *steps = NULL;
        const size_t count = app_sound_pattern((app_beep_t)kind, &steps);
        assert(steps && count >= 1U && count <= APP_SOUND_MAX_STEPS);
        unsigned total = 0, sounding = 0;
        for (size_t i = 0; i < count; ++i) {
            assert(steps[i].ms > 0U && steps[i].ms <= APP_SOUND_MAX_MS);
            if (steps[i].hz) {
                assert(steps[i].hz >= APP_SOUND_MIN_HZ && steps[i].hz <= APP_SOUND_MAX_HZ);
                ++sounding;
            }
            total += steps[i].ms;
        }
        assert(sounding >= 1U);                      /* a sound that is only silence is a bug */
        assert(steps[count - 1U].hz != 0U);          /* and it ends on a note, not on a rest */
        assert(total <= APP_SOUND_MAX_MS && app_sound_length_ms((app_beep_t)kind) == total);
    }

    /* The original tones are kept: the notify chirp, the confirm blip, the key tick and the error growl. */
    const app_tone_t *steps;
    assert(app_sound_pattern(APP_BEEP_NOTIFY, &steps) == 1U && steps[0].hz == 3000U && steps[0].ms == 120U);
    assert(app_sound_pattern(APP_BEEP_CONFIRM, &steps) == 1U && steps[0].hz == 2400U && steps[0].ms == 220U);
    assert(app_sound_pattern(APP_BEEP_KEY, &steps) == 1U && steps[0].hz == 4000U && steps[0].ms == 30U);
    assert(app_sound_pattern(APP_BEEP_ERROR, &steps) == 1U && steps[0].hz == 1500U && steps[0].ms == 400U);
    /* The alarm is three beeps, and the whole thing fits well inside the two seconds the alarm app repeats it in. */
    assert(app_sound_pattern(APP_BEEP_ALARM, &steps) == 5U && app_sound_length_ms(APP_BEEP_ALARM) < 1000U);

    /* An unknown kind (a corrupted queue entry) is refused and says nothing. */
    steps = (const app_tone_t *)&steps;
    assert(app_sound_pattern(APP_BEEP_COUNT, &steps) == 0U && steps == NULL);
    assert(app_sound_pattern((app_beep_t)-1, NULL) == 0U && app_sound_length_ms((app_beep_t)99) == 0U);

    /* A single note (the buzzer test, app_beep_tone): the same pitch and length limits, edges included. */
    assert(app_sound_tone_valid(APP_SOUND_MIN_HZ, APP_SOUND_MIN_MS) && app_sound_tone_valid(APP_SOUND_MAX_HZ, APP_SOUND_MAX_MS));
    assert(app_sound_tone_valid(2500U, 400U));
    assert(!app_sound_tone_valid(0U, 100U) && !app_sound_tone_valid(APP_SOUND_MIN_HZ - 1U, 100U) && !app_sound_tone_valid(APP_SOUND_MAX_HZ + 1U, 100U));
    assert(!app_sound_tone_valid(2500U, 0U) && !app_sound_tone_valid(2500U, APP_SOUND_MIN_MS - 1U) && !app_sound_tone_valid(2500U, APP_SOUND_MAX_MS + 1U));
    assert(!app_sound_tone_valid(0xFFFFFFFFU, 0xFFFFFFFFU)); /* a request that does not fit the queue's 16 bits is refused, not truncated */

    /* The buzzer and the servo never share a LEDC timer or channel (hw_pwm.h also asserts this at compile time). */
    assert(HW_LEDC_BUZZER_TIMER != HW_LEDC_SERVO_TIMER && HW_LEDC_BUZZER_CHANNEL != HW_LEDC_SERVO_CHANNEL);
    assert(HW_LEDC_BUZZER_TIMER >= 0 && HW_LEDC_SERVO_TIMER < 4 && HW_LEDC_BUZZER_CHANNEL >= 0 && HW_LEDC_SERVO_CHANNEL < 6);
    puts("sound tests passed");
    return 0;
}
