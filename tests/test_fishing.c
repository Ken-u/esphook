/* Host tests for the fishing game (apps/fishing/firmware/fishing_core.c). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fishing_core.h"

static uint32_t g_now;
static void adv(fs_t *f, uint32_t ms)
{
    while (ms) {
        uint32_t d = ms > 5 ? 5 : ms;
        g_now += d;
        fs_update(f, g_now);
        ms -= d;
    }
}
static int absi(int v) { return v < 0 ? -v : v; }
static void press(fs_t *f) { fs_press(f, g_now); }
static void release(fs_t *f) { fs_release(f, g_now); }
/* Casts and waits for the bite, then hooks it as the given tier. */
static void hook(fs_t *f, unsigned tier)
{
    if (f->phase == FS_IDLE || f->phase == FS_RESULT) {
        if (f->phase == FS_RESULT) adv(f, FS_RESULT_MIN_MS);
        press(f);
    }
    assert(f->phase == FS_CAST);
    adv(f, FS_CAST_MS);
    assert(f->phase == FS_WAIT);
    for (unsigned i = 0; i < 2000 && f->phase == FS_WAIT; ++i) adv(f, 5);
    assert(f->phase == FS_BITE);
    f->tier = (uint8_t)tier;
    press(f);
    assert(f->phase == FS_FIGHT && f->need == fs_tier(tier)->need && f->lives == FS_LIVES);
}
/* Advances until the beat of the current pull is `offset` ms away (negative: it has passed). */
static void to_beat(fs_t *f, int offset)
{
    for (unsigned i = 0; i < 4000; ++i) {
        if (f->phase != FS_FIGHT) return;
        int t = fs_to_target(f);
        if (t != INT32_MAX && t <= offset) return;
        adv(f, 1);
    }
    assert(!"no pull came");
}
static void wait_prompt(fs_t *f)
{
    for (unsigned i = 0; i < 4000 && f->phase == FS_FIGHT && f->p.state != FP_APPROACH; ++i) adv(f, 1);
}
/* Plays the whole fight perfectly. Returns the longest/shortest gap between presses seen. */
static void bot(fs_t *f, unsigned *min_gap, unsigned *presses)
{
    uint32_t last = 0, last2 = 0; /* the global exit gesture is three presses inside 800 ms */
    while (f->phase == FS_FIGHT) {
        wait_prompt(f);
        if (f->phase != FS_FIGHT) break;
        to_beat(f, 0);
        if (f->phase != FS_FIGHT) break;
        if (last2 && g_now - last2 < *min_gap) *min_gap = g_now - last2;
        last2 = last;
        last = g_now;
        ++*presses;
        press(f);
        if (f->phase == FS_FIGHT && f->p.state == FP_HELD) { to_beat(f, 0); release(f); }
    }
}

static void test_tiers(void)
{
    unsigned weight = 0;
    for (unsigned i = 0; i < FS_TIERS; ++i) {
        const fs_tier_t *t = fs_tier(i);
        weight += t->weight;
        assert(t->min_g < t->max_g && t->win_ms >= 140 && t->approach_ms >= 560);
        if (i) {
            const fs_tier_t *p = fs_tier(i - 1);
            /* a bigger fish: more pulls, tighter timing, faster beat, harder shaking, bigger on screen, rarer */
            assert(t->need > p->need && t->win_ms < p->win_ms && t->approach_ms < p->approach_ms);
            assert(t->amp_deg > p->amp_deg && t->half_ms < p->half_ms && t->len > p->len && t->weight < p->weight);
            assert(t->min_g > p->max_g && t->hold_pct >= p->hold_pct);
        }
    }
    assert(weight == 100 && fs_tier(99) == fs_tier(FS_TIERS - 1));
    assert(fs_tier(0)->hold_pct == 0 && fs_tier(0)->need == 2 && fs_tier(4)->need == 9 && fs_tier(4)->amp_deg == 16);
}

