/* main/beeper.c
 * GPIO7 passive buzzer, driven by LEDC PWM on its own timer and channel (hw_pwm.h).
 *
 * Why this cannot disturb the servo (framework/drivers/servo_nhy009.c, LEDC timer 1 / channel 1, 50 Hz):
 *  - the buzzer owns LEDC timer 0 and channel 0 and only ever calls ledc_set_freq() on timer 0, so the servo's
 *    timer, divider and duty registers are never written here;
 *  - the one thing the timers do share on the ESP32-C3 is the clock source of the whole low-speed group, so both
 *    drivers ask for the APB clock explicitly instead of letting LEDC_AUTO_CLK pick one (a second timer that
 *    asked for another source would be refused by ESP-IDF, and the servo's pulse widths would move with it);
 *  - the notes are played from a task of their own (beeper_task) off beeper_q, so the display/UI task, which also
 *    pumps the servo requests, is never held up by a sound that takes 100..500 ms to play.
 * `sound` (USB) prints the buzzer's timer and, when a servo is configured, the servo's timer, so a change in either
 * shows up. */
#include "beeper.h"
#include "config_store.h"
#include "events.h"
#include "hw_pwm.h"
#include "usb_cdc.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
#include "robot_runtime.h"
#endif
#include <stdio.h>
#include <string.h>

#define BUZZER_TIMER ((ledc_timer_t)HW_LEDC_BUZZER_TIMER)
#define BUZZER_CHANNEL ((ledc_channel_t)HW_LEDC_BUZZER_CHANNEL)
#define BUZZER_DUTY 512U /* half of the 10-bit range: the loudest a square wave gets */
#define SOUND_KEY "sys_sound"

_Static_assert(HW_LEDC_BUZZER_TIMER == (int)LEDC_TIMER_0 && HW_LEDC_BUZZER_CHANNEL == (int)LEDC_CHANNEL_0,
               "hw_pwm.h names the LEDC enum values");
_Static_assert(HW_LEDC_SERVO_TIMER == (int)LEDC_TIMER_1 && HW_LEDC_SERVO_CHANNEL == (int)LEDC_CHANNEL_1,
               "hw_pwm.h names the LEDC enum values");

static volatile bool s_ready;
static volatile bool s_enabled = true;
static volatile unsigned s_played, s_dropped, s_bad, s_failures;
static volatile uint32_t s_last_hz;

/* Output off and held low: a passive buzzer must never be left on a DC level. */
static void silence(void) { (void)ledc_stop(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, 0); }

esp_err_t beeper_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BUZZER_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 2000,
        .clk_cfg = LEDC_USE_APB_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    const ledc_channel_config_t channel = {
        .gpio_num = HW_BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZER_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) return err;
    silence();
    s_ready = true;
    return ESP_OK;
}

static void tone(unsigned hz, unsigned ms)
{
    if (ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZER_TIMER, hz) != ESP_OK ||
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, BUZZER_DUTY) != ESP_OK ||
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL) != ESP_OK) {
        ++s_failures;
        silence();
        return;
    }
    s_last_hz = hz;
    vTaskDelay(pdMS_TO_TICKS(ms));
    silence();
}

static void play(const app_tone_t *steps, size_t count)
{
    for (size_t i = 0; i < count && s_enabled; ++i) {
        if (steps[i].hz == 0U) vTaskDelay(pdMS_TO_TICKS(steps[i].ms));
        else tone(steps[i].hz, steps[i].ms);
    }
}

static void beeper_task(void *arg)
{
    (void)arg;
    for (;;) {
        app_beep_t kind;
        if (xQueueReceive(beeper_q, &kind, portMAX_DELAY) != pdTRUE) continue;
        const app_tone_t *steps;
        const size_t count = app_sound_pattern(kind, &steps);
        if (count == 0U) { ++s_bad; continue; }
        if (!s_ready || !s_enabled) { ++s_dropped; continue; }
        play(steps, count);
        ++s_played;
    }
}

esp_err_t beeper_start(void)
{
    uint8_t on = 1;
    size_t length = sizeof on;
    if (config_app_blob_get(SOUND_KEY, &on, &length) == ESP_OK && length == sizeof on && on <= 1U) s_enabled = on != 0U;
    if (!beeper_q) return ESP_ERR_INVALID_STATE;
    return xTaskCreate(beeper_task, "beeper", 3072, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool beeper_enabled(void) { return s_enabled; }

esp_err_t beeper_set_enabled(bool on)
{
    s_enabled = on;
    if (!on) silence();
    const uint8_t value = on ? 1U : 0U;
    return config_app_blob_set(SOUND_KEY, &value, sizeof value);
}

void beeper_stats(beeper_stats_t *out)
{
    if (!out) return;
    out->ready = s_ready;
    out->enabled = s_enabled;
    out->played = s_played;
    out->dropped = s_dropped;
    out->bad = s_bad;
    out->failures = s_failures;
    out->last_hz = s_last_hz;
}

static void report(void)
{
    beeper_stats_t st;
    beeper_stats(&st);
    unsigned servo_hz = 0;
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    robot_status_t robot;
    robot_get_status(&robot);
    if (robot.servo_ready) servo_hz = ledc_get_freq(LEDC_LOW_SPEED_MODE, (ledc_timer_t)HW_LEDC_SERVO_TIMER);
#endif
    char line[160];
    snprintf(line, sizeof line, "sound=%s ready=%u played=%u dropped=%u bad=%u failures=%u hz=%u buzzer_hz=%u servo_hz=%u",
             st.enabled ? "on" : "off", st.ready ? 1U : 0U, st.played, st.dropped, st.bad, st.failures,
             (unsigned)st.last_hz, st.ready ? (unsigned)ledc_get_freq(LEDC_LOW_SPEED_MODE, BUZZER_TIMER) : 0U, servo_hz);
    (void)cdc_write(line);
}

bool beeper_command(const char *line)
{
    if (!line || strncmp(line, "sound", 5) != 0 || (line[5] != '\0' && line[5] != ' ')) return false;
    const char *arg = line + 5;
    while (*arg == ' ') ++arg;
    if (*arg == '\0') {
        report();
    } else if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
        const bool on = strcmp(arg, "on") == 0;
        const esp_err_t err = beeper_set_enabled(on);
        (void)cdc_write(err == ESP_OK ? (on ? "OK sound on" : "OK sound off")
                                      : (on ? "OK sound on (not saved)" : "OK sound off (not saved)"));
    } else if (strcmp(arg, "test") == 0) {
        const app_beep_t kind = APP_BEEP_NOTIFY;
        if (!s_ready) (void)cdc_write("ERR no buzzer: init failed");
        else if (!s_enabled) (void)cdc_write("ERR sound is off (sound on)");
        else if (!beeper_q || xQueueSend(beeper_q, &kind, 0) != pdTRUE) (void)cdc_write("ERR sound queue full");
        else (void)cdc_write("OK sound test queued");
    } else {
        (void)cdc_write("ERR usage: sound [on|off|test]");
    }
    return true;
}
