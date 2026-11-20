#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { APP_PROFILE_AUTO, APP_PROFILE_SCREEN, APP_PROFILE_ROBOT } app_hardware_profile_t;
typedef enum { APP_HW_UNKNOWN, APP_HW_INITIALIZED, APP_HW_NOT_DETECTED,
               APP_HW_DETECTED, APP_HW_DISABLED, APP_HW_ERROR } app_hardware_state_t;
typedef struct { bool radar, servo; unsigned physical_keys; } app_hardware_policy_t;
typedef struct {
    app_hardware_profile_t active, configured;
    app_hardware_state_t screen, radar, servo;
    bool drivers_compiled; /* always true: the one universal image has the radar and servo drivers; kept for older PC tools */
    bool restart_required;
    unsigned physical_keys;
    uint32_t radar_frames;
} app_hardware_info_t;
const char *app_hardware_profile_name(app_hardware_profile_t profile);
bool app_hardware_profile_parse(const char *name, app_hardware_profile_t *profile);
app_hardware_policy_t app_hardware_policy(app_hardware_profile_t profile);
/* Runtime services: changing the saved profile takes effect after restart. */
app_hardware_profile_t app_hardware_active(void);
void app_hardware_info(app_hardware_info_t *out);
bool app_hardware_set_profile(app_hardware_profile_t profile);
void app_device_restart(void);