static void test_cast_and_bite(void)
{
    fs_t f;
    g_now = 1000;
    fs_init(&f, 5, g_now);
    assert(f.phase == FS_IDLE && f.casts == 0);
    press(&f);
    assert(f.phase == FS_CAST && f.casts == 1);
    press(&f); /* mashing while the line flies does nothing */
    assert(f.phase == FS_CAST && f.casts == 1);
    adv(&f, FS_CAST_MS);
    assert(f.phase == FS_WAIT);
    /* pulling in before a bite just reels the line in */
    adv(&f, 300);
    press(&f);
    assert(f.phase == FS_IDLE && f.last_result == FR_EARLY && f.caught == 0 && f.lost == 0);
    /* a patient angler gets a bite in 1.8..4.2 s and a nibble or two before it */
    unsigned seen_nibbles = 0;
    for (unsigned round = 0; round < 40; ++round) {
        press(&f);
        adv(&f, FS_CAST_MS);
        uint32_t t0 = g_now;
        bool shook = false;
        while (f.phase == FS_WAIT) {
            adv(&f, 5);
            int a; unsigned h, d;
            if (f.phase == FS_WAIT && fs_take_shake(&f, &a, &h, &d)) { shook = true; assert(a == 2); }
        }
        assert(f.phase == FS_BITE && g_now - t0 >= FS_WAIT_MIN_MS && g_now - t0 <= FS_WAIT_MAX_MS + 10);
        seen_nibbles += f.nibbles;
        (void)shook;
        assert(f.bite_ms == 900U - f.tier * 80U);
        /* the bite is a tactile cue too */
        int amp; unsigned half, dur;
        assert(fs_take_shake(&f, &amp, &half, &dur) && amp == 4 + f.tier);
        /* ... missing it lets the fish go, the line stays in the water */
        adv(&f, f.bite_ms + 20);
        assert(f.phase == FS_WAIT && f.missed_bites == round + 1);
        press(&f); /* reel in for the next round */
        assert(f.phase == FS_IDLE);
    }
    assert(seen_nibbles >= 40);
}

static void test_perfect_fights(void)
{
    for (unsigned tier = 0; tier < FS_TIERS; ++tier) {
        fs_t f;
        g_now = 5000 + tier;
        fs_init(&f, 100 + tier, g_now);
        hook(&f, tier);
        unsigned min_gap = 100000, presses = 0;
        bot(&f, &min_gap, &presses);
        assert(f.phase == FS_LAND && f.done == fs_tier(tier)->need && f.lives == FS_LIVES);
        assert(f.caught == 1 && f.lost == 0 && f.perfect == f.done && f.combo == f.done);
        assert(presses == f.done);
        assert(f.best_g == f.weight_g && f.best_tier == tier);
        /* normal play never presses three times inside the 800 ms of the global exit gesture */
        assert(min_gap > 800);
        adv(&f, FS_LAND_MS);
        assert(f.phase == FS_RESULT);
        /* a press right at the result does nothing, a moment later it casts again */
        press(&f);
        assert(f.phase == FS_RESULT);
        adv(&f, FS_RESULT_MIN_MS);
        press(&f);
        assert(f.phase == FS_CAST && f.casts == 2);
    }
}

