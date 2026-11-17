#include "app_alarm.h"
#include "app.h"
#include <string.h>

/* The registry is a handful of fixed slots: no allocation, nothing to free, and every answer is a copy. */
typedef struct {
    bool used;
    app_alarm_info_t i;
    uint32_t last_key; /* CLOCK: the minute (see minute_key) this entry last started in */
    uint64_t due_ms, resume_ms, ring_ms;
} entry_t;
_Static_assert(sizeof(((app_alarm_info_t *)0)->owner) == APP_ID_MAX + 1U, "info ids must hold an app id");
static entry_t s_slots[APP_ALARM_SLOTS];
static unsigned s_next_id;
static uint64_t s_now_ms;
static uint32_t s_key; /* the minute of the latest poll that had a clock, 0 without */
static app_alarm_stats_t s_stats;

/* Minutes of local time since 1970, plus one so that 0 can mean "never". */
static uint32_t minute_key(const app_time_t *t)
{
    const int64_t local = (int64_t)t->epoch + (int64_t)t->tz_min * 60;
    return local > 0 ? (uint32_t)(local / 60) + 1U : 1U;
}
static bool id_ok(const char *id)
{
    if (!id || !id[0] || strlen(id) > APP_ID_MAX) return false;
    for (const char *p = id; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
    return true;
}
static bool spec_ok(const app_alarm_spec_t *s)
{
    if (!s || !id_ok(s->owner) || (s->target && !id_ok(s->target)) || (s->label && strlen(s->label) > APP_ALARM_LABEL_MAX)) return false;
    if (s->window_s > 3600U || s->snooze_s > 3600U) return false;
    switch (s->kind) {
    case APP_ALARM_CLOCK:
        return s->minute <= 59U && s->day_mask && !(s->day_mask & ~APP_ALARM_EVERY_DAY) && s->hour_mask &&
               !(s->hour_mask & ~APP_ALARM_EVERY_HOUR);
    case APP_ALARM_AT: return s->epoch >= APP_EPOCH_MIN;
    case APP_ALARM_AFTER: return s->after_s >= 1U && s->after_s <= 604800U;
    default: return false;
    }
}
static entry_t *find(unsigned id)
{
    for (unsigned k = 0; id && k < APP_ALARM_SLOTS; ++k)
        if (s_slots[k].used && s_slots[k].i.id == id) return &s_slots[k];
    return NULL;
}
static void fill(entry_t *e, const app_alarm_spec_t *s)
{
    app_alarm_info_t *i = &e->i;
    memset(i->owner, 0, sizeof i->owner); memset(i->target, 0, sizeof i->target); memset(i->label, 0, sizeof i->label);
    strcpy(i->owner, s->owner);
    strcpy(i->target, s->target ? s->target : s->owner);
    if (s->label) strcpy(i->label, s->label);
    i->kind = s->kind; i->enabled = s->enabled; i->idle_only = s->idle_only; i->once = s->once;
    i->minute = s->minute; i->day_mask = s->day_mask; i->hour_mask = s->hour_mask;
    i->epoch = s->epoch; i->after_s = s->after_s; i->tag = s->tag;
    i->window_s = s->window_s ? s->window_s : APP_ALARM_DEFAULT_WINDOW_S;
    if (i->kind == APP_ALARM_CLOCK && i->window_s > 60U) i->window_s = 60U; /* it can only start inside its own minute */
    i->max_snoozes = s->max_snoozes; i->snooze_s = s->snooze_s ? s->snooze_s : APP_ALARM_DEFAULT_SNOOZE_S;
    i->state = APP_ALARM_WAITING; i->snoozes = 0;
    e->due_ms = s_now_ms + (uint64_t)s->after_s * 1000U;
    e->resume_ms = e->ring_ms = 0;
}
unsigned app_alarm_add(const app_alarm_spec_t *spec)
{
    if (!spec_ok(spec)) return 0;
    entry_t *slot = NULL;
    for (unsigned k = 0; k < APP_ALARM_SLOTS && !slot; ++k) if (!s_slots[k].used) slot = &s_slots[k];
    if (!slot) return 0;
    unsigned id = 0;
    for (unsigned tries = 0; tries < 256U && !id; ++tries) { /* ids roll round 1..255 and skip the ones in use */
        s_next_id = s_next_id % 255U + 1U;
        if (!find(s_next_id)) id = s_next_id;
    }
    if (!id) return 0;
    memset(slot, 0, sizeof *slot);
    slot->used = true; slot->i.id = id;
    fill(slot, spec);
    return id;
}
bool app_alarm_update(unsigned id, const app_alarm_spec_t *spec)
{
    entry_t *e = find(id);
    if (!e || !spec_ok(spec) || strcmp(spec->owner, e->i.owner)) return false;
    entry_t next = *e;
    fill(&next, spec);
    const app_alarm_state_t state = next.i.state; const unsigned snoozes = next.i.snoozes; /* what fill() gave: waiting, none */
    next.i.state = e->i.state; next.i.snoozes = e->i.snoozes;
    if (!memcmp(&next.i, &e->i, sizeof next.i)) return true; /* nothing changed: a ring or a snooze under way is left alone */
    next.i.state = state; next.i.snoozes = snoozes;
    next.last_key = e->last_key; /* settings changed: a pending snooze or ring is over, the minute it last rang in is not forgotten */
    *e = next;
    return true;
}
bool app_alarm_cancel(unsigned id)
{
    entry_t *e = find(id);
    if (!e) return false;
    memset(e, 0, sizeof *e);
    return true;
}
unsigned app_alarm_cancel_owner(const char *owner)
{
    unsigned n = 0;
    for (unsigned k = 0; owner && k < APP_ALARM_SLOTS; ++k)
        if (s_slots[k].used && !strcmp(s_slots[k].i.owner, owner)) { memset(&s_slots[k], 0, sizeof s_slots[k]); ++n; }
    return n;
}
bool app_alarm_get(unsigned id, app_alarm_info_t *out)
{
    const entry_t *e = find(id);
    if (!e || !out) return false;
    *out = e->i;
    return true;
}
unsigned app_alarm_list(const char *owner, unsigned *ids, unsigned max)
{
    unsigned n = 0;
    for (unsigned k = 0; k < APP_ALARM_SLOTS; ++k) {
        if (!s_slots[k].used || (owner && strcmp(s_slots[k].i.owner, owner))) continue;
        if (ids && n < max) ids[n] = s_slots[k].i.id;
        ++n;
    }
    return n;
}

/* ---- when is something due ---------------------------------------------------------------------------------------- */
static bool clock_matches(const app_alarm_info_t *i, const app_time_t *t)
{
    return (unsigned)t->minute == i->minute && ((i->hour_mask >> (unsigned)t->hour) & 1U) && ((i->day_mask >> (unsigned)t->weekday) & 1U);
}
typedef enum { NOT_DUE, DUE, EXPIRED } verdict_t;
static verdict_t judge(const entry_t *e, const app_time_t *t, uint64_t now_ms, bool idle)
{
    const app_alarm_info_t *i = &e->i;
    if (i->state == APP_ALARM_SNOOZED) return now_ms >= e->resume_ms ? DUE : NOT_DUE; /* a snooze was asked for: no window, no idle rule */
    if (i->state != APP_ALARM_WAITING || !i->enabled) return NOT_DUE;
    switch (i->kind) {
    case APP_ALARM_CLOCK:
        if (!t->valid || !clock_matches(i, t) || (unsigned)t->second >= i->window_s || e->last_key == minute_key(t)) return NOT_DUE;
        break;
    case APP_ALARM_AT:
        if (!t->valid || t->epoch < i->epoch) return NOT_DUE;
        if (t->epoch - i->epoch > i->window_s) return EXPIRED;
        break;
    default: /* APP_ALARM_AFTER */
        if (now_ms < e->due_ms) return NOT_DUE;
        if (now_ms - e->due_ms > (uint64_t)i->window_s * 1000U) return EXPIRED;
        break;
    }
    return (!i->idle_only || idle) ? DUE : NOT_DUE;
}
bool app_alarm_poll(const app_time_t *now, uint64_t now_ms, bool idle, unsigned *id)
{
    static const app_time_t no_clock = {0};
    const app_time_t *t = now ? now : &no_clock;
    s_now_ms = now_ms;
    s_key = t->valid ? minute_key(t) : 0U;
    bool ringing = false;
    for (unsigned k = 0; k < APP_ALARM_SLOTS; ++k) {
        entry_t *e = &s_slots[k];
        if (!e->used || e->i.state != APP_ALARM_RINGING) continue;
        if (now_ms >= e->ring_ms && now_ms - e->ring_ms >= APP_ALARM_RING_MAX_MS) { /* nobody answered */
            ++s_stats.timeouts;
            if (e->i.kind == APP_ALARM_CLOCK) e->i.state = APP_ALARM_WAITING; else memset(e, 0, sizeof *e);
        } else ringing = true;
    }
    for (unsigned k = 0; k < APP_ALARM_SLOTS; ++k) { /* the ones that can no longer start go first, whatever rings */
        entry_t *e = &s_slots[k];
        if (e->used && judge(e, t, now_ms, idle) == EXPIRED) { ++s_stats.missed; memset(e, 0, sizeof *e); }
    }
    if (ringing) return false; /* one at a time: the others wait, and are missed if their window runs out */
    for (unsigned pass = 0; pass < 2U; ++pass) { /* what takes the screen before what only waits for the standby screen */
        for (unsigned k = 0; k < APP_ALARM_SLOTS; ++k) {
            entry_t *e = &s_slots[k];
            if (!e->used || e->i.idle_only != (pass == 1U) || judge(e, t, now_ms, idle) != DUE) continue;
            const bool resumed = e->i.state == APP_ALARM_SNOOZED;
            e->i.state = APP_ALARM_RINGING; e->ring_ms = now_ms;
            if (!resumed) {
                e->i.snoozes = 0;
                if (e->i.kind == APP_ALARM_CLOCK) { e->last_key = minute_key(t); if (e->i.once) e->i.enabled = false; }
                else e->i.enabled = false; /* AT and AFTER happen once */
            }
            ++s_stats.fired;
            if (id) *id = e->i.id;
            return true;
        }
    }
    return false;
}

/* ---- answers ------------------------------------------------------------------------------------------------------ */
void app_alarm_done(unsigned id)
{
    entry_t *e = find(id);
    if (!e || e->i.state == APP_ALARM_WAITING) return;
    if (e->i.kind == APP_ALARM_CLOCK) { e->i.state = APP_ALARM_WAITING; e->i.snoozes = 0; }
    else memset(e, 0, sizeof *e);
}
bool app_alarm_snooze(unsigned id)
{
    entry_t *e = find(id);
    if (!e || e->i.state != APP_ALARM_RINGING) return false;
    if (e->i.snoozes >= e->i.max_snoozes) { app_alarm_done(id); return false; }
    ++e->i.snoozes;
    e->i.state = APP_ALARM_SNOOZED;
    e->resume_ms = s_now_ms + (uint64_t)e->i.snooze_s * 1000U;
    return true;
}
bool app_alarm_skip(unsigned id)
{
    entry_t *e = find(id);
    if (!e || e->i.kind != APP_ALARM_CLOCK || !s_key) return false;
    e->last_key = s_key;
    return true;
}
void app_alarm_leaving(const char *app_id)
{
    for (unsigned k = 0; app_id && k < APP_ALARM_SLOTS; ++k)
        if (s_slots[k].used && s_slots[k].i.state == APP_ALARM_RINGING && !strcmp(s_slots[k].i.target, app_id)) app_alarm_done(s_slots[k].i.id);
}
int32_t app_alarm_seconds_until(unsigned id, const app_time_t *now, uint64_t now_ms)
{
    const entry_t *e = find(id);
    if (!e) return -1;
    const app_alarm_info_t *i = &e->i;
    if (i->state == APP_ALARM_RINGING) return 0;
    if (i->state == APP_ALARM_SNOOZED) return e->resume_ms > now_ms ? (int32_t)((e->resume_ms - now_ms + 999U) / 1000U) : 0;
    if (!i->enabled) return -1;
    if (i->kind == APP_ALARM_AFTER) return e->due_ms > now_ms ? (int32_t)((e->due_ms - now_ms + 999U) / 1000U) : 0;
    if (!now || !now->valid) return -1;
    if (i->kind == APP_ALARM_AT) return i->epoch > now->epoch ? (int32_t)(i->epoch - now->epoch) : 0;
    const int sod = now->hour * 3600 + now->minute * 60 + now->second;
    for (int day = 0; day <= 7; ++day) { /* eight days reach the same weekday next week */
        if (!((i->day_mask >> (unsigned)((now->weekday + day) % 7)) & 1U)) continue;
        for (int hour = 0; hour < 24; ++hour) {
            if (!((i->hour_mask >> (unsigned)hour) & 1U)) continue;
            const int wait = day * 86400 + hour * 3600 + (int)i->minute * 60 - sod;
            if (wait > 0) return wait;
        }
    }
    return -1;
}
void app_alarm_stats(app_alarm_stats_t *out) { if (out) *out = s_stats; }
void app_alarm_reset(void)
{
    memset(s_slots, 0, sizeof s_slots);
    memset(&s_stats, 0, sizeof s_stats);
    s_next_id = 0; s_now_ms = 0; s_key = 0;
}
