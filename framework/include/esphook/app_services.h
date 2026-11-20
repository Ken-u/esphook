#pragma once
#include "app.h"
#include "app_hardware.h"
#include "app_sound.h"
#include "app_time.h"
#include "robot_follow.h"
#include "robot_ui_protocol.h"

typedef struct {
    bool available, online;
    uint32_t age_ms, frames, rejected;
    unsigned range_mm;
    ld2450_frame_t frame;
} app_radar_t;
typedef struct {
    bool available, armed, following, fault;
    float angle_deg, target_deg;
    robot_state_t tracking;
    int limit_deg, speed_dps; /* speed_dps: the motion service's slew limit; 0 when unknown */
} app_servo_t;
typedef enum { APP_SERVO_ARM, APP_SERVO_DISARM, APP_SERVO_FOLLOW,
               APP_SERVO_CENTER, APP_SERVO_ANGLE } app_servo_action_t;
/* The servo's calibration (docs/robot-bringup.md): `config.center_us` is the pulse that centres it (1400..1600),
 * `limit_deg` the travel to each side (5..45), `speed_dps` the slew limit (5..90), `reverse` flips the direction.
 * Positive angles turn toward the sensor's +X, which is the player's left when they face the machine. `stored` is
 * false until a calibration has been saved: a robot that never had one runs the `servocal` first-run setup. */
typedef struct {
    robot_config_t config;
    bool available, stored;
    uint32_t save_seq; /* advances when a queued save completes, even if it failed */
    bool save_ok;
} app_servo_cal_t;
typedef struct {
    bool current, connected;
    unsigned clients, unread;
    int status;
    char client[24], session[48], title[32], body[128];
} app_hook_t;
typedef struct {
    char version[32], build[32], daemon_host[96], ip[16];
    unsigned daemon_port;
    bool robot_hardware;
} app_device_info_t;

void app_radar_read(app_radar_t *out);
/* Set the screen's radar radius in RAM, keeping the current display mode and expiry. */
bool app_radar_set_view_range(unsigned range_mm);
void app_servo_read(app_servo_t *out);
/* Queued, bounded and rate-limited by the motion service. Never auto-arms.
 * The first request leases the servo to this app; leaving disarms its lease. */
bool app_servo_request(app_context_t *ctx, app_servo_action_t action, int value);
/* A combination the motion service accepts (centre and travel have to fit inside 1000..2000 us together). */
static inline bool app_servo_cal_valid(const robot_config_t *config) { return config && robot_config_valid(config); }
void app_servo_cal_read(app_servo_cal_t *out);
/* Queues a calibration; with `save` it is stored. It works while the servo is armed (the servo is then centred on the new
 * neutral so the change can be watched) but never arms it, and it is refused (false) while another app holds the servo,
 * when there is no servo, for an invalid combination or when the motion service's queue is full. */
bool app_servo_cal_apply(app_context_t *ctx, const robot_config_t *config, bool save);
void app_display_read(robot_ui_t *out);
bool app_display_update(const char *json, size_t length, char *error, size_t error_size);
void app_hook_read(app_hook_t *out);
bool app_hook_reply(unsigned key);
void app_key_word(unsigned key, char *out, size_t size);
/* A sound on the passive buzzer (GPIO7, app_sound.h): queued for the buzzer's own task, so it never blocks and never
 * touches the servo's PWM. True when it was queued; false when the queue is full (the sound is simply skipped). It is
 * still silent when the player muted the device (USB `sound off`) or the board has no buzzer. Short sounds only: ask for
 * the next one when you want it (an alarm ring asks every couple of seconds), do not queue a tune ahead. */
bool app_beep(app_beep_t kind);
/* One plain note (`hz` and `ms` as app_sound_tone_valid() allows), for the buzzer test and tuners; same rules as app_beep(). */
bool app_beep_tone(unsigned hz, unsigned ms);
/* The player's mute switch (USB `sound on|off`), kept across reboots. app_beep_enable() writes flash: call it on a
 * deliberate choice only; false when the new state could not be saved (it still applies until the next reboot). */
bool app_beep_enabled(void);
bool app_beep_enable(bool on);
void app_device_info(app_device_info_t *out);
uint32_t app_random(void);
uint64_t app_clock_ms(void);
/* Local wall-clock time; `valid` is false until the PC or Wi-Fi has set it (there is no battery clock). */
void app_time_read(app_time_t *out);
/* Sets the clock (UTC epoch and the local offset): the USB `time` op, called from the USB task. */
bool app_time_set(uint32_t epoch, int tz_min);
/* A few bytes of settings per app that survive a reboot: `key` is 1..15 characters of [a-z0-9_], `size` at most
 * APP_STORAGE_MAX. Load fails (and leaves `out` alone) when nothing was saved under that key or the size differs.
 * Flash writes are slow: save on a deliberate change, never every frame. */
#define APP_STORAGE_MAX 64U
bool app_storage_load(const char *key, void *out, size_t size);
bool app_storage_save(const char *key, const void *data, size_t size);
bool app_transport_write(const char *line);
/* Bounded USB app_event; no credentials and no UI work in transport callbacks. */
bool app_host_event(const app_context_t *ctx, const char *action, unsigned key, const char *text);