static void test_failures(void)
{
    fs_t f;
    g_now = 10000;
    fs_init(&f, 7, g_now);
    hook(&f, 2);
    /* the fish starts moving on its own after a random pause, not at once */
    uint32_t t0 = g_now;
    wait_prompt(&f);
    assert(g_now - t0 >= 500 && g_now - t0 <= 1100 + 10);
    int amp; unsigned half, dur;
    while (fs_take_shake(&f, &amp, &half, &dur)) {}
    /* not pressing at all: three late pulls and the line breaks */
    for (unsigned lives = FS_LIVES; lives > 0; --lives) {
        wait_prompt(&f);
        assert(f.phase == FS_FIGHT && f.lives == lives);
        adv(&f, f.p.approach_ms + f.p.win_ms + 60);
        if (lives > 1) assert(f.last_result == FR_LATE && f.lives == lives - 1 && f.phase == FS_FIGHT);
    }
    assert(f.phase == FS_LOST && f.lost == 1 && f.caught == 0 && f.done == 0);
    adv(&f, FS_LOST_MS);
    assert(f.phase == FS_IDLE);

    /* pressing while the ring is still far away is a miss; pressing between pulls is free */
    g_now += 100;
    hook(&f, 3);
    press(&f);
    assert(f.lives == FS_LIVES && f.p.state == FP_NONE);
    wait_prompt(&f);
    adv(&f, 60);
    press(&f);
    assert(f.last_result == FR_EARLY && f.lives == FS_LIVES - 1 && f.combo == 0);
    int dummy;
    assert(f.phase == FS_FIGHT && f.p.state == FP_DONE);
    (void)dummy;
}

static void test_windows(void)
{
    for (unsigned tier = 0; tier < FS_TIERS; ++tier) {
        const unsigned win = fs_tier(tier)->win_ms;
        /* inside the window: good (perfect in the middle), outside: too early */
        static const int offsets[] = {-1000, 0, 1, 2};
        (void)offsets;
        for (int trial = 0; trial < 12; ++trial) {
            fs_t f;
            g_now = 20000 + (uint32_t)trial;
            fs_init(&f, 300 + (uint32_t)trial * 7 + tier, g_now);
            hook(&f, tier);
            for (unsigned guard = 0; guard < 20 && f.p.kind != FP_TAP; ++guard) { /* find a TAP pull */
                wait_prompt(&f);
                if (f.p.kind == FP_TAP) break;
                to_beat(&f, 0); press(&f); to_beat(&f, 0); release(&f);
            }
            if (f.phase != FS_FIGHT) continue;
            wait_prompt(&f);
            if (f.p.kind != FP_TAP) continue;
            const int off = trial < 4 ? 0 : trial < 8 ? (int)win - 5 : trial < 10 ? -(int)win + 5 : (int)win + 30;
            if (off > (int)win) { adv(&f, f.p.approach_ms + win + 40); assert(f.last_result == FR_LATE); continue; }
            to_beat(&f, -off); /* off > 0: late, off < 0: early */
            const unsigned done = f.done;
            press(&f);
            assert(f.done == done + 1);
            const bool centre = absi(off) * 100 <= (int)win * 40;
            assert(f.last_result == (centre ? FR_PERFECT : FR_GOOD));
        }
    }
    /* a just-too-early press is a miss */
    fs_t f;
    g_now = 40000;
    fs_init(&f, 9, g_now);
    hook(&f, 1);
    for (unsigned guard = 0; guard < 30; ++guard) {
        wait_prompt(&f);
        if (f.p.kind == FP_TAP) break;
        to_beat(&f, 0); press(&f); to_beat(&f, 0); release(&f);
    }
    if (f.phase == FS_FIGHT && f.p.kind == FP_TAP) {
        to_beat(&f, (int)fs_tier(1)->win_ms + 12);
        const unsigned done = f.done, lives = f.lives;
        press(&f);
        assert(f.last_result == FR_EARLY && f.done == done && f.lives == lives - 1);
    }
}

