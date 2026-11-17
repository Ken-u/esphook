#pragma once
#include "app.h"
#include "app_alarm.h"


struct app_manager {
    const app_descriptor_t *const *catalog;
    size_t catalog_count, active, standby, selected, pending;
    bool idle, pending_idle, pending_set, ready, dirty, triple_blocked;
    uint64_t activity_ms, rendered_ms, alarm_ms;
    unsigned exit_hold_ms, alarm_id; /* alarm_id: an alarm that started and has not been announced to its app yet */
    app_input_t input[3];
    app_platform_t platform;
    app_context_t context;
    union { max_align_t alignment; unsigned char bytes[APP_STATE_CAPACITY]; } storage;
};
/* Id of the app flagged APP_STANDBY|APP_DEFAULT_STANDBY, or `fallback` when the catalog has none. */
const char *app_manager_default_standby(const app_descriptor_t *const *catalog, size_t count, const char *fallback);
bool app_manager_init(app_manager_t *m, const app_descriptor_t *const *catalog,
                      size_t count, const app_platform_t *platform,
                      const char *standby_id, uint64_t now_ms);
void app_manager_key(app_manager_t *m, unsigned key, bool down, uint64_t ms);
void app_manager_cancel_key(app_manager_t *m, unsigned key, uint64_t ms);
void app_manager_event(app_manager_t *m, const app_event_t *event);
void app_manager_tick(app_manager_t *m, uint64_t ms);
void app_manager_render(app_manager_t *m);
/* Navigation is deferred until the current app callback has returned. */
void app_manager_commit(app_manager_t *m);
