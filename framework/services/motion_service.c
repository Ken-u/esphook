#include "sdkconfig.h"
#include "robot_runtime.h"
#include "app_hardware.h"
#include "usb_cdc.h"
#include "motion_ports.h"
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
static const char *TAG = "robot";
static QueueHandle_t s_commands;
static bool s_force_disarm;
static bool s_app_motion_changed;
static robot_app_motion_t s_app_motion;
static bool s_servo_ready, s_radar_ready;
static robot_config_t s_boot_config; /* read in robot_start(), before the app manager asks for it, then owned by the task */
static int s_boot_trim;
static bool s_boot_stored;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static robot_status_t s_status = {.state = ROBOT_FAULT, .radar_age_ms = UINT32_MAX};
typedef enum { COMMAND_TEXT, COMMAND_TRIM_PREVIEW, COMMAND_TRIM_SAVE } command_kind_t;
typedef struct { char line[80]; command_kind_t kind; int trim_deg; } command_t;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
void robot_get_status(robot_status_t *out)
{
    portENTER_CRITICAL(&s_status_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_status_lock);
}
void robot_set_app_motion(robot_app_motion_t mode)
{
    portENTER_CRITICAL(&s_status_lock);
    s_app_motion = mode;
    s_app_motion_changed = true;
    portEXIT_CRITICAL(&s_status_lock);
}
/* True when a valid calibration was stored (robot save, or the calibration app): a robot that never had one is
 * offered the calibration at first boot. */
