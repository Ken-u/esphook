/* main/beeper.h */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esphook/app_sound.h"

typedef struct {
    bool ready, enabled;           /* ready: the LEDC timer/channel are set up */
    unsigned played, dropped, bad; /* sounds played / dropped while muted or not ready / queue entries of an unknown kind */
    unsigned failures;             /* LEDC calls that failed (the note is skipped) */
    uint32_t last_hz;              /* the last note's frequency */
} beeper_stats_t;

/* GPIO7 passive buzzer on its own LEDC timer and channel (hw_pwm.h), silent afterwards. A failure is reported, never
 * fatal: the board works without sound. */
esp_err_t beeper_init(void);
/* The task that plays what is queued with beeper_play() / beeper_play_tone(). Asking for a sound is a non-blocking
 * xQueueSend from any task; playing one takes its length in the buzzer's task and never delays the caller. Both return
 * false when the sound was not queued (queue full, buzzer task not started, invalid request). */
esp_err_t beeper_start(void);
bool beeper_play(app_beep_t kind);
bool beeper_play_tone(unsigned hz, unsigned ms);
/* Mute switch, kept across reboots. Muting stops a sound already playing at its next note. */
bool beeper_enabled(void);
esp_err_t beeper_set_enabled(bool on);
void beeper_stats(beeper_stats_t *out);
/* USB text command `sound [on|off|test]`: true when the line was one. */
bool beeper_command(const char *line);
