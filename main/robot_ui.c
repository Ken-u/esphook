#include "sdkconfig.h"
#include "robot_ui.h"
#include "usb_cdc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static robot_ui_t s_view;
static bool s_set;
static uint64_t s_deadline;

esp_err_t robot_ui_submit(const char *json, size_t len, char *error, size_t error_len)
{
    robot_ui_t next;
    if (!robot_ui_parse(json, len, &next, error, error_len)) return ESP_ERR_INVALID_ARG;
    uint64_t deadline = next.ttl_ms ? (uint64_t)esp_timer_get_time() / 1000 + next.ttl_ms : 0;
    portENTER_CRITICAL(&s_lock);
    s_view = next;
    s_deadline = deadline;
    s_set = true;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}
void robot_ui_get(robot_ui_t *view)
{
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_lock);
    if (s_set && s_deadline && now >= s_deadline) s_set = false;
    *view = s_set ? s_view : robot_ui_default();
    portEXIT_CRITICAL(&s_lock);
}
bool robot_ui_command(const char *line)
{
    const char *json;
    if (!strcmp(line, "robot radar")) json = "{\"v\":1,\"mode\":\"radar\"}";
    else if (!strcmp(line, "robot face")) json = "{\"v\":1,\"mode\":\"auto\"}";
    else if (!strcmp(line, "ui status")) {
        robot_ui_t v;
        robot_ui_get(&v);
        char response[100];
        snprintf(response, sizeof response, "{\"ok\":true,\"v\":1,\"mode\":\"%s\",\"range_mm\":%u}",
                 robot_ui_mode_name(v.mode), v.range_mm);
        cdc_write(response);
        return true;
    } else if (!strncmp(line, "ui ", 3)) json = line + 3;
    else return false;
    char error[96], response[140];
    if (robot_ui_submit(json, strlen(json), error, sizeof error) == ESP_OK)
        cdc_write("{\"ok\":true,\"v\":1}");
    else {
        snprintf(response, sizeof response, "{\"ok\":false,\"v\":1,\"error\":\"%s\"}", error);
        cdc_write(response);
    }
    return true;
}
#else
esp_err_t robot_ui_submit(const char *json, size_t len, char *error, size_t n)
{
    (void)json; (void)len;
    if (n) snprintf(error, n, "robot mode disabled");
    return ESP_ERR_NOT_SUPPORTED;
}
void robot_ui_get(robot_ui_t *view) { *view = robot_ui_default(); }
bool robot_ui_command(const char *line) { (void)line; return false; }
#endif