static void test_hold(void)
{
    /* find a hold pull on a tier that has them */
    fs_t f;
    g_now = 60000;
    fs_init(&f, 21, g_now);
    hook(&f, 3);
    for (unsigned guard = 0; guard < 40 && f.p.kind != FP_HOLD; ++guard) {
        wait_prompt(&f);
        if (f.p.kind == FP_HOLD) break;
        to_beat(&f, 0); press(&f);
    }
    assert(f.phase == FS_FIGHT && f.p.kind == FP_HOLD);
    assert(f.p.hold_ms >= fs_tier(3)->hold_min_ms && f.p.hold_ms <= fs_tier(3)->hold_max_ms);
    const unsigned done = f.done, lives = f.lives;
    to_beat(&f, 0);
    press(&f);
    assert(f.p.state == FP_HELD && f.done == done);
    assert(fs_ring(&f) == 0 && fs_hold_fill(&f) < 40);
    adv(&f, f.p.hold_ms / 2);
    assert(fs_hold_fill(&f) > 100 && fs_hold_fill(&f) < 156);
    /* letting go in the middle is too early and costs a life */
    release(&f);
    assert(f.last_result == FR_EARLY && f.lives == lives - 1 && f.done == done);

    /* held until the right moment: success */
    wait_prompt(&f);
    for (unsigned guard = 0; guard < 40 && f.p.kind != FP_HOLD; ++guard) {
        to_beat(&f, 0); press(&f);
        if (f.p.state == FP_HELD) { to_beat(&f, 0); release(&f); }
        wait_prompt(&f);
        if (f.phase != FS_FIGHT) break;
    }
    if (f.phase == FS_FIGHT && f.p.kind == FP_HOLD) {
        const unsigned d2 = f.done;
        to_beat(&f, 0);
        press(&f);
        to_beat(&f, -(int)f.p.win_ms / 2);
        release(&f); /* a little late, but inside the window */
        assert(f.done == d2 + 1);
    }

    /* never letting go: the pull times out */
    fs_init(&f, 22, g_now);
    hook(&f, 4);
    for (unsigned guard = 0; guard < 60 && f.phase == FS_FIGHT; ++guard) {
        wait_prompt(&f);
        if (f.phase != FS_FIGHT) break;
        to_beat(&f, 0);
        press(&f);
        if (f.p.state != FP_HELD) continue; /* a tap */
        const unsigned lives = f.lives;
        adv(&f, f.p.hold_ms + f.p.win_ms + 100);
        assert(f.lives == lives - 1 && f.last_result == FR_LATE);
        break;
    }
}

static void test_shake_scales_with_fish(void)
{
    int max_amp[FS_TIERS] = {0};
    unsigned pulls[FS_TIERS] = {0};
    for (unsigned tier = 0; tier < FS_TIERS; ++tier) {
        fs_t f;
        g_now = 70000 + tier;
        fs_init(&f, 500 + tier, g_now);
        hook(&f, tier);
        int amp; unsigned half, dur;
        while (fs_take_shake(&f, &amp, &half, &dur)) {}
        while (f.phase == FS_FIGHT) {
            const unsigned prev_seq = f.shake_seq;
            wait_prompt(&f);
            if (f.phase != FS_FIGHT) break;
            if (fs_take_shake(&f, &amp, &half, &dur)) {
                if (f.shake_seq > prev_seq) ++pulls[tier];
                if (amp > max_amp[tier]) max_amp[tier] = amp;
                assert(half == fs_tier(tier)->half_ms || amp == 2); /* one rhythm per fish */
                assert(dur >= f.p.approach_ms);
            }
            to_beat(&f, 0);
            press(&f);
            if (f.p.state == FP_HELD) { to_beat(&f, 0); release(&f); }
        }
        assert(pulls[tier] >= fs_tier(tier)->need);
        assert(max_amp[tier] >= (int)fs_tier(tier)->amp_deg - 1 && max_amp[tier] <= (int)fs_tier(tier)->amp_deg + 1);
        if (tier) assert(max_amp[tier] > max_amp[tier - 1] && pulls[tier] > pulls[tier - 1]);
    }
    /* a miss jerks the line harder than a pull */
    fs_t f;
    g_now = 90000;
    fs_init(&f, 3, g_now);
    hook(&f, 2);
    int amp; unsigned half, dur;
    while (fs_take_shake(&f, &amp, &half, &dur)) {}
    wait_prompt(&f);
    while (fs_take_shake(&f, &amp, &half, &dur)) {}
    adv(&f, f.p.approach_ms + f.p.win_ms + 20);
    assert(fs_take_shake(&f, &amp, &half, &dur) && amp == fs_tier(2)->amp_deg + 4);
    assert(!fs_take_shake(&f, &amp, &half, &dur));
}

