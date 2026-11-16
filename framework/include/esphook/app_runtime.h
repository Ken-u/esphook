#pragma once
#include "app.h"
#include "events.h"
#include "esp_err.h"
extern const app_descriptor_t *const esphook_apps[];
extern const size_t esphook_app_count;
esp_err_t app_runtime_init(void);
void app_runtime_key(unsigned key, bool pressed, uint64_t now_ms);
void app_runtime_display_event(const display_event_t *event, uint64_t now_ms);
void app_runtime_tick(uint64_t now_ms);
bool app_runtime_command(const char *line);
bool app_runtime_is_active(const char *id);
bool app_runtime_open(const char *id);
bool app_runtime_menu(void);
/* Pure message copy; processed on the UI task, only if the target is active. */
bool app_runtime_message(const char *id, const char *json);
