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
void app_set_triple_exit(app_context_t *c, bool enabled) { c->manager->triple_blocked = !enabled; }
bool app_triple_exit(const app_context_t *c) { return !c->manager->triple_blocked; }
/* The double click works wherever the triple exit does: an app that is played by mashing the key has no use for either. */
void app_set_double_back(app_context_t *c, bool enabled) { c->manager->double_blocked = !enabled; }
bool app_double_back(const app_context_t *c) { return !c->manager->double_blocked && !c->manager->triple_blocked; }
/* "Back" from an app is the app before it; from the menu it is the app the menu was opened from. */
static const app_recent_t *back_target(const app_manager_t *m, bool from_menu)
{
    const app_recent_t *r = &m->recent[from_menu ? 0 : 1];
    return r->valid ? r : NULL;
}
bool app_back(app_context_t *c)
{
    const app_recent_t *r = back_target(c->manager, c->manager->active == c->manager->launcher);
    if (!r) return false;
    request(c, r->app, r->idle);
    return true;
}
void app_set_exit_hold(app_context_t *c, unsigned ms)
{
    app_manager_t *m = c->manager;
    m->exit_hold_ms = ms == 0 ? 0 : ms < 1000U ? 1000U : ms > 10000U ? 10000U : ms;
    if (!m->exit_hold_ms) m->exit_armed = false; /* switching it off drops the press under way: turning it on again does not revive it */
}
unsigned app_exit_hold_progress(const app_context_t *c)
{
    const app_manager_t *m = c->manager;
    const app_input_t *i = &m->input[0];
    if (!m->exit_hold_ms || !m->exit_armed || !i->down || i->blocked || c->now_ms < i->down_ms) return 0;
    const uint64_t held = c->now_ms - i->down_ms;
    return held > m->exit_hold_ms ? m->exit_hold_ms : (unsigned)held;
}
void app_manager_commit(app_manager_t *m)
{
    if (!m->pending_set) return;
    size_t target = m->pending; bool idle = m->pending_idle;
    bool first_run = m->first_run_next; /* the very first entry of a boot, or the next first run after one that has ended */
    m->pending_set = false; m->first_run_next = false;
    /* A first run that ends with app_idle() hands over to the next app (in catalog order) that has something to set up;
     * only when none is left does the board go on to the standby screen. Going forward only means that a run left
     * without storing anything is not offered again before the next boot. */
    if (m->ready && m->context.first_run && idle && target == m->standby) {
        for (size_t i = m->active + 1; i < m->catalog_count; ++i)
            if (m->catalog[i]->first_run && m->catalog[i]->first_run()) { target = i; idle = false; first_run = true; break; }
    }
    if (m->ready) {
        if (m->context.app->on_leave) m->context.app->on_leave(&m->context);
        app_alarm_leaving(m->context.app->id); /* leaving an app answers the alarm that was ringing in it */
        if (m->platform.release_services) m->platform.release_services(m->context.app->id, m->platform.user);
        if (m->context.root && m->platform.scene_delete) m->platform.scene_delete(m->context.root, m->platform.user);
    }
    m->active = target; m->idle = idle; m->triple_blocked = false; m->double_blocked = false; m->exit_hold_ms = 0; m->exit_armed = false;
    m->dbl_pending = false; /* a double click that was waiting for its quiet moment belongs to the screen it was made on */
    if (target != m->launcher && !first_run) {
        if (!m->recent[0].valid || m->recent[0].app != target) { m->recent[1] = m->recent[0]; m->recent[0].app = target; m->recent[0].valid = true; }
        m->recent[0].idle = idle;
    }
    memset(&m->storage, 0, sizeof m->storage);
    m->context.app = m->catalog[target]; m->context.standby = idle; m->context.first_run = first_run;
    m->context.state = m->storage.bytes;
    m->context.root = m->platform.scene_create ? m->platform.scene_create(m->platform.user) : NULL;
    for (unsigned key = 0; key < 3; ++key) app_input_consume(&m->input[key]);
    m->ready = true; m->dirty = true; m->activity_ms = m->context.now_ms;
    if (m->platform.activate_services) m->platform.activate_services(&m->context, m->platform.user);
    if (m->context.app->on_enter) m->context.app->on_enter(&m->context);
}
const char *app_manager_default_standby(const app_descriptor_t *const *catalog, size_t count, const char *fallback)
{
    for (size_t i = 0; catalog && i < count; ++i)
        if (catalog[i] && (catalog[i]->flags & (APP_STANDBY | APP_DEFAULT_STANDBY)) == (APP_STANDBY | APP_DEFAULT_STANDBY))
            return catalog[i]->id;
    return fallback;
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
    m->launcher = launcher; m->standby = standby; m->context.manager = m; m->context.now_ms = ms;
    /* A first run (descriptor->first_run) comes before the standby screen; it ends with app_idle(). */
    size_t first = count;
    for (size_t i = 0; i < count && first == count; ++i)
        if (catalog[i]->first_run && catalog[i]->first_run()) first = i;
    m->first_run_next = first != count;
    request(&m->context, first != count ? first : standby, first == count); app_manager_commit(m); return true;
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
/* Double click, K1 only: two clicks no more than APP_DOUBLE_MS apart, then a quiet moment of the same length. The
 * quiet moment is what keeps a quick triple press a triple (the menu) instead of a double that left first; both
 * clicks reach the app as ordinary presses, and only then is it taken back. The gesture is measured from the screen it
 * began on: the first click of a double on a screen that opens the menu on a click (the standby screen, radar) must
 * not make "back" mean the app that click has just left. */
static void double_press(app_manager_t *m, uint64_t ms)
{
    m->dbl_pending = false; /* a further press: whatever the pair was, it is no double (three of them are the menu) */
    if (m->triple_blocked || m->double_blocked) { m->dbl_clicks = 0; return; }
    if (m->dbl_clicks == 1 && ms >= m->dbl_first_ms && ms - m->dbl_first_ms <= APP_DOUBLE_MS) { m->dbl_clicks = 2; return; }
    m->dbl_clicks = 1; m->dbl_first_ms = ms;
    m->dbl_launcher = m->active == m->launcher; m->dbl_selected = m->selected;
}
static void double_release(app_manager_t *m, unsigned action, uint64_t ms)
{
    if (!(action & INPUT_SHORT)) { m->dbl_clicks = 0; return; } /* a hold, or the press that was the menu, is not a click */
    if (m->dbl_clicks == 2) { m->dbl_pending = true; m->dbl_due_ms = ms + APP_DOUBLE_MS; }
}
static void double_fire(app_manager_t *m, uint64_t ms)
{
    m->dbl_pending = false; m->dbl_clicks = 0;
    if (m->triple_blocked || m->double_blocked) return; /* the app on screen has opted out since */
    const app_recent_t *r = back_target(m, m->dbl_launcher);
    if (!r) return; /* nothing to go back to: the pair of clicks stays what it was */
    if (m->active == m->launcher) m->selected = m->dbl_selected; /* the clicks that were a double must not have moved the menu cursor */
    request(&m->context, r->app, r->idle);
    m->activity_ms = ms; m->dirty = true;
    app_manager_commit(m);
}
void app_manager_key(app_manager_t *m, unsigned key, bool down, uint64_t ms)
{
    if (!m->ready || key >= 3) return;
    if (key || m->triple_blocked) m->input[key].clicks = 0; /* Triple navigation belongs only to K1, and an app may switch it off. */
    const bool was_down = m->input[key].down;
    unsigned action = app_input_edge(&m->input[key], down, ms);
    if (!key && down && !was_down) { m->exit_armed = m->exit_hold_ms != 0; double_press(m, ms); } /* only a press that began with the exit hold on counts */
    if (key) action &= ~INPUT_MENU;
    dispatch(m, key, action, ms);
    if (!key && !down && was_down) double_release(m, action, ms);
}
/* The alarm registry (app_alarm.h) is looked at a few times a second. What starts opens its target app, whatever is on
 * screen, and the target is told right after it has been entered (see announce_alarm). */
static void poll_alarms(app_manager_t *m, uint64_t ms)
{
    if (m->pending_set || m->alarm_id || (m->alarm_ms && ms >= m->alarm_ms && ms - m->alarm_ms < APP_ALARM_POLL_MS)) return;
    m->alarm_ms = ms ? ms : 1U;
    app_time_t now = {0};
    if (m->platform.time_read) m->platform.time_read(&now, m->platform.user);
    unsigned id;
    if (!app_alarm_poll(&now, ms, m->idle, &id)) return;
    app_alarm_info_t info;
    const size_t target = app_alarm_get(id, &info) ? find(m, info.target) : m->catalog_count;
    if (target == m->catalog_count) { app_alarm_done(id); return; } /* its app is gone: nothing to open */
    m->activity_ms = ms; m->dirty = true;
    if (target != m->active) request(&m->context, target, false);
    if (m->catalog[target]->on_event) m->alarm_id = id; /* an app that cannot be told is only opened: nothing to answer */
    else app_alarm_done(id);
}
static void announce_alarm(app_manager_t *m, uint64_t ms)
{
    const unsigned id = m->alarm_id;
    if (!id) return;
    m->alarm_id = 0;
    app_alarm_info_t info;
    if (!app_alarm_get(id, &info) || strcmp(info.target, m->context.app->id)) { app_alarm_done(id); return; }
    app_event_t e = {.kind = APP_EVENT_ALARM, .key = id, .ms = ms, .text = info.label};
    m->context.now_ms = ms; m->dirty = true;
    m->context.app->on_event(&m->context, &e);
    app_manager_commit(m);
}
void app_manager_tick(app_manager_t *m, uint64_t ms)
{
    if (!m->ready) return;
    m->context.now_ms = ms;
    poll_alarms(m, ms);
    app_manager_commit(m);
    announce_alarm(m, ms);
    if (m->exit_hold_ms && m->exit_armed && m->input[0].down && !m->input[0].blocked && ms >= m->input[0].down_ms &&
        ms - m->input[0].down_ms >= m->exit_hold_ms) { /* held long enough: the deliberate exit of a game with no triple */
        m->activity_ms = ms; m->dirty = true;
        app_menu(&m->context); app_manager_commit(m); /* the commit consumes the held key, so its release does nothing */
        return;
    }
    if (m->dbl_pending && ms >= m->dbl_due_ms && !m->input[0].down) { double_fire(m, ms); return; }
    for (unsigned key = 0; key < 3; ++key) dispatch(m, key, app_input_tick(&m->input[key], ms), ms);
    if (!strcmp(app_active_id(&m->context), APP_LAUNCHER) && ms >= m->activity_ms &&
        ms - m->activity_ms >= APP_MENU_IDLE_MS) app_idle(&m->context);
    app_manager_commit(m);
    if (m->context.app->on_tick) m->context.app->on_tick(&m->context);
    app_manager_commit(m);
}
void app_manager_cancel_key(app_manager_t *m,unsigned key,uint64_t ms)
{
    if(!m->ready || key>=3) return;
    bool notify=m->input[key].down && !m->input[key].blocked;
    memset(&m->input[key],0,sizeof m->input[key]);
    if(!key) { m->dbl_clicks=0;m->dbl_pending=false; } /* a cancelled press is no click */
    if(notify) { app_event_t e={.kind=APP_EVENT_CANCEL,.key=key,.ms=ms};app_manager_event(m,&e); }
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
