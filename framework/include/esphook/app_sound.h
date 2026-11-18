#pragma once
/* The passive buzzer (GPIO7) as data: which sounds exist and what notes each is made of. Pure tables, no hardware:
 * the driver (main/beeper.c) plays them from its own task, so asking for a sound never blocks the caller.
 * Applications ask through app_beep() in app_services.h. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The order is the queue payload between the producers (/notify, device link, apps) and the buzzer task. */
typedef enum {
    APP_BEEP_NOTIFY,  /* a Hook notification arrived */
    APP_BEEP_CONFIRM, /* ... that waits for an answer */
    APP_BEEP_KEY,     /* a short tick */
    APP_BEEP_ERROR,   /* ... or reports a failure */
    APP_BEEP_ALARM,   /* the alarm clock ring: three quick beeps, asked for again every couple of seconds */
    APP_BEEP_COUNT
} app_beep_t;

typedef struct {
    uint16_t hz; /* 0 is silence */
    uint16_t ms;
} app_tone_t;

/* What a passive buzzer and the LEDC timer can do: a 10-bit duty at 80 MHz reaches down to ~77 Hz, and the
 * buzzers on these boards are only loud near their resonance (2..4 kHz). */
#define APP_SOUND_MIN_HZ 300U
#define APP_SOUND_MAX_HZ 6000U
#define APP_SOUND_MAX_STEPS 8U
/* One sound is at most this long, so a full queue (8 entries) is at most 8 s of noise, never a locked-up buzzer. */
#define APP_SOUND_MAX_MS 1000U

/* The notes of a sound: returns the number of steps (0 for a kind that does not exist) and points `*steps` at them. */
size_t app_sound_pattern(app_beep_t kind, const app_tone_t **steps);
/* How long it sounds, silences included. */
unsigned app_sound_length_ms(app_beep_t kind);
