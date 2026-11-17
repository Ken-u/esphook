#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app_input.h"

#define APP_API_VERSION 1U
#define APP_STATE_CAPACITY 2048U
#define APP_ID_MAX 23U
#define APP_MESSAGE_MAX 128U
#define APP_LAUNCHER "launcher"

typedef struct app_manager app_manager_t;
typedef struct app_context app_context_t;
typedef struct app_descriptor app_descriptor_t;
typedef enum { APP_EVENT_DOWN, APP_EVENT_UP, APP_EVENT_SHORT, APP_EVENT_CONFIRM,
               APP_EVENT_MESSAGE, APP_EVENT_NOTIFY, APP_EVENT_NETWORK, APP_EVENT_CANCEL } app_event_kind_t;
typedef struct {
    app_event_kind_t kind;
    unsigned key;
    uint64_t ms;
    const char *text; /* Borrowed until on_event returns. Never retain this pointer. */
} app_event_t;
/* APP_DEFAULT_STANDBY (together with APP_STANDBY) makes an app the standby used when none is saved in NVS or the
 * saved one is gone; at most one app may set it, and `face` stays the fallback when none does. */
enum { APP_HIDDEN = 1U, APP_STANDBY = 2U, APP_NOTIFICATIONS = 4U, APP_DEFAULT_STANDBY = 8U };

/* All lifecycle/event/tick/render callbacks run on the UI task. command is the
 * only exception: called by the USB task; it must enqueue/copy and never use UI. */
struct app_descriptor {
    uint32_t api_version;
    const char *id, *title;
    unsigned flags, refresh_ms;
    size_t state_size;
    bool (*init)(void); /* Once at boot, before USB begins accepting commands. */
    void (*on_enter)(app_context_t *);
    void (*on_leave)(app_context_t *);
    void (*on_event)(app_context_t *, const app_event_t *);
    void (*on_tick)(app_context_t *);
    void (*render)(app_context_t *);
    void (*icon)(void *parent, int x, int y, bool selected);
    bool (*command)(const char *line);
};
struct app_context {
    app_manager_t *manager;
    const app_descriptor_t *app;
    void *state; /* Zeroed on entry; invalid after leaving. */
    void *root; /* Framework-owned LVGL root, valid only while active. */
    uint64_t now_ms;
    bool standby;
};
typedef struct {
    void *(*scene_create)(void *user);
    void (*scene_delete)(void *root, void *user);
    void (*release_services)(const char *id, void *user);
    bool (*save_standby)(const char *id, void *user);
    void *user;
} app_platform_t;

/* Read-only catalog; hidden system apps are excluded from menu/count. */
size_t app_count(const app_context_t *ctx);
const app_descriptor_t *app_at(const app_context_t *ctx, size_t index);
const app_descriptor_t *app_find(const app_context_t *ctx, const char *id);
const char *app_active_id(const app_context_t *ctx);
const char *app_standby_id(const app_context_t *ctx);
bool app_open(app_context_t *ctx, const char *id);
void app_menu(app_context_t *ctx);
void app_idle(app_context_t *ctx);
bool app_set_standby(app_context_t *ctx, const char *id);
unsigned app_hold_ms(const app_context_t *ctx);
size_t app_selection(const app_context_t *ctx);
void app_select_next(app_context_t *ctx);
/* The global exit: three quick presses of K1 go back to the menu. A game that is played by mashing the key (a cannon,
 * a pedal, a mallet) can switch it off while it needs every press, and must switch it on again whenever the player
 * has another way out (a title or game over screen, or its own long press that calls app_menu). It is on by default
 * and restored on every app change, so a forgotten call never leaves the next app without it. While it is off the
 * three presses are plain presses: DOWN/UP/SHORT/CONFIRM arrive as usual and nothing navigates. */
void app_set_triple_exit(app_context_t *ctx, bool enabled);
bool app_triple_exit(const app_context_t *ctx);
/* The deliberate way out for a game that switched the triple exit off: holding K1 for `ms` (1000..10000, 0 turns it
 * off, which is the default) goes back to the menu. The app still sees DOWN, and CONFIRM at 500 ms, like any hold, so
 * pick a time no normal play reaches and only enable it where the press that starts the hold does not itself change
 * the screen. Restored (off) on every app change; the release afterwards is consumed. */
void app_set_exit_hold(app_context_t *ctx, unsigned ms);
/* How long K1 has been held towards that exit (0 when it is off or the key is up), for a progress line. */
unsigned app_exit_hold_progress(const app_context_t *ctx);
