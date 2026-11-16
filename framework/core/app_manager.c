#include "app_manager.h"
#include <string.h>

static size_t find(const app_manager_t *m, const char *id)
{
    if (!id) return m->catalog_count;
    for (size_t i = 0; i < m->catalog_count; ++i)
        if (!strcmp(m->catalog[i]->id, id)) return i;
    return m->catalog_count;
}
size_t app_count(const app_context_t *c)
{
    size_t n = 0;
    for (size_t i = 0; i < c->manager->catalog_count; ++i)
        if (!(c->manager->catalog[i]->flags & APP_HIDDEN)) ++n;
    return n;
}
const app_descriptor_t *app_at(const app_context_t *c, size_t index)
{
    for (size_t i = 0; i < c->manager->catalog_count; ++i) {
        const app_descriptor_t *a = c->manager->catalog[i];
        if (!(a->flags & APP_HIDDEN)) { if (!index) return a; --index; }
    }
    return NULL;
}
const app_descriptor_t *app_find(const app_context_t *c, const char *id)
{ size_t i = find(c->manager, id); return i < c->manager->catalog_count ? c->manager->catalog[i] : NULL; }
const char *app_active_id(const app_context_t *c) { return c->manager->catalog[c->manager->active]->id; }
const char *app_standby_id(const app_context_t *c) { return c->manager->catalog[c->manager->standby]->id; }
static void request(app_context_t *c, size_t index, bool idle)
{
    c->manager->pending = index; c->manager->pending_idle = idle;
    c->manager->pending_set = true;
}
bool app_open(app_context_t *c, const char *id)
{
    size_t i = find(c->manager, id);
    if (i == c->manager->catalog_count) return false;
    request(c, i, false); return true;
}
void app_menu(app_context_t *c) { (void)app_open(c, APP_LAUNCHER); }
void app_idle(app_context_t *c) { request(c, c->manager->standby, true); }
bool app_set_standby(app_context_t *c, const char *id)
{
    size_t i = find(c->manager, id);
    if (i == c->manager->catalog_count || !(c->manager->catalog[i]->flags & APP_STANDBY)) return false;
    if (c->manager->platform.save_standby &&
        !c->manager->platform.save_standby(id, c->manager->platform.user)) return false;
    c->manager->standby = i; return true;
}
unsigned app_hold_ms(const app_context_t *c) { return app_input_progress(&c->manager->input[0], c->now_ms); }
size_t app_selection(const app_context_t *c) { return c->manager->selected; }
void app_select_next(app_context_t *c)
{ size_t n = app_count(c); if (n) c->manager->selected = (c->manager->selected + 1) % n; }
void app_manager_commit(app_manager_t *m)
{
    if (!m->pending_set) return;
    size_t target = m->pending; bool idle = m->pending_idle;
    m->pending_set = false;
    if (m->ready) {
        if (m->context.app->on_leave) m->context.app->on_leave(&m->context);
        if (m->platform.release_services) m->platform.release_services(m->context.app->id, m->platform.user);
        if (m->context.root && m->platform.scene_delete) m->platform.scene_delete(m->context.root, m->platform.user);
    }
    m->active = target; m->idle = idle;
    memset(&m->storage, 0, sizeof m->storage);
    m->context.app = m->catalog[target]; m->context.standby = idle;
    m->context.state = m->storage.bytes;
    m->context.root = m->platform.scene_create ? m->platform.scene_create(m->platform.user) : NULL;
    for (unsigned key = 0; key < 3; ++key) app_input_consume(&m->input[key]);
    m->ready = true; m->dirty = true; m->activity_ms = m->context.now_ms;
    if (m->context.app->on_enter) m->context.app->on_enter(&m->context);
}
bool app_manager_init(app_manager_t *m, const app_descriptor_t *const *catalog,
                      size_t count, const app_platform_t *platform,
                      const char *standby_id, uint64_t ms)
{
    if (!m || !catalog || !count || !platform) return false;
    memset(m, 0, sizeof *m); m->catalog = catalog; m->catalog_count = count; m->platform = *platform;
    for (size_t i = 0; i < count; ++i) {
        const app_descriptor_t *a = catalog[i];
        if (!a || a->api_version != APP_API_VERSION || !a->id || !a->title || !a->id[0] ||
            strlen(a->id) > APP_ID_MAX || a->state_size > APP_STATE_CAPACITY || !a->refresh_ms) return false;
        for (const char *p = a->id; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
        for (size_t k = 0; k < i; ++k) if (!strcmp(catalog[k]->id, a->id)) return false;
    }
    size_t launcher = find(m, APP_LAUNCHER), standby = find(m, standby_id);
    if (launcher == count || standby == count || !(catalog[standby]->flags & APP_STANDBY)) return false;
    m->standby = standby; m->context.manager = m; m->context.now_ms = ms;
    request(&m->context, standby, true); app_manager_commit(m); return true;
}
void app_manager_event(app_manager_t *m, const app_event_t *e)
{
    if (!m->ready) return;
    m->context.now_ms = e->ms; m->dirty = true;
    if (m->context.app->on_event) m->context.app->on_event(&m->context, e);
    app_manager_commit(m);
}
static void dispatch(app_manager_t *m, unsigned key, unsigned actions, uint64_t ms)
{
    if (!actions) return;
    m->context.now_ms = ms; m->activity_ms = ms; m->dirty = true;
    if (key == 0 && (actions & INPUT_MENU)) { app_menu(&m->context); app_manager_commit(m); return; }
    if (key == 0 && m->idle && (actions & INPUT_SHORT)) { app_menu(&m->context); app_manager_commit(m); return; }
    const unsigned bits[] = {INPUT_DOWN, INPUT_CONFIRM, INPUT_UP, INPUT_SHORT};
    const app_event_kind_t kinds[] = {APP_EVENT_DOWN, APP_EVENT_CONFIRM, APP_EVENT_UP, APP_EVENT_SHORT};
    /* A transition consumes the remaining parts of a delayed release. */
    for (unsigned i = 0; i < 4; ++i) {
        if (!(actions & bits[i])) continue;
        app_event_t e = {.kind = kinds[i], .key = key, .ms = ms};
        if (m->context.app->on_event) m->context.app->on_event(&m->context, &e);
        if (m->pending_set) { app_manager_commit(m); break; }
    }
}
void app_manager_key(app_manager_t *m, unsigned key, bool down, uint64_t ms)
{
    if (!m->ready || key >= 3) return;
    if (key) m->input[key].clicks = 0; /* Triple navigation belongs only to K1. */
    unsigned action = app_input_edge(&m->input[key], down, ms);
    if (key) action &= ~INPUT_MENU;
    dispatch(m, key, action, ms);
}
void app_manager_tick(app_manager_t *m, uint64_t ms)
{
    if (!m->ready) return;
    m->context.now_ms = ms;
    for (unsigned key = 0; key < 3; ++key) dispatch(m, key, app_input_tick(&m->input[key], ms), ms);
    if (!strcmp(app_active_id(&m->context), APP_LAUNCHER) && ms >= m->activity_ms &&
        ms - m->activity_ms >= APP_MENU_IDLE_MS) app_idle(&m->context);
    app_manager_commit(m);
    if (m->context.app->on_tick) m->context.app->on_tick(&m->context);
    app_manager_commit(m);
}
void app_manager_render(app_manager_t *m)
{
    if (!m->ready) return;
    uint64_t now = m->context.now_ms;
    if (!m->dirty && now >= m->rendered_ms && now - m->rendered_ms < m->context.app->refresh_ms) return;
    m->dirty = false; m->rendered_ms = now;
    if (m->context.app->render) m->context.app->render(&m->context);
    app_manager_commit(m);
}
