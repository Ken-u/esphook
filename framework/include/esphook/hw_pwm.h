#pragma once
/* Who owns which LEDC timer and channel (docs/hardware.md). Board code only: applications never include this.
 *
 * The ESP32-C3 has four low-speed LEDC timers and six channels, and ALL of the timers share one clock source that
 * is selected for the whole group (ESP-IDF refuses a second timer that asks for a different one: "timer clock
 * conflict"; a timer left on LEDC_AUTO_CLK may land on another source than its neighbour). The buzzer re-tunes its
 * timer for every note, the servo needs a steady 50 Hz, so they must never share a timer or a channel, and both
 * ask for the APB clock (80 MHz, fixed: power management is off) explicitly. The numbers are the values of
 * LEDC_TIMER_x and LEDC_CHANNEL_x; the drivers cast them and carry the same _Static_assert. */
#define HW_LEDC_BUZZER_TIMER 0
#define HW_LEDC_BUZZER_CHANNEL 0
#define HW_LEDC_SERVO_TIMER 1
#define HW_LEDC_SERVO_CHANNEL 1
#define HW_BUZZER_GPIO 7 /* passive buzzer, + side (docs/hardware.md); nothing else uses this pin */

_Static_assert(HW_LEDC_BUZZER_TIMER != HW_LEDC_SERVO_TIMER, "buzzer and servo must not share a LEDC timer");
_Static_assert(HW_LEDC_BUZZER_CHANNEL != HW_LEDC_SERVO_CHANNEL, "buzzer and servo must not share a LEDC channel");