static bool load_config(robot_config_t *c, int *trim)
{
    *c = robot_default_config();
    *trim = 0;
    nvs_handle_t h;
    if (nvs_open("robot", NVS_READONLY, &h) != ESP_OK) return false;
    robot_config_t v = *c;
    int32_t value;
    uint8_t reverse;
    const bool stored = nvs_get_i32(h, "center", &value) == ESP_OK;
    if (stored) v.center_us = value;
    if (nvs_get_i32(h, "limit", &value) == ESP_OK) v.limit_deg = value;
    if (nvs_get_i32(h, "speed", &value) == ESP_OK) v.speed_dps = value;
    if (nvs_get_u8(h, "reverse", &reverse) == ESP_OK) v.reverse = reverse != 0;
    if (nvs_get_i32(h, "follow_trim", &value) == ESP_OK && robot_follow_trim_valid(value)) *trim = value;
    nvs_close(h);
    if (robot_config_valid(&v)) { *c = v; return stored; }
    ESP_LOGW(TAG, "invalid saved calibration; using defaults");
    return false;
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
static esp_err_t save_trim(int degrees)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("robot", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(h, "follow_trim", degrees);
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
             "xy=%.0f,%.0f frames=%lu bad=%lu center_us=%d limit=%d reverse=%d speed=%d dodge=%d",
             robot_state_name(f->state), armed, following ? "follow" : "manual", fault,
             (double)angle, (double)f->target_deg, (double)f->x_mm, (double)f->y_mm,
             (unsigned long)p->frames, (unsigned long)p->rejected,
             f->config.center_us, f->config.limit_deg, f->config.reverse, f->config.speed_dps,
             now_ms() < f->dodge_until_ms);
    cdc_write(buf);
}
static void receive_frame(const ld2450_frame_t *frame, uint64_t ms, void *user)
{ robot_follow_frame((robot_follow_t *)user, frame, ms); }
static void robot_task(void *arg)
{
    (void)arg;
    robot_config_t config = s_boot_config;
    bool config_stored = s_boot_stored;
    robot_follow_t follow;
    robot_follow_init(&follow, &config, now_ms());
    ld2450_frame_t raw = {0};
    bool armed = false, following = false, hardware_fault = false;
    bool auto_armed = false, auto_pending = false;
    uint32_t cal_save_seq = 0;
    bool cal_save_ok = false;
    int saved_trim = s_boot_trim, preview_trim = 0;
    bool trim_preview = false, trim_save_ok = false;
    uint32_t trim_save_seq = 0;
    robot_app_motion_t app_motion = ROBOT_APP_MOTION_NONE;
    float angle = 0, manual_goal = 0, follow_velocity = 0;
    uint64_t last_step = now_ms();
    int last_pulse = 0;
    while (1) {
        if(s_radar_ready) radar_port_poll(receive_frame, &follow);
        else vTaskDelay(pdMS_TO_TICKS(20));
        raw = *radar_port_frame();
        uint64_t now = now_ms();
        command_t command;
        portENTER_CRITICAL(&s_status_lock);
        bool force_disarm = s_force_disarm; s_force_disarm = false;
        bool motion_changed = s_app_motion_changed;
        if (motion_changed) app_motion = s_app_motion;
        s_app_motion_changed = false;
        portEXIT_CRITICAL(&s_status_lock);
        if (force_disarm || (motion_changed && (auto_armed || trim_preview))) {
            hardware_fault = servo_port_write(0) != ESP_OK;
            armed = following = auto_armed = auto_pending = false;
            trim_preview = false;
            follow_velocity = 0;
            last_pulse = 0;
        }
        if (motion_changed) auto_pending = app_motion != ROBOT_APP_MOTION_NONE;
        /* On departure skip queued movements. Keep settings saves and calibration rollback. */
        const unsigned work = force_disarm ? uxQueueMessagesWaiting(s_commands) : 4;
        for (unsigned k = 0; k < work && xQueueReceive(s_commands, &command, 0); ++k) {
            if (command.kind == COMMAND_TRIM_SAVE) {
                trim_save_ok = (saved_trim == command.trim_deg || save_trim(command.trim_deg) == ESP_OK);
                if (trim_save_ok) saved_trim = command.trim_deg;
                ++trim_save_seq;
                continue;
            }
            if (command.kind == COMMAND_TRIM_PREVIEW) {
                if (armed && !force_disarm) {
                    trim_preview = following = true;
                    preview_trim = command.trim_deg;
                }
                continue;
            }
            const char *cmd = command.line;
            if (force_disarm && strncmp(cmd, "cal:", 4) && strcmp(cmd, "status") && strcmp(cmd, "disarm")) continue;
            if (!strcmp(cmd, "status")) {
                print_status(&follow, armed, following, hardware_fault, radar_port_parser(), angle);
                continue;
            }
            if (!strcmp(cmd, "disarm")) {
                esp_err_t err = servo_port_write(0);
                armed = following = auto_armed = auto_pending = false;
                trim_preview = false;
                last_pulse = 0;
                hardware_fault = err != ESP_OK;
                cdc_write(err == ESP_OK ? "OK robot disarmed" : "ERR servo disable failed");
                continue;
            }
            if (!strcmp(cmd, "arm")) {
                if(!s_servo_ready) { cdc_write("ERR servo unavailable; select robot profile and reboot"); continue; }
                if (armed) { cdc_write("ERR already armed; disarm before re-centering"); continue; }
                /* First pulse is neutral. Install the horn AFTER centering;
                 * no sensor provides the shaft's actual initial angle. */
                esp_err_t err = servo_port_write(follow.config.center_us);
                if (err != ESP_OK) {
                    hardware_fault = true;
                    cdc_write("ERR servo arm failed");
                    continue;
                }
                armed = true;
                following = false;
                trim_preview = false;
                auto_pending = false;
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
                if(!strcmp(cmd,"follow") && (!s_radar_ready || !follow.have_frame || now-follow.frame_ms>800)) { cdc_write("ERR no detected online radar; manual servo mode remains available"); continue; }
                following = !strcmp(cmd, "follow");
                trim_preview = false;
                manual_goal = requested;
                cdc_write("OK robot motion mode set");
                continue;
            }
            if (!strncmp(cmd, "cal:", 4)) {
                /* The calibration app (robot_servo_calibrate). Unlike the text settings below it works while armed: the
                 * servo is then centred on the new neutral at once, so the change can be watched; it never arms. */
                robot_config_t next;
                bool store;
                if (!robot_cal_parse(cmd, &next, &store)) { ESP_LOGW(TAG, "calibration request rejected"); continue; }
                robot_follow_init(&follow, &next, now);
                angle = manual_goal = 0;
                following = false;
                trim_preview = false;
                if (armed) {
                    const esp_err_t err = servo_port_write(next.center_us);
                    if (err == ESP_OK) last_pulse = next.center_us;
                    else { (void)servo_port_write(0); armed = false; last_pulse = 0; hardware_fault = true; }
                }
                if (store) {
                    cal_save_ok = save_config(&next) == ESP_OK;
                    ++cal_save_seq;
                    if (cal_save_ok) config_stored = true;
                    else ESP_LOGW(TAG, "calibration not saved");
                }
                continue;
            }
            if (!strcmp(cmd, "save")) {
                const bool saved = save_config(&follow.config) == ESP_OK;
                if (saved) config_stored = true;
                cdc_write(saved ? "OK robot saved" : "ERR robot save failed");
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
            robot_follow_init(&follow, &next, now);
            angle = manual_goal = 0;
            cdc_write("OK robot calibration set (robot save to persist)");
        }
        robot_follow_set_trim(&follow, trim_preview ? preview_trim : saved_trim);
        robot_follow_tick(&follow, now);
        /* A scene default can wait for radar lock, independent of the manual request queue. Only a saved
         * calibration enables automatic output. An explicit disarm cancels this scene's pending default. */
        if (auto_pending && config_stored && s_servo_ready && !hardware_fault &&
            (app_motion == ROBOT_APP_MOTION_HAPTIC || follow.state == ROBOT_TRACKING ||
             now < follow.dodge_until_ms)) {
            auto_pending = false;
            if (!armed) {
                hardware_fault = servo_port_write(follow.config.center_us) != ESP_OK;
                armed = !hardware_fault;
                angle = 0;
                follow_velocity = 0;
                last_pulse = armed ? follow.config.center_us : 0;
            }
            auto_armed = armed;
            following = armed && app_motion == ROBOT_APP_MOTION_FOLLOW;
            manual_goal = 0;
            last_step = now;
        }
        const float elapsed = (now - last_step) / 1000.0f;
        if (following) {
            angle = robot_follow_slew(&follow.config, angle, &follow_velocity, follow.target_deg, elapsed);
        } else {
            follow_velocity = 0;
            angle = robot_slew(angle, manual_goal, follow.config.speed_dps, elapsed);
        }
        last_step = now;
        if (armed) {
            int pulse = robot_pulse_us(&follow.config, angle);
            if (abs(pulse - last_pulse) >= (following ? 1 : 3)) {
                esp_err_t err = servo_port_write(pulse);
                if (err == ESP_OK) last_pulse = pulse;
                else {
                    (void)servo_port_write(0);
                    armed = following = auto_armed = false;
                    trim_preview = false;
                    hardware_fault = true;
                    ESP_LOGE(TAG, "servo update failed: %s", esp_err_to_name(err));
                }
            }
        }
        robot_status_t status = {
            .state = hardware_fault ? ROBOT_FAULT : follow.state,
            .armed = armed, .following = following, .hardware_fault = hardware_fault,
            .servo_ready=s_servo_ready,.radar_ready=s_radar_ready,
            .config_stored = config_stored, .cal_save_seq = cal_save_seq, .cal_save_ok = cal_save_ok,
            .trim_saved_deg = saved_trim, .trim_active_deg = following ? follow.trim_deg : 0,
            .trim_aim_deg = follow.aim_deg, .trim_save_seq = trim_save_seq,
            .trim_save_ok = trim_save_ok, .trim_preview = trim_preview,
            .angle_deg = angle, .target_deg = follow.target_deg,
            .x_mm = (int)follow.x_mm, .y_mm = (int)follow.y_mm,
            .frames = radar_port_parser()->frames, .rejected = radar_port_parser()->rejected, .config = follow.config,
            .raw = raw,
            .radar_age_ms = !follow.have_frame || now - follow.frame_ms > UINT32_MAX
                            ? UINT32_MAX : (uint32_t)(now - follow.frame_ms),
            .radar_online = follow.have_frame && now - follow.frame_ms <= 800,
        };
        portENTER_CRITICAL(&s_status_lock);
        s_status = status;
        portEXIT_CRITICAL(&s_status_lock);
    }
}

/* Reserve GPIO6 for the game button, screen pins, native USB, flash, strapping pins, and UART0 TX (ROM
 * boot log on GPIO21). GPIO21 is allowed ONLY as radar TX: radar RX tolerates
 * stray boot bytes, whereas a servo must never receive ROM log pulses. */
static bool available_gpio(int pin)
{
    return pin == 7 || pin == 10 || pin == 20 || pin == 21;
}
esp_err_t robot_start(void)
{
    if (s_commands) return ESP_ERR_INVALID_STATE;
    app_hardware_profile_t profile=app_hardware_active();
    app_hardware_policy_t policy=app_hardware_policy(profile);
    if(!policy.radar && !policy.servo) {
        robot_status_t status={.state=ROBOT_IDLE,.radar_age_ms=UINT32_MAX,.config=robot_default_config()};
        portENTER_CRITICAL(&s_status_lock);s_status=status;portEXIT_CRITICAL(&s_status_lock);
        return ESP_OK;
    }
    int rx = CONFIG_ROBOT_RADAR_RX_GPIO, tx = CONFIG_ROBOT_RADAR_TX_GPIO;
    int servo = CONFIG_ROBOT_SERVO_GPIO;
    if (!available_gpio(rx) || !available_gpio(tx) || !available_gpio(servo) ||
        rx == tx || rx == servo || tx == servo || servo == 21 || rx == 21)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err;
    if(policy.servo) {
        err=servo_port_init(servo,CONFIG_ROBOT_SERVO_HZ);s_servo_ready=err==ESP_OK;
        if(err!=ESP_OK) ESP_LOGW(TAG,"optional servo driver unavailable: %s",esp_err_to_name(err));
    }
    if(policy.radar) {
        /* Auto probes only RX, preserving old display-only GPIO behavior. */
        err=radar_port_init(rx,profile==APP_PROFILE_AUTO?-1:tx,CONFIG_ROBOT_RADAR_BAUD);
        s_radar_ready=err==ESP_OK;
        if(err!=ESP_OK) ESP_LOGW(TAG,"optional radar driver unavailable: %s",esp_err_to_name(err));
    }
    s_boot_stored = load_config(&s_boot_config, &s_boot_trim);
    portENTER_CRITICAL(&s_status_lock);
    s_status.radar_ready=s_radar_ready;s_status.servo_ready=s_servo_ready;
    s_status.config=s_boot_config;s_status.config_stored=s_boot_stored;
    s_status.trim_saved_deg = s_boot_trim;
    portEXIT_CRITICAL(&s_status_lock);
    s_commands = xQueueCreate(8, sizeof(command_t));
    if (!s_commands) { if(s_radar_ready) radar_port_close(); return ESP_ERR_NO_MEM; }
    if (xTaskCreate(robot_task, "robot", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_commands);
        s_commands = NULL;
        if(s_radar_ready) radar_port_close();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,"profile=%s radar=%d servo_initialized=%d (PWM DISARMED)",app_hardware_profile_name(profile),s_radar_ready,s_servo_ready);
    return ESP_OK;
}
bool robot_servo_request(unsigned action, int value)
{
    if (!s_commands || action > 4 || (!s_servo_ready && action!=1)) return false;
    if (action == 1) {
        portENTER_CRITICAL(&s_status_lock); s_force_disarm = true; portEXIT_CRITICAL(&s_status_lock);
        return true;
    }
    if (action == 4) {
        robot_status_t status; robot_get_status(&status);
        if (value < -status.config.limit_deg || value > status.config.limit_deg) return false;
    }
    const char *names[] = {"arm", "disarm", "follow", "center"};
    command_t c = {0};
    if (action == 4) snprintf(c.line, sizeof c.line, "angle:%d", value);
    else strcpy(c.line, names[action]);
    return xQueueSend(s_commands, &c, 0) == pdTRUE;
}
bool robot_servo_calibrate(const robot_config_t *config, bool save)
{
    if (!s_commands || !s_servo_ready || !config || !robot_config_valid(config)) return false;
    command_t c = {0};
    if (robot_cal_format(c.line, sizeof c.line, config, save) >= (int)sizeof c.line) return false;
    return xQueueSend(s_commands, &c, 0) == pdTRUE;
}
bool robot_follow_trim_request(int degrees, bool save)
{
    if (!s_commands || !s_servo_ready || !s_radar_ready || !robot_follow_trim_valid(degrees)) return false;
    const command_t c = {.kind = save ? COMMAND_TRIM_SAVE : COMMAND_TRIM_PREVIEW, .trim_deg = degrees};
    return xQueueSend(s_commands, &c, 0) == pdTRUE;
}
bool robot_command(const char *line)
{
    if (strncmp(line, "robot", 5) || (line[5] && line[5] != ' ')) return false;
    if (!strcmp(line, "robot") || !strcmp(line, "robot help")) {
        cdc_write("apps: short GPIO6 opens menu, triple-click returns menu | robot menu | robot game start/stop/status");
        cdc_write("display: robot radar | robot face | ui <JSON> | ui status");
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
void robot_set_app_motion(robot_app_motion_t mode) { (void)mode; }
bool robot_servo_request(unsigned a, int v) { (void)a; (void)v; return false; }
bool robot_servo_calibrate(const robot_config_t *c, bool save) { (void)c; (void)save; return false; }
bool robot_follow_trim_request(int degrees, bool save) { (void)degrees; (void)save; return false; }
bool robot_command(const char *line) { (void)line; return false; }
#endif
