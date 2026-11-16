#pragma once
#include "app.h"
#include "robot_runtime.h"
#include "robot_ui.h"

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
    int limit_deg;
} app_servo_t;
typedef enum { APP_SERVO_ARM, APP_SERVO_DISARM, APP_SERVO_FOLLOW,
               APP_SERVO_CENTER, APP_SERVO_ANGLE } app_servo_action_t;
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
void app_servo_read(app_servo_t *out);
/* Queued, bounded and rate-limited by the motion service. Never auto-arms.
 * The first request leases the servo to this app; leaving disarms its lease. */
bool app_servo_request(app_context_t *ctx, app_servo_action_t action, int value);
void app_display_read(robot_ui_t *out);
bool app_display_update(const char *json, size_t length, char *error, size_t error_size);
void app_hook_read(app_hook_t *out);
bool app_hook_reply(unsigned key);
void app_key_word(unsigned key, char *out, size_t size);
void app_device_info(app_device_info_t *out);
uint32_t app_random(void);
uint64_t app_clock_ms(void);
bool app_transport_write(const char *line);
/* Bounded USB app_event; no credentials and no UI work in transport callbacks. */
bool app_host_event(const app_context_t *ctx, const char *action, unsigned key, const char *text);
