/* Host tests for the jump game (apps/jump/firmware/jump_core.c): boxes, charging, landing, combos and falls. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jump_core.h"

static void run(jp_t *g, uint32_t ms) { while (ms) { const uint32_t d = ms > 5U ? 5U : ms; jp_advance(g, d); ms -= d; } }
static void start(jp_t *g, uint32_t seed)
{
    jp_init(g, seed);
    assert(g->phase == JP_TITLE);
    jp_press(g);
    assert(g->phase == JP_READY && g->cur == 0 && jp_plat(g, 0)->hw == 11 && jp_plat(g, 1)->hw == 10 && jp_plat(g, 2)->hw >= 5);
}
/* A jump whose length is `extra16` more than needed to reach the middle of the next box. */
static void hop(jp_t *g, int extra16)
{
    assert(g->phase == JP_READY);
    const int want = jp_next_distance16(g) + extra16;
    const unsigned charge = jp_charge_for(want);
    jp_press(g);
    assert(g->phase == JP_CHARGE);
    for (unsigned i = 0; i < charge; ++i) jp_advance(g, 1);
    jp_release(g);
}
static void land(jp_t *g) { run(g, 1600); }
static int abs_i(int v) { return v < 0 ? -v : v; }

static void test_boxes(void)
{
    assert(jp_hw_max(0) == 11 && jp_hw_max(11) == 11 && jp_hw_max(12) == 10 && jp_hw_max(36) == 8 && jp_hw_max(48) == 7 && jp_hw_max(5000) == 7);
    unsigned turns = 0, straights = 0, colours[8] = {0};
    for (unsigned seed = 1; seed <= 200; ++seed) {
        jp_t g = {0};
        start(&g, seed);
        for (unsigned n = 0; n < 90; ++n) {
            const jp_plat_t *c = jp_plat(&g, 0), *a = jp_plat(&g, 1), *b = jp_plat(&g, 2);
            assert(c->hw >= 5 && a->hw >= 5 && b->hw >= 5 && b->hw <= jp_hw_max((unsigned)g.score));
            /* the boxes go on from each other along one axis, with a gap of at least three units */
            for (int w = -1; w <= 1; ++w) {
                const jp_plat_t *p = jp_plat(&g, w), *q = jp_plat(&g, w + 1);
                if (!p->hw) continue;
                const int da = q->a - p->a, db = q->b - p->b;
                assert((da > 0) != (db > 0) && da >= 0 && db >= 0);
                assert(da + db >= p->hw + q->hw + 3 && da + db <= p->hw + q->hw + 3 + 6 + 12 + 18 + 4);
                turns += db > 0; straights += da > 0;
            }
            /* no two boxes touch, however the path bends */
            for (int x = -2; x <= 2; ++x)
                for (int y = x + 1; y <= 2; ++y) {
                    const jp_plat_t *p = jp_plat(&g, x), *q = jp_plat(&g, y);
                    if (p->hw && q->hw) assert(abs_i(p->a - q->a) >= p->hw + q->hw || abs_i(p->b - q->b) >= p->hw + q->hw);
                }
            ++colours[jp_plat(&g, 1)->color];
            assert(jp_charge_for(jp_next_distance16(&g)) <= JP_MAX_CHARGE_MS - 100U); /* every box is within reach */
            hop(&g, 0);
            land(&g);
            assert(g.phase == JP_READY);
        }
    }
    assert(turns > 4000 && straights > 4000);
    for (unsigned k = 0; k < 8; ++k) assert(colours[k] > 500);
    jp_t a = {0}, b = {0};
    start(&a, 5); start(&b, 5);
    assert(!memcmp(a.plat, b.plat, sizeof a.plat));
    start(&b, 6);
    assert(memcmp(a.plat, b.plat, sizeof a.plat));
}

