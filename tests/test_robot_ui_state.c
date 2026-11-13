#include "robot_ui.h"
#include "usb_cdc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int64_t clock_us;
static char reply[200];
int64_t esp_timer_get_time(void) { return clock_us; }
esp_err_t cdc_write(const char *line) { snprintf(reply, sizeof reply, "%s", line); return ESP_OK; }
int main(void)
{
    robot_ui_t view;
    robot_ui_get(&view);
    assert(view.mode == UI_AUTO);
    assert(robot_ui_command("ui {\"v\":1,\"mode\":\"text\",\"text\":\"你好\",\"ttl_ms\":500}"));
    assert(strstr(reply, "\"ok\":true"));
    clock_us = 499000;
    robot_ui_get(&view);
    assert(view.mode == UI_TEXT && !strcmp(view.text, "你好"));
    clock_us = 500000;
    robot_ui_get(&view);
    assert(view.mode == UI_AUTO && !view.text[0]);
    assert(robot_ui_command("robot radar"));
    clock_us += INT64_C(86400000000);
    robot_ui_get(&view);
    assert(view.mode == UI_RADAR && view.range_mm == 6000); /* No implicit TTL. */
    assert(robot_ui_command("ui {\"v\":99,\"mode\":\"face\"}"));
    assert(strstr(reply, "\"ok\":false"));
    robot_ui_get(&view);
    assert(view.mode == UI_RADAR);
    assert(robot_ui_command("ui status") && strstr(reply, "\"mode\":\"radar\""));
    assert(robot_ui_command("robot face"));
    robot_ui_get(&view);
    assert(view.mode == UI_AUTO);
    assert(!robot_ui_command("robot arm")); /* Display handler never consumes motor commands. */
    assert(!robot_ui_command("ssid:network"));
    puts("robot UI state: TTL, mode replacement, USB aliases and error atomicity passed");
}
