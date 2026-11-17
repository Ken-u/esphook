#pragma once
/* Plain-text commands an app answers on the USB link (`robot <app> status`, `robot <app> start`, ...).
 *
 * `command(line)` runs on the USB task: match the line, reply, and at most ask the framework to change app. It must
 * not touch LVGL or the app's session state - read a published snapshot instead (see app_sync.h). Return true when
 * the line was yours (a reply, even an error, was sent), false to let the next app try.
 *
 *     static bool command(const char *line)
 *     {
 *         if (app_command_is(line, "robot dino start")) {
 *             (void)app_command_reply(app_command_open("dino") ? "OK dino queued" : "ERR dino queue full");
 *             return true;
 *         }
 *         if (app_command_is(line, "robot dino stop")) { (void)app_command_menu(); return true; }
 *         if (!app_command_is(line, "robot dino status")) return false;
 *         status_t v;
 *         app_shared_copy(v, s_status);
 *         (void)app_command_replyf("phase=%u score=%u", v.phase, v.score);
 *         return true;
 *     }
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "app_services.h"
#include "app_sync.h"

/* Replies are one line; anything longer is truncated rather than dropped. */
#define APP_COMMAND_REPLY_MAX 320

/* Ask the UI task to open the app `id` (as if chosen in the menu). False when the request queue is full or the id
 * is unknown; the switch itself happens a tick later. */
bool app_command_open(const char *id);
/* Ask the UI task to return to the menu. */
bool app_command_menu(void);
/* True while the app `id` is the foreground app. */
bool app_command_active(const char *id);

static inline bool app_command_is(const char *line, const char *text) { return !strcmp(line, text); }
static inline bool app_command_reply(const char *text) { return app_transport_write(text); }
static inline bool app_command_replyf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static inline bool app_command_replyf(const char *fmt, ...)
{
    char line[APP_COMMAND_REPLY_MAX];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    return app_transport_write(line);
}
