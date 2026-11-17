#include "muyu_core.h"
#include <string.h>

void muyu_start(muyu_t *m, uint64_t now_ms)
{
    memset(m, 0, sizeof *m);
    m->clock_ms = now_ms;
}
static void advance(muyu_t *m, uint64_t now_ms)
{
    if (now_ms <= m->clock_ms) return; /* time never runs backwards */
    if (now_ms - m->clock_ms > MUYU_STALL_MS) m->auto_ms += now_ms - m->clock_ms; /* a pause delays the next auto knock */
    m->clock_ms = now_ms;
}
static bool knock(muyu_t *m)
{
    if (m->count >= MUYU_COUNT_MAX) return false;
    ++m->count;
    unsigned slot = m->count & 1U;
    m->slot_ms[slot] = m->clock_ms;
    m->slot_used[slot] = true;
    m->slot_round[slot] = m->count % MUYU_ROUND == 0U;
    return true;
}
void muyu_tick(muyu_t *m, uint64_t now_ms)
{
    advance(m, now_ms);
    if (!m->auto_on || m->clock_ms - m->auto_ms < MUYU_AUTO_MS) return;
    (void)knock(m);
    m->auto_ms += MUYU_AUTO_MS;
    if (m->clock_ms - m->auto_ms >= MUYU_AUTO_MS) m->auto_ms = m->clock_ms; /* far behind: do not catch up */
}
bool muyu_knock(muyu_t *m, uint64_t now_ms)
{
    advance(m, now_ms);
    return knock(m);
}
void muyu_toggle_auto(muyu_t *m, uint64_t now_ms)
{
    advance(m, now_ms);
    m->auto_on = !m->auto_on;
    if (m->auto_on) m->auto_ms = m->clock_ms;
}
void muyu_snapshot(const muyu_t *m, muyu_view_t *view)
{
    *view = (muyu_view_t){.count = m->count, .rounds = m->count / MUYU_ROUND, .auto_on = m->auto_on,
                          .since_knock_ms = MUYU_AGE_MAX};
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t age = m->slot_used[i] ? m->clock_ms - m->slot_ms[i] : MUYU_AGE_MAX;
        view->age_ms[i] = age > MUYU_AGE_MAX ? MUYU_AGE_MAX : (unsigned)age;
        view->shown[i] = m->slot_used[i];
        view->round[i] = m->slot_round[i];
        if (view->age_ms[i] < view->since_knock_ms) view->since_knock_ms = view->age_ms[i];
    }
}
