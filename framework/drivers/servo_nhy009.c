#include "motion_ports.h"
#include "hw_pwm.h"
#include "driver/ledc.h"
/* LEDC timer/channel come from hw_pwm.h: the buzzer (main/beeper.c) has timer 0 / channel 0 and re-tunes its timer
 * for every note, this driver keeps timer 1 / channel 1 at a steady PWM. The clock source is shared by the whole
 * low-speed group on the ESP32-C3, so both name APB (80 MHz) instead of LEDC_AUTO_CLK: AUTO picks APB first for a
 * 50 Hz / 14-bit timer anyway (nothing changes for the servo), and the choice no longer depends on which driver
 * initialises first. */
#define SERVO_TIMER ((ledc_timer_t)HW_LEDC_SERVO_TIMER)
#define SERVO_CHANNEL ((ledc_channel_t)HW_LEDC_SERVO_CHANNEL)
_Static_assert(HW_LEDC_SERVO_TIMER == (int)LEDC_TIMER_1 && HW_LEDC_SERVO_CHANNEL == (int)LEDC_CHANNEL_1,
               "hw_pwm.h names the LEDC enum values");
static unsigned s_hz;
esp_err_t servo_port_init(int gpio, unsigned hz)
{
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = SERVO_TIMER,
        .duty_resolution = LEDC_TIMER_14_BIT, .freq_hz = hz, .clk_cfg = LEDC_USE_APB_CLK};
    esp_err_t err = ledc_timer_config(&timer); if (err != ESP_OK) return err;
    ledc_channel_config_t channel = {.gpio_num = gpio, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SERVO_CHANNEL, .timer_sel = SERVO_TIMER, .duty = 0};
    err = ledc_channel_config(&channel); if (err == ESP_OK) s_hz = hz; return err;
}
esp_err_t servo_port_write(int pulse_us)
{
    if(!s_hz) return pulse_us?ESP_ERR_INVALID_STATE:ESP_OK;
    if(pulse_us && (pulse_us<1000 || pulse_us>2000)) return ESP_ERR_INVALID_ARG;
    uint32_t duty = pulse_us ? (uint32_t)(((uint64_t)pulse_us * s_hz * (1U << 14) + 500000) / 1000000) : 0;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty);
    return err == ESP_OK ? ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL) : err;
}