static void test_flow(void)
{
    jp_t g = {0};
    jp_init(&g, 3);
    run(&g, 3000);
    assert(g.phase == JP_TITLE);
    jp_release(&g);
    assert(g.phase == JP_TITLE);
    jp_press(&g);
    assert(g.phase == JP_READY && g.score == 0);
    jp_release(&g);
    assert(g.phase == JP_READY); /* a release without a press is nothing */
    run(&g, 500);
    jp_press(&g);
    assert(g.phase == JP_CHARGE && g.charge_ms == 0);
    jp_press(&g);
    assert(g.phase == JP_CHARGE);
    int a, b, lift;
    run(&g, 450);
    assert(g.charge_ms == 450 && jp_squash(&g) > 100 && jp_squash(&g) < 160);
    jp_pawn(&g, &a, &b, &lift);
    assert(a == 0 && b == 0 && lift == 0); /* it squats on the spot */
    while (g.phase == JP_CHARGE) jp_advance(&g, 5);
    assert(g.phase == JP_FLY && g.charge_ms == JP_MAX_CHARGE_MS && g.dist16 == JP_MIN_D16 + (int)(JP_MAX_CHARGE_MS * JP_RATE_D16_PER_S / 1000U)); /* held too long: it goes by itself */
    assert(g.result == JP_MISSED); /* ...and a hold that long overshoots the first box */
    /* a good jump: the arc, the landing and the squash */
    start(&g, 3);
    const int len = jp_next_distance16(&g);
    assert(len == 25 * 16);
    hop(&g, 0);
    assert(g.phase == JP_FLY && g.result == JP_PERFECT && g.dist16 >= len - 2 && g.dist16 <= len + 2);
    int peak = 0, prev_a = 0;
    for (unsigned t = 0; t < g.fly_ms; t += 5U) {
        jp_pawn(&g, &a, &b, &lift);
        assert(lift >= 0 && a >= prev_a && b == 0);
        prev_a = a;
        if (lift > peak) peak = lift;
        jp_advance(&g, 5);
    }
    assert(peak >= 12 * 16 && peak <= 20 * 16 && g.fly_ms >= 330 && g.fly_ms <= 700);
    assert(g.phase == JP_LAND && g.score == 2 && g.combo == 1 && g.popup == 2 && g.popup_ms == 0 && g.perfects == 1);
    jp_pawn(&g, &a, &b, &lift);
    assert(abs_i(a - 25 * 16) <= 2 && b == 0 && lift == 0);
    run(&g, 60);
    assert(jp_squash(&g) > 50); /* it squashes on landing */
    run(&g, JP_LAND_MS);
    assert(g.phase == JP_READY && g.cur == 1 && jp_squash(&g) == 0);
    assert(g.popup_ms > 0 && g.popup_ms != 0xFFFFFFFFU);
}

static void test_scoring(void)
{
    jp_t g = {0};
    start(&g, 21);
    const unsigned expect[] = {2, 4, 6, 8, 10, 12, 12, 12};
    unsigned total = 0;
    for (unsigned i = 0; i < 8; ++i) {
        hop(&g, 0);
        land(&g);
        total += expect[i];
        assert(g.score == total && g.combo == (i < 6 ? i + 1 : 6) && g.perfects == i + 1);
    }
    /* a landing that is not in the middle is a point, and the streak starts again */
    hop(&g, 40);
    assert(g.result == JP_GOOD);
    land(&g);
    assert(g.score == total + 1 && g.combo == 0 && g.phase == JP_READY);
    hop(&g, -40);
    assert(g.result == JP_GOOD);
    land(&g);
    assert(g.score == total + 2);
    hop(&g, 0);
    land(&g);
    assert(g.combo == 1 && g.score == total + 4); /* the next perfect starts at two again */
    /* the aim is at the middle from wherever the pawn stands: an off-centre landing costs nothing on the next */
    for (unsigned i = 0; i < 40; ++i) { hop(&g, (int)(i % 5) * 16 - 32); land(&g); assert(g.phase == JP_READY && g.result != JP_MISSED); }
    for (unsigned i = 0; i < 40; ++i) { hop(&g, 0); assert(g.result == JP_PERFECT); land(&g); }
}

