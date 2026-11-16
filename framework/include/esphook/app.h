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
enum { APP_HIDDEN = 1U, APP_STANDBY = 2U, APP_NOTIFICATIONS = 4U };

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
