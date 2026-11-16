#include "motion_ports.h"
#include "sdkconfig.h"
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
#include "driver/ledc.h"
static unsigned s_hz;
esp_err_t servo_port_init(int gpio, unsigned hz)
{
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_14_BIT, .freq_hz = hz, .clk_cfg = LEDC_AUTO_CLK};
    esp_err_t err = ledc_timer_config(&timer); if (err != ESP_OK) return err;
    ledc_channel_config_t channel = {.gpio_num = gpio, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1, .timer_sel = LEDC_TIMER_1, .duty = 0};
    err = ledc_channel_config(&channel); if (err == ESP_OK) s_hz = hz; return err;
}
esp_err_t servo_port_write(int pulse_us)
{
    if(!s_hz) return pulse_us?ESP_ERR_INVALID_STATE:ESP_OK;
    if(pulse_us && (pulse_us<1000 || pulse_us>2000)) return ESP_ERR_INVALID_ARG;
    uint32_t duty = pulse_us ? (uint32_t)(((uint64_t)pulse_us * s_hz * (1U << 14) + 500000) / 1000000) : 0;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    return err == ESP_OK ? ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1) : err;
}
#endif