static void test_edges_stay_and_fall(void)
{
    jp_t g = {0};
    /* the edge of the next box is the limit, to the hand's width of a sixteenth */
    start(&g, 4);
    const jp_plat_t *n = jp_plat(&g, 1);
    const int len = jp_next_distance16(&g);
    hop(&g, n->hw * 16 - 3);
    assert(g.result == JP_GOOD);
    land(&g);
    start(&g, 4);
    hop(&g, n->hw * 16 + 6);
    assert(g.result == JP_MISSED);
    start(&g, 4);
    hop(&g, -len - 100); /* a hop of three units from the box it stands on, whatever the aim */
    assert(g.result == JP_STAYED && g.dist16 == JP_MIN_D16);
    /* the gap in between: falling short is a miss, not a safe landing */
    start(&g, 4);
    const int gap_land = jp_plat(&g, 0)->hw * 16 + 8;
    jp_press(&g);
    const unsigned charge = jp_charge_for(gap_land + 16);
    for (unsigned i = 0; i < charge; ++i) jp_advance(&g, 1);
    jp_release(&g);
    assert(g.result == JP_MISSED);
    land(&g);
    assert(g.phase == JP_FALL || g.phase == JP_OVER);
    /* a tap is a hop on the spot or forward on the same box */
    start(&g, 8);
    jp_press(&g);
    jp_release(&g);
    assert(g.dist16 == JP_MIN_D16 && g.result == JP_STAYED);
    int a, b, lift;
    run(&g, g.fly_ms);
    assert(g.phase == JP_LAND && g.score == 0);
    jp_pawn(&g, &a, &b, &lift);
    assert(a == 48 && b == 0 && lift == 0);
    run(&g, JP_LAND_MS);
    assert(g.phase == JP_READY && g.cur == 0 && g.pa16 == 48);
    /* from there the next box is nearer, and a perfect hop is still perfect */
    assert(jp_next_distance16(&g) == 25 * 16 - 48);
    hop(&g, 0);
    assert(g.result == JP_PERFECT);
    land(&g);
    assert(g.cur == 1 && g.score == 2);
    /* a miss falls, ends the game, keeps the best and restarts only after a moment */
    start(&g, 9);
    hop(&g, 0); land(&g); hop(&g, 0); land(&g);
    const uint32_t score = g.score;
    assert(score == 6 && g.best == 0);
    hop(&g, 5 * 16);
    assert(g.result == JP_MISSED || g.result == JP_GOOD);
    while (g.result != JP_MISSED) { land(&g); hop(&g, 6 * 16); }
    jp_pawn(&g, &a, &b, &lift);
    run(&g, g.fly_ms);
    assert(g.phase == JP_FALL);
    int previous = 0;
    for (unsigned t = 0; t < JP_FALL_MS; t += 50U) {
        jp_pawn(&g, &a, &b, &lift);
        assert(lift <= previous);
        previous = lift;
        run(&g, 50);
    }
    assert(g.phase == JP_OVER && g.new_best && g.best == g.score && g.best >= score);
    jp_pawn(&g, &a, &b, &lift);
    assert(lift <= -30 * 16 && lift >= -60 * 16); /* it dropped out of sight */
    const uint32_t best = g.best;
    jp_press(&g);
    assert(g.phase == JP_OVER); /* too soon */
    run(&g, JP_RESTART_MS);
    jp_press(&g);
    assert(g.phase == JP_READY && g.score == 0 && g.best == best && g.cur == 0 && g.pa16 == 0 && g.combo == 0);
    /* a second, lower score does not take the record */
    hop(&g, 40 * 16);
    run(&g, 3000);
    assert(g.phase == JP_OVER && !g.new_best && g.best == best);
}

int main(void)
{
    test_boxes();
    test_flow();
    test_scoring();
    test_edges_stay_and_fall();
    puts("jump core: boxes, charge and arc, landing, combos, stays, falls and restarts passed");
    return 0;
}
