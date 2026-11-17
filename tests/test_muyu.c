#include "muyu_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static muyu_view_t view_at(muyu_t *m, uint64_t now)
{
    muyu_view_t v;
    muyu_tick(m, now);
    muyu_snapshot(m, &v);
    return v;
}
static void fresh_state(void)
{
    muyu_t m;
    muyu_start(&m, 5000);
    muyu_view_t v = view_at(&m, 5000);
    assert(v.count == 0 && v.rounds == 0 && !v.auto_on && !v.shown[0] && !v.shown[1]);
    assert(v.since_knock_ms == MUYU_AGE_MAX && v.age_ms[0] == MUYU_AGE_MAX && v.age_ms[1] == MUYU_AGE_MAX);
    muyu_tick(&m, 600000); /* nothing happens by itself while auto mode is off */
    v = view_at(&m, 600000);
    assert(v.count == 0);
}
static void knocking(void)
{
    muyu_t m;
    muyu_start(&m, 0);
    assert(muyu_knock(&m, 100) && muyu_knock(&m, 200) && muyu_knock(&m, 300));
    muyu_view_t v = view_at(&m, 350);
    assert(v.count == 3 && v.shown[0] && v.shown[1]);
    assert(v.age_ms[1] == 50 && v.age_ms[0] == 150); /* knock 3 re-used slot 1, knock 2 is in slot 0 */
    assert(v.since_knock_ms == 50);
    v = view_at(&m, 100000);
    assert(v.age_ms[0] == MUYU_AGE_MAX && v.age_ms[1] == MUYU_AGE_MAX && v.since_knock_ms == MUYU_AGE_MAX); /* saturates */
    assert(v.count == 3);
    muyu_tick(&m, 50); /* time never runs backwards */
    muyu_snapshot(&m, &v);
    assert(v.age_ms[1] == MUYU_AGE_MAX && v.count == 3);
}
static void rounds(void)
{
    muyu_t m;
    muyu_start(&m, 0);
    for (unsigned i = 1; i <= MUYU_ROUND - 1; ++i) assert(muyu_knock(&m, i * 10U));
    muyu_view_t v = view_at(&m, 2000);
    assert(v.rounds == 0 && !v.round[0] && !v.round[1]);
    assert(muyu_knock(&m, 2100)); /* the 108th */
    v = view_at(&m, 2150);
    assert(v.count == MUYU_ROUND && v.rounds == 1 && v.round[MUYU_ROUND & 1U] && !v.round[(MUYU_ROUND + 1U) & 1U]);
    assert(muyu_knock(&m, 2200));
    v = view_at(&m, 2250);
    assert(v.rounds == 1 && v.round[MUYU_ROUND & 1U] && !v.round[(MUYU_ROUND + 1U) & 1U]); /* still shown beside the next one */
    assert(muyu_knock(&m, 2300));
    v = view_at(&m, 2350);
    assert(!v.round[0] && !v.round[1]); /* the round marker was overwritten by knock 110 */
    for (unsigned i = 0; i < MUYU_ROUND - 2U; ++i) assert(muyu_knock(&m, 3000U + i * 10U));
    v = view_at(&m, 5000);
    assert(v.count == 2U * MUYU_ROUND && v.rounds == 2);
}
static void saturation(void)
{
    muyu_t m;
    muyu_start(&m, 0);
    m.count = MUYU_COUNT_MAX - 1U;
    assert(muyu_knock(&m, 10));
    assert(!muyu_knock(&m, 20) && !muyu_knock(&m, 30));
    muyu_view_t v = view_at(&m, 40);
    assert(v.count == MUYU_COUNT_MAX);
    assert(v.age_ms[MUYU_COUNT_MAX & 1U] == 30); /* the refused knocks did not restart the animation */
    muyu_toggle_auto(&m, 50);
    muyu_tick(&m, 5000);
    assert(view_at(&m, 5000).count == MUYU_COUNT_MAX);
}
static unsigned auto_run(unsigned step, uint64_t end)
{
    muyu_t m;
    muyu_start(&m, 0);
    muyu_toggle_auto(&m, 1000);
    unsigned most_in_one_tick = 0;
    for (uint64_t now = 1000; now <= end; now += step) {
        uint32_t before = m.count;
        muyu_tick(&m, now);
        if (m.count - before > most_in_one_tick) most_in_one_tick = m.count - before;
    }
    assert(most_in_one_tick <= 1);
    return m.count;
}
static void auto_mode(void)
{
    /* Ten seconds of auto mode is ten knocks however often the app is ticked (each of these steps divides 11000). */
    static const unsigned steps[] = {1, 5, 8, 40, 125, 250, 1000};
    for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; ++i) assert(auto_run(steps[i], 11000) == 10);
    muyu_t m;
    muyu_start(&m, 0);
    muyu_toggle_auto(&m, 0);
    muyu_tick(&m, 999);
    assert(view_at(&m, 999).count == 0 && view_at(&m, 1000).count == 1); /* exactly one interval after switching on */
    assert(muyu_knock(&m, 1200) && view_at(&m, 1999).count == 2 && view_at(&m, 2000).count == 3); /* manual knocks do not shift the beat */
    muyu_toggle_auto(&m, 2100);
    assert(!view_at(&m, 2100).auto_on);
    assert(view_at(&m, 9000).count == 3); /* off again: silence */
    muyu_toggle_auto(&m, 9000);
    assert(view_at(&m, 9999).count == 3 && view_at(&m, 10000).count == 4);
}
static void stalls(void)
{
    muyu_t m;
    muyu_start(&m, 0);
    muyu_toggle_auto(&m, 0);
    muyu_tick(&m, 500);
    muyu_tick(&m, 10500); /* the app was stopped for ten seconds: no backlog of knocks */
    assert(view_at(&m, 10500).count == 0);
    assert(view_at(&m, 10999).count == 0 && view_at(&m, 11000).count == 1);
    muyu_t n;
    muyu_start(&n, 0);
    muyu_toggle_auto(&n, 0);
    muyu_tick(&n, 500);
    muyu_tick(&n, 3000); /* a long frame, shorter than a stall: one knock, not three */
    assert(view_at(&n, 3000).count == 1);
    muyu_tick(&n, 3999);
    assert(view_at(&n, 3999).count == 1 && view_at(&n, 4000).count == 2);
}
static unsigned next(unsigned *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state >> 8;
}
static void stress(void)
{
    muyu_t m;
    unsigned seed = 24U;
    uint64_t now = 0;
    muyu_start(&m, 0);
    uint32_t last = 0;
    for (unsigned i = 0; i < 400000; ++i) {
        unsigned roll = next(&seed) % 100U;
        now += 1U + next(&seed) % (roll == 0U ? 9000U : 90U);
        if (roll < 20) (void)muyu_knock(&m, now);
        else if (roll < 22) muyu_toggle_auto(&m, now);
        else muyu_tick(&m, now);
        muyu_view_t v;
        muyu_snapshot(&m, &v);
        assert(v.count >= last && v.count <= MUYU_COUNT_MAX && v.rounds == v.count / MUYU_ROUND);
        assert(v.age_ms[0] <= MUYU_AGE_MAX && v.age_ms[1] <= MUYU_AGE_MAX);
        assert(v.since_knock_ms <= v.age_ms[0] && v.since_knock_ms <= v.age_ms[1]);
        assert(v.count == 0U || v.shown[v.count & 1U]);
        last = v.count;
    }
}
int main(void)
{
    fresh_state();
    knocking();
    rounds();
    saturation();
    auto_mode();
    stalls();
    stress();
    puts("muyu: knocks, 108-round marker, saturation, auto cadence independent of tick rate, stalls and stress passed");
    return 0;
}
