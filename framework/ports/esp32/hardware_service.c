#include "app_hardware.h"
#include "config_store.h"
#include "robot_runtime.h"
#include "display_view.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include <string.h>
static app_hardware_profile_t s_active;
void app_hardware_init(void)
{
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    s_active=config_get_hardware_profile();
#else
    s_active=APP_PROFILE_SCREEN;
#endif
}
app_hardware_profile_t app_hardware_active(void) { return s_active; }
bool app_hardware_set_profile(app_hardware_profile_t profile)
{
#ifndef CONFIG_DESKTOP_FOLLOW_ROBOT
    if(profile!=APP_PROFILE_SCREEN) return false;
#endif
    return config_set_hardware_profile(profile)==ESP_OK;
}
void app_hardware_info(app_hardware_info_t *out)
{
    memset(out,0,sizeof *out);out->active=s_active;
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    out->drivers_compiled=true;out->configured=config_get_hardware_profile();
#else
    out->configured=APP_PROFILE_SCREEN;
#endif
    out->restart_required=out->active!=out->configured;
    app_hardware_policy_t policy=app_hardware_policy(s_active);out->physical_keys=policy.physical_keys;
    out->screen=dv_is_initialized()?APP_HW_INITIALIZED:APP_HW_UNKNOWN;
    robot_status_t status;robot_get_status(&status);out->radar_frames=status.frames;
    out->radar=!policy.radar?APP_HW_DISABLED:!status.radar_ready?APP_HW_ERROR:
        status.radar_online?APP_HW_DETECTED:APP_HW_NOT_DETECTED;
    out->servo=s_active==APP_PROFILE_SCREEN?APP_HW_DISABLED:!policy.servo?APP_HW_UNKNOWN:
        !status.servo_ready || status.hardware_fault?APP_HW_ERROR:APP_HW_INITIALIZED;
}
void app_device_restart(void) { esp_restart(); }
