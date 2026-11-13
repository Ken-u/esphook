#include "sdkconfig.h"
#include "robot_runtime.h"
#include "usb_cdc.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
#define RADAR_UART UART_NUM_1
#define SERVO_TIMER LEDC_TIMER_1
#define SERVO_CHANNEL LEDC_CHANNEL_1
#define SERVO_BITS LEDC_TIMER_14_BIT
static const char *TAG = "robot";
static QueueHandle_t s_commands, s_uart_events;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static robot_status_t s_status = {.state = ROBOT_FAULT};
typedef struct { char line[80]; } command_t;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
void robot_get_status(robot_status_t *out)
{
    portENTER_CRITICAL(&s_status_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_status_lock);
}
static esp_err_t servo_write(int pulse_us)
{
    uint32_t duty = pulse_us ? (uint32_t)(((uint64_t)pulse_us *
                    CONFIG_ROBOT_SERVO_HZ * (1U << 14) + 500000) / 1000000) : 0;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty);
    return err == ESP_OK ? ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL) : err;
}
static void load_config(robot_config_t *c)
{
    *c = robot_default_config();
    nvs_handle_t h;
    if (nvs_open("robot", NVS_READONLY, &h) != ESP_OK) return;
    robot_config_t v = *c;
    int32_t value;
    uint8_t reverse;
    if (nvs_get_i32(h, "center", &value) == ESP_OK) v.center_us = value;
    if (nvs_get_i32(h, "limit", &value) == ESP_OK) v.limit_deg = value;
    if (nvs_get_i32(h, "speed", &value) == ESP_OK) v.speed_dps = value;
    if (nvs_get_u8(h, "reverse", &reverse) == ESP_OK) v.reverse = reverse != 0;
    nvs_close(h);
    if (robot_config_valid(&v)) *c = v;
    else ESP_LOGW(TAG, "invalid saved calibration; using defaults");
}
static esp_err_t save_config(const robot_config_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("robot", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(h, "center", c->center_us);
    if (err == ESP_OK) err = nvs_set_i32(h, "limit", c->limit_deg);
    if (err == ESP_OK) err = nvs_set_i32(h, "speed", c->speed_dps);
    if (err == ESP_OK) err = nvs_set_u8(h, "reverse", c->reverse);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
static bool number(const char *text, int *value)
{
    if (!text[0]) return false;
    errno = 0;
    char *end;
    long n = strtol(text, &end, 10);
    if (errno || *end || n < -10000 || n > 10000) return false;
    *value = (int)n;
    return true;
}
static void print_status(const robot_follow_t *f, bool armed, bool following,
                         bool fault, const ld2450_parser_t *p, float angle)
{
    char buf[300];
    snprintf(buf, sizeof buf,
             "robot state=%s armed=%d mode=%s fault=%d angle=%.1f target=%.1f "
             "xy=%.0f,%.0f frames=%lu bad=%lu center_us=%d limit=%d reverse=%d speed=%d",
             robot_state_name(f->state), armed, following ? "follow" : "manual", fault,
             (double)angle, (double)f->goal_deg, (double)f->x_mm, (double)f->y_mm,
             (unsigned long)p->frames, (unsigned long)p->rejected,
             f->config.center_us, f->config.limit_deg, f->config.reverse, f->config.speed_dps);
    cdc_write(buf);
}
static void robot_task(void *arg)
{
    (void)arg;
    robot_config_t config;
    load_config(&config);
    robot_follow_t follow;
    robot_follow_init(&follow, &config, now_ms());
    ld2450_parser_t parser = {0};
    bool armed = false, following = false, hardware_fault = false;
    float angle = 0, manual_goal = 0;
    uint64_t last_step = now_ms(), last_byte = 0;
    int last_pulse = 0;
    while (1) {
        uart_event_t event;
        if (xQueueReceive(s_uart_events, &event, pdMS_TO_TICKS(20))) {
            if (event.type == UART_DATA) {
                uint8_t bytes[128];
                size_t left = event.size;
                while (left) {
                    int n = uart_read_bytes(RADAR_UART, bytes,
                                           left < sizeof bytes ? left : sizeof bytes, 0);
                    if (n <= 0) break;
                    uint64_t stamp = now_ms();
                    if (last_byte && stamp - last_byte > 250) parser.used = 0;
                    last_byte = stamp;
                    left -= n;
                    for (int i = 0; i < n; ++i) {
                        ld2450_frame_t frame;
                        if (ld2450_feed(&parser, bytes[i], &frame))
                            robot_follow_frame(&follow, &frame, stamp);
                    }
                }
            } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
                       event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR) {
                uart_flush_input(RADAR_UART);
                xQueueReset(s_uart_events);
                parser.used = 0;
                parser.rejected++;
            }
        }
        uint64_t now = now_ms();
        robot_follow_tick(&follow, now);
        command_t command;
        /* Bound command work so a busy CDC client cannot starve motion/UART. */
        for (int k = 0; k < 4 && xQueueReceive(s_commands, &command, 0); ++k) {
            const char *cmd = command.line;
            if (!strcmp(cmd, "status")) {
                print_status(&follow, armed, following, hardware_fault, &parser, angle);
                continue;
            }
            if (!strcmp(cmd, "disarm")) {
                esp_err_t err = servo_write(0);
                armed = following = false;
                last_pulse = 0;
                hardware_fault = err != ESP_OK;
                cdc_write(err == ESP_OK ? "OK robot disarmed" : "ERR servo disable failed");
                continue;
            }
            if (!strcmp(cmd, "arm")) {
                if (armed) { cdc_write("ERR already armed; disarm before re-centering"); continue; }
                /* First pulse is neutral. Install the horn AFTER centering;
                 * no sensor provides the shaft's actual initial angle. */
                esp_err_t err = servo_write(follow.config.center_us);
                if (err != ESP_OK) {
                    hardware_fault = true;
                    cdc_write("ERR servo arm failed");
                    continue;
                }
                armed = true;
                following = false;
                hardware_fault = false;
                angle = manual_goal = 0;
                last_pulse = follow.config.center_us;
                cdc_write("OK robot armed at neutral; use robot follow after calibration");
                continue;
            }
            if (!strcmp(cmd, "follow") || !strcmp(cmd, "center") || !strncmp(cmd, "angle:", 6)) {
                int requested = 0;
                if (!strncmp(cmd, "angle:", 6) &&
                    (!number(cmd + 6, &requested) || abs(requested) > follow.config.limit_deg)) {
                    cdc_write("ERR angle outside configured limit");
                    continue;
                }
                if (!armed) { cdc_write("ERR robot is disarmed; use robot arm"); continue; }
                following = !strcmp(cmd, "follow");
                manual_goal = requested;
                cdc_write("OK robot motion mode set");
                continue;
            }
            if (!strcmp(cmd, "save")) {
                cdc_write(save_config(&follow.config) == ESP_OK ? "OK robot saved" : "ERR robot save failed");
                continue;
            }
            if (armed) { cdc_write("ERR disarm before changing calibration"); continue; }
            robot_config_t next = follow.config;
            int value;
            const char *colon = strchr(cmd, ':');
            if (!colon || !number(colon + 1, &value)) {
                cdc_write("ERR unknown robot command (try robot help)"); continue;
            }
            if (!strncmp(cmd, "center_us:", 10)) next.center_us = value;
            else if (!strncmp(cmd, "limit:", 6)) next.limit_deg = value;
            else if (!strncmp(cmd, "speed:", 6)) next.speed_dps = value;
            else if (!strncmp(cmd, "reverse:", 8) && (value == 0 || value == 1)) next.reverse = value;
            else { cdc_write("ERR unknown robot setting"); continue; }
            if (!robot_config_valid(&next)) {
                cdc_write("ERR invalid calibration: keep full pulse range inside 1000..2000us"); continue;
            }
            /* Expire old target/filter state after coordinate/limit changes. */
            robot_follow_init(&follow, &next, now_ms());
            angle = manual_goal = 0;
            cdc_write("OK robot calibration set (robot save to persist)");
        }
        angle = robot_slew(angle, following ? follow.goal_deg : manual_goal,
                           follow.config.speed_dps, (now - last_step) / 1000.0f);
        last_step = now;
        if (armed) {
            int pulse = robot_pulse_us(&follow.config, angle);
            if (abs(pulse - last_pulse) >= 3) {
                esp_err_t err = servo_write(pulse);
                if (err == ESP_OK) last_pulse = pulse;
                else {
                    (void)servo_write(0);
                    armed = following = false;
                    hardware_fault = true;
                    ESP_LOGE(TAG, "servo update failed: %s", esp_err_to_name(err));
                }
            }
        }
        robot_status_t status = {
            .state = hardware_fault ? ROBOT_FAULT : follow.state,
            .armed = armed, .following = following, .hardware_fault = hardware_fault,
            .angle_deg = angle, .target_deg = follow.goal_deg,
            .x_mm = (int)follow.x_mm, .y_mm = (int)follow.y_mm,
            .frames = parser.frames, .rejected = parser.rejected, .config = follow.config,
        };
        portENTER_CRITICAL(&s_status_lock);
        s_status = status;
        portEXIT_CRITICAL(&s_status_lock);
    }
}

/* Reserve screen pins, native USB, flash, strapping pins, and UART0 TX (ROM
 * boot log on GPIO21). GPIO21 is allowed ONLY as radar TX: radar RX tolerates
 * stray boot bytes, whereas a servo must never receive ROM log pulses. */
static bool available_gpio(int pin)
{
    return pin == 6 || pin == 7 || pin == 10 || pin == 20 || pin == 21;
}
esp_err_t robot_start(void)
{
    if (s_commands) return ESP_ERR_INVALID_STATE;
    int rx = CONFIG_ROBOT_RADAR_RX_GPIO, tx = CONFIG_ROBOT_RADAR_TX_GPIO;
    int servo = CONFIG_ROBOT_SERVO_GPIO;
    if (!available_gpio(rx) || !available_gpio(tx) || !available_gpio(servo) ||
        rx == tx || rx == servo || tx == servo || servo == 21 || rx == 21)
        return ESP_ERR_INVALID_ARG;
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = SERVO_TIMER,
        .duty_resolution = SERVO_BITS, .freq_hz = CONFIG_ROBOT_SERVO_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    ledc_channel_config_t channel = {
        .gpio_num = servo, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SERVO_CHANNEL, .timer_sel = SERVO_TIMER, .duty = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) return err;
    uart_config_t uart = {
        .baud_rate = CONFIG_ROBOT_RADAR_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_param_config(RADAR_UART, &uart);
    if (err == ESP_OK) err = uart_set_pin(RADAR_UART, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    err = uart_driver_install(RADAR_UART, 2048, 0, 16, &s_uart_events, 0);
    if (err != ESP_OK) return err;
    s_commands = xQueueCreate(8, sizeof(command_t));
    if (!s_commands) { uart_driver_delete(RADAR_UART); return ESP_ERR_NO_MEM; }
    if (xTaskCreate(robot_task, "robot", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_commands);
        s_commands = NULL;
        uart_driver_delete(RADAR_UART);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "LD2450 RX=%d TX=%d, servo=%d %dHz (DISARMED)", rx, tx, servo, CONFIG_ROBOT_SERVO_HZ);
    return ESP_OK;
}
bool robot_command(const char *line)
{
    if (strncmp(line, "robot", 5) || (line[5] && line[5] != ' ')) return false;
    if (!strcmp(line, "robot") || !strcmp(line, "robot help")) {
        cdc_write("robot: status | arm | disarm | follow | center | angle:<deg>");
        cdc_write("disarmed settings: center_us:<1400..1600> | limit:<5..45> | reverse:<0|1> | speed:<5..90> | save");
        return true;
    }
    if (!s_commands) { cdc_write("ERR robot unavailable"); return true; }
    command_t cmd = {0};
    if (strlen(line + 6) >= sizeof cmd.line) { cdc_write("ERR robot command too long"); return true; }
    strcpy(cmd.line, line + 6);
    if (xQueueSend(s_commands, &cmd, 0) != pdTRUE) cdc_write("ERR robot command queue full; retry");
    return true;
}
#else
esp_err_t robot_start(void) { return ESP_ERR_NOT_SUPPORTED; }
void robot_get_status(robot_status_t *s) { memset(s, 0, sizeof *s); }
bool robot_command(const char *line) { (void)line; return false; }
#endif