static void test_stall_and_bounds(void)
{
    fs_t f;
    g_now = 100000;
    fs_init(&f, 11, g_now);
    hook(&f, 2);
    wait_prompt(&f);
    const uint32_t before = f.phase_ms;
    fs_update(&f, g_now + 3600000U); /* the device was asleep for an hour: a pause, not a loss */
    assert(f.phase == FS_FIGHT && f.lives == FS_LIVES && f.phase_ms - before <= FS_STALL_MS);
    g_now += 3600000U;
    /* the fish stays inside the water and comes closer as it is reeled in */
    fs_init(&f, 12, g_now);
    hook(&f, 4);
    const int start_x = fs_fish_x(&f);
    while (f.phase == FS_FIGHT) {
        adv(&f, 5);
        assert(fs_fish_x(&f) >= FS_FISH_X_MIN - 1 && fs_fish_x(&f) <= FS_FISH_X_MAX + 1);
        assert(fs_fish_y(&f) >= FS_FISH_Y_MIN - 1 && fs_fish_y(&f) <= FS_FISH_Y_MAX + 1);
        const int t = fs_to_target(&f);
        if (f.p.state == FP_APPROACH && t <= 0) press(&f);
        else if (f.p.state == FP_HELD && t <= 0) release(&f);
    }
    assert(f.phase == FS_LAND && f.done == 9);
    for (unsigned i = 0; i < 100; ++i) adv(&f, 10);
    assert(fs_fish_x(&f) < start_x - 30);
}

static void test_stats(void)
{
    fs_t f;
    g_now = 200000;
    fs_init(&f, 77, g_now);
    uint32_t count[FS_TIERS] = {0};
    for (unsigned i = 0; i < 4000; ++i) {
        f.phase = FS_IDLE;
        press(&f);
        adv(&f, FS_CAST_MS);
        for (unsigned k = 0; k < 2000 && f.phase == FS_WAIT; ++k) adv(&f, 5);
        assert(f.phase == FS_BITE);
        ++count[f.tier];
        const fs_tier_t *t = fs_tier(f.tier);
        assert(f.weight_g >= t->min_g && f.weight_g <= t->max_g);
    }
    for (unsigned i = 0; i < FS_TIERS; ++i) {
        const int expect = (int)fs_tier(i)->weight * 40; /* of 4000 */
        assert(absi((int)count[i] - expect) <= 160);
    }
    /* the best catch is kept */
    fs_init(&f, 78, g_now);
    hook(&f, 0);
    f.weight_g = 300;
    unsigned mg = 100000, pr = 0;
    bot(&f, &mg, &pr);
    assert(f.best_g == 300 && f.best_tier == 0);
    adv(&f, FS_LAND_MS + FS_RESULT_MIN_MS);
    hook(&f, 2);
    f.weight_g = 3000;
    bot(&f, &mg, &pr);
    assert(f.best_g == 3000 && f.best_tier == 2 && f.caught == 2);
    adv(&f, FS_LAND_MS + FS_RESULT_MIN_MS);
    hook(&f, 1);
    f.weight_g = 900;
    bot(&f, &mg, &pr);
    assert(f.best_g == 3000 && f.caught == 3);
}

int main(void)
{
    test_tiers();
    test_cast_and_bite();
    test_perfect_fights();
    test_failures();
    test_windows();
    test_hold();
    test_shake_scales_with_fish();
    test_stall_and_bounds();
    test_stats();
    puts("fishing tests passed");
    return 0;
}
