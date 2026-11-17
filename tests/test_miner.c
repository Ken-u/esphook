/* Host tests for the gold miner (apps/miner/firmware/miner_core.c): level layout, the claw, loads and a bot. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miner_core.h"

static void run(mn_t *g, uint32_t ms)
{
    while (ms) { const uint32_t d = ms > 20U ? 20U : ms; mn_advance(g, d); ms -= d; }
}
static void play(mn_t *g, uint32_t seed)
{
    mn_init(g, seed);
    mn_press(g);
    assert(g->phase == MN_INTRO);
    run(g, MN_INTRO_MS + 20U);
    assert(g->phase == MN_PLAY);
}
/* A hand-made level: items by kind and position, the claw pointing straight down. */
static void scene(mn_t *g, unsigned n, const mn_item_t *items)
{
    memset(g->items, 0, sizeof g->items);
    for (unsigned i = 0; i < n; ++i) g->items[i] = items[i];
    g->count = (uint8_t)n;
    g->swing_ms = 0; /* the middle of the swing: straight down */
}
static unsigned alive(const mn_t *g) { unsigned n = 0; for (unsigned i = 0; i < g->count; ++i) n += g->items[i].alive; return n; }

static void test_tables_and_levels(void)
{
    for (unsigned k = 0; k < MN_KINDS; ++k) {
        const mn_info_t *i = mn_info(k);
        assert(i->radius >= 3 && i->radius <= 9 && i->pull_pps >= 20 && i->pull_pps <= MN_EMPTY_PPS);
    }
    assert(mn_info(MN_DIAMOND)->value > mn_info(MN_GOLD_L)->value && mn_info(MN_GOLD_L)->value > mn_info(MN_GOLD_M)->value);
    assert(mn_info(MN_GOLD_L)->pull_pps < mn_info(MN_GOLD_S)->pull_pps && mn_info(MN_STONE_L)->pull_pps < mn_info(MN_STONE_S)->pull_pps);
    assert(mn_info(MN_DIAMOND)->pull_pps > mn_info(MN_GOLD_S)->pull_pps); /* small and light comes home first */
    assert(mn_info(99)->radius == mn_info(0)->radius);
    assert(mn_level_time_ms(1) == 60000U && mn_level_time_ms(2) == 57000U && mn_level_time_ms(7) == 42000U && mn_level_time_ms(20) == 40000U);
    unsigned kinds[MN_KINDS] = {0};
    for (unsigned seed = 1; seed <= 300; ++seed) {
        mn_t g;
        mn_init(&g, seed);
        mn_press(&g);
        for (unsigned level = 1; level <= 8; ++level) {
            if (level > 1U) { g.phase = MN_CLEAR; g.phase_ms = 700; mn_press(&g); } /* the next level is dug when a clear is dismissed */
            assert(g.phase == MN_INTRO && g.level == level && g.count >= 8U && g.count <= MN_MAX_ITEMS);
            unsigned total = 0;
            for (unsigned i = 0; i < g.count; ++i) {
                const mn_item_t *it = &g.items[i];
                const mn_info_t *info = mn_info(it->kind);
                assert(it->alive && it->x - info->radius >= 2 && it->x + info->radius <= MN_W - 3 && it->y + info->radius <= MN_H - 2);
                assert(it->y - info->radius >= MN_ORIGIN_Y + 10); /* clear of the miner */
                for (unsigned j = 0; j < i; ++j) {
                    const int dx = it->x - g.items[j].x, dy = it->y - g.items[j].y, need = info->radius + mn_info(g.items[j].kind)->radius + 3;
                    assert(dx * dx + dy * dy >= need * need);
                }
                total += it->kind == MN_BAG ? 150U : (unsigned)info->value;
                ++kinds[it->kind];
            }
            assert(total == g.level_total && g.goal >= 60U && g.goal % 10U == 0U);
            assert(g.goal <= total * 85U / 100U || g.goal == 60U); /* a goal is never the whole level */
            assert(g.time_ms == mn_level_time_ms(level));
        }
    }
    for (unsigned k = 0; k < MN_KINDS; ++k) assert(kinds[k] > 100U); /* every kind of thing turns up */
    mn_t a, b;
    mn_init(&a, 77); mn_init(&b, 77);
    mn_press(&a); mn_press(&b);
    assert(!memcmp(a.items, b.items, sizeof a.items) && a.goal == b.goal); /* the same seed digs the same mine */
    mn_init(&b, 78); mn_press(&b);
    assert(memcmp(a.items, b.items, sizeof a.items));
}

static void test_swing(void)
{
    mn_t g;
    mn_init(&g, 5);
    int lo = 999, hi = -999, last = 0;
    unsigned crossings = 0;
    for (unsigned ms = 0; ms < 2U * MN_SWING_MS; ms += 10U) {
        g.swing_ms = ms;
        int dx, dy;
        mn_dir(&g, &dx, &dy);
        assert(dy > 0 && dx * dx + dy * dy > 255 * 255 - 1200 && dx * dx + dy * dy < 257 * 257 + 1200); /* a unit vector pointing down */
        if (dx < lo) lo = dx;
        if (dx > hi) hi = dx;
        crossings += (last < 0 && dx >= 0) || (last > 0 && dx <= 0);
        last = dx;
        int t = 0, u = 0;
        g.len16 = 80 * 16;
        mn_tip(&g, &t, &u);
        assert(u >= MN_ORIGIN_Y + 25 && t >= 0 && t < MN_W && u <= MN_ORIGIN_Y + 80);
    }
    assert(lo <= -230 && hi >= 230 && lo >= -241 && hi <= 241); /* about 75 degrees either way */
    assert(crossings >= 3U && crossings <= 5U); /* the middle is passed twice a swing */
    g.swing_ms = 0; g.len16 = 0;
    int x, y;
    mn_tip(&g, &x, &y);
    assert(x == MN_ORIGIN_X && y == MN_ORIGIN_Y);
}

static void test_flow_and_claw(void)
{
    mn_t g;
    mn_init(&g, 9);
    assert(g.phase == MN_TITLE && g.dynamite == 1 && g.level == 1);
    run(&g, 5000);
    assert(g.phase == MN_TITLE && g.time_ms == 0); /* nothing runs on the title */
    mn_press(&g);
    assert(g.phase == MN_INTRO && g.score == 0 && g.time_ms == 60000U && g.claw == MN_SWING);
    run(&g, 200);
    mn_press(&g);
    assert(g.phase == MN_INTRO); /* too early to skip: a press just before it appears is not meant for it */
    run(&g, 200);
    mn_press(&g);
    run(&g, 20);
    assert(g.phase == MN_PLAY);
    const uint32_t swing = g.swing_ms;
    run(&g, 1000);
    assert(g.swing_ms == swing + 1000U);
    assert(g.time_ms <= 59000U && g.time_ms >= 58900U);
    scene(&g, 0, NULL);
    mn_press(&g);
    assert(g.claw == MN_EXTEND && g.len16 == 6 * 16);
    const uint32_t frozen = g.swing_ms;
    mn_press(&g);
    assert(g.claw == MN_EXTEND); /* a second press while the claw is out does nothing */
    run(&g, 200);
    assert(g.swing_ms == frozen && g.len16 > 6 * 16 && g.len16 < 40 * 16); /* it flies straight and the swing waits */
    assert(g.len16 >= 28 * 16 && g.len16 <= 32 * 16); /* 6 px to start with, then 120 px a second */
    run(&g, 1400);
    assert(g.claw == MN_RETRACT || g.claw == MN_SWING); /* it hit the ground and turned back */
    run(&g, 2000);
    assert(g.claw == MN_SWING && g.len16 == 0 && g.score == 0 && g.caught == 0 && g.hooked == -1);
    mn_press(&g);
    assert(g.claw == MN_EXTEND); /* ready for the next shot */
    /* a swing to the side reaches the wall instead of the ground */
    mn_init(&g, 9); mn_press(&g); run(&g, MN_INTRO_MS + 20U);
    scene(&g, 0, NULL);
    g.swing_ms = MN_SWING_MS / 4U; /* the far right of the swing */
    int dx, dy;
    mn_dir(&g, &dx, &dy);
    assert(dx > 200);
    mn_press(&g);
    unsigned steps = 0;
    while (g.claw == MN_EXTEND && steps++ < 400) run(&g, 10);
    int x, y;
    mn_tip(&g, &x, &y);
    assert(g.claw == MN_RETRACT && (x >= MN_W - 6 || y >= MN_H - 6));
}

static void test_loads(void)
{
    mn_t g;
    /* gold small, straight down: +50 */
    play(&g, 3);
    scene(&g, 1, (mn_item_t[]){{80, 40, MN_GOLD_S, 1}});
    mn_press(&g);
    run(&g, 180);
    assert(g.claw == MN_RETRACT && g.hooked == 0 && g.hooked_kind == MN_GOLD_S);
    int x, y;
    mn_tip(&g, &x, &y);
    assert(g.items[0].x == x && g.items[0].y == y); /* the load rides on the claw */
    run(&g, 800);
    assert(g.claw == MN_SWING && g.score == 50U && g.caught == 1U && !g.items[0].alive && g.hooked == -1);
    assert(g.pop_value == 50 && g.pop_ms < 800U && g.pop_kind == 0);
    /* a big nugget and a boulder take longer than a diamond */
    unsigned times[3];
    const uint8_t kinds[3] = {MN_DIAMOND, MN_GOLD_L, MN_STONE_L};
    const unsigned values[3] = {500, 250, 20};
    for (unsigned k = 0; k < 3; ++k) {
        play(&g, 4);
        scene(&g, 1, (mn_item_t[]){{80, 56, kinds[k], 1}});
        mn_press(&g);
        unsigned ms = 0;
        while (g.claw != MN_RETRACT && ms < 3000) { run(&g, 10); ms += 10; }
        assert(g.claw == MN_RETRACT && g.hooked == 0);
        ms = 0;
        while (g.claw == MN_RETRACT && ms < 30000) { run(&g, 10); ms += 10; }
        times[k] = ms;
        assert(g.score == values[k]);
    }
    assert(times[0] < times[1] && times[1] < times[2] && times[2] > 1000U); /* weight matters */
    /* the claw passes by what it does not touch */
    play(&g, 3);
    scene(&g, 1, (mn_item_t[]){{110, 40, MN_GOLD_M, 1}});
    mn_press(&g);
    run(&g, 3500);
    assert(g.claw == MN_SWING && g.score == 0 && g.items[0].alive);
    /* the first thing on the way is what is caught */
    play(&g, 3);
    scene(&g, 2, (mn_item_t[]){{80, 52, MN_GOLD_L, 1}, {80, 34, MN_STONE_S, 1}});
    mn_press(&g);
    run(&g, 3000);
    assert(g.score == 12U && g.items[0].alive && !g.items[1].alive);
}

static void test_dynamite_and_barrels(void)
{
    mn_t g;
    /* a throw of dynamite at a boulder frees the claw, which comes back empty and fast */
    play(&g, 3);
    scene(&g, 2, (mn_item_t[]){{80, 56, MN_STONE_L, 1}, {20, 50, MN_GOLD_M, 1}});
    assert(g.dynamite == 1);
    mn_press(&g);
    run(&g, 600);
    assert(g.claw == MN_RETRACT && g.hooked == 0);
    run(&g, 300);
    const int32_t before = g.len16;
    mn_press(&g);
    assert(g.hooked == -1 && g.dynamite == 0 && !g.items[0].alive && g.boom_ms == 0 && g.score == 0);
    run(&g, 40);
    assert(before - g.len16 >= 80 && before - g.len16 <= 110); /* 150 px a second again, not the boulder's 24 */
    run(&g, 2000);
    assert(g.claw == MN_SWING && g.score == 0 && g.caught == 0);
    /* no dynamite, no blast: the press is ignored while hauling */
    scene(&g, 1, (mn_item_t[]){{80, 56, MN_STONE_L, 1}});
    mn_press(&g);
    run(&g, 900);
    assert(g.claw == MN_RETRACT && g.hooked == 0);
    mn_press(&g);
    assert(g.hooked == 0 && g.items[0].alive);
    /* a barrel takes its neighbours with it, and nothing is earned */
    play(&g, 3);
    scene(&g, 4, (mn_item_t[]){{80, 40, MN_TNT, 1}, {95, 42, MN_GOLD_M, 1}, {64, 44, MN_GOLD_S, 1}, {130, 55, MN_GOLD_M, 1}});
    mn_press(&g);
    run(&g, 160);
    assert(g.boom_ms < 100U && g.hooked == -1 && g.claw == MN_RETRACT);
    assert(!g.items[0].alive && !g.items[1].alive && !g.items[2].alive && g.items[3].alive && alive(&g) == 1U);
    run(&g, 1500);
    assert(g.claw == MN_SWING && g.score == 0 && g.caught == 0);
    /* a mystery bag pays something, or gives a stick of dynamite */
    unsigned money = 0, sticks = 0;
    for (unsigned seed = 1; seed <= 200; ++seed) {
        play(&g, seed);
        g.dynamite = 1;
        scene(&g, 1, (mn_item_t[]){{80, 40, MN_BAG, 1}});
        mn_press(&g);
        run(&g, 1500);
        assert(g.claw == MN_SWING && !g.items[0].alive);
        if (g.pop_kind == 1) { ++sticks; assert(g.dynamite == 2 && g.score == 0 && g.pop_value == 0); }
        else { ++money; assert(g.score > 0 && g.score <= 600U && g.dynamite == 1); }
    }
    assert(money > 120U && sticks > 15U);
    play(&g, 3);
    g.dynamite = MN_MAX_DYNAMITE;
    scene(&g, 1, (mn_item_t[]){{80, 40, MN_BAG, 1}});
    for (unsigned seed = 1; seed < 40; ++seed) { g.items[0] = (mn_item_t){80, 40, MN_BAG, 1}; g.rng = seed * 7919U; g.swing_ms = 0; mn_press(&g); run(&g, 1500); assert(g.dynamite <= MN_MAX_DYNAMITE); }
}

static bool aim(const mn_t *g, bool avoid_junk)
{
    int dx, dy, first = -1, first_at = 1 << 20;
    mn_dir(g, &dx, &dy);
    for (unsigned i = 0; i < g->count; ++i) {
        if (!g->items[i].alive) continue;
        const int vx = g->items[i].x - MN_ORIGIN_X, vy = g->items[i].y - MN_ORIGIN_Y, r = mn_info(g->items[i].kind)->radius;
        const int along = (vx * dx + vy * dy) / 256, perp = abs(vx * dy - vy * dx) / 256;
        if (along <= 0 || perp > r - 1) continue;
        if (along - r < first_at) { first_at = along - r; first = (int)i; }
    }
    if (first < 0) return false;
    const unsigned kind = g->items[first].kind;
    return !(avoid_junk && (kind == MN_TNT || kind == MN_STONE_L || kind == MN_STONE_S));
}
static unsigned bot_level(mn_t *g, bool avoid_junk)
{
    unsigned throws = 0, guard = 0;
    while (g->phase == MN_PLAY && guard++ < 20000U) {
        if (g->claw == MN_SWING && aim(g, avoid_junk)) {
            const uint32_t before = g->score;
            mn_press(g);
            ++throws;
            unsigned ms = 0;
            while (g->claw != MN_SWING && ms < 40000U) { mn_advance(g, 10); ms += 10; }
            assert(g->score >= before);
        } else mn_advance(g, 10);
    }
    assert(guard < 20000U);
    return throws;
}
static void test_levels_end(void)
{
    /* a bot that aims by the numbers earns a good deal in a minute, and the level ends on the clock */
    unsigned cleared = 0;
    for (unsigned seed = 1; seed <= 40; ++seed) {
        mn_t g;
        play(&g, seed);
        const uint32_t goal = g.goal;
        const unsigned total = g.level_total;
        const unsigned throws = bot_level(&g, true);
        assert(throws >= 3U && g.score > 0U && g.caught >= 3U);
        assert(g.time_ms == 0 && g.timeup && g.claw == MN_SWING);
        assert((g.score >= goal) == (g.phase == MN_CLEAR) && (g.phase == MN_CLEAR || g.phase == MN_OVER));
        assert(g.score <= total * 3U); /* bags aside, the level holds only so much */
        cleared += g.phase == MN_CLEAR;
    }
    assert(cleared >= 25U); /* a level-one goal is within reach of someone who aims */
    /* the clock ends the level even when the claw is out: the haul on its way home still counts */
    mn_t g;
    play(&g, 3);
    scene(&g, 1, (mn_item_t[]){{80, 56, MN_GOLD_L, 1}});
    g.time_ms = 600;
    mn_press(&g);
    run(&g, 800);
    assert(g.timeup && g.phase == MN_PLAY && g.claw == MN_RETRACT);
    run(&g, 3000);
    assert(g.phase != MN_PLAY && g.score == 250U);
    /* clearing a level: the result waits, a press only counts after 600 ms, and the next level is harder */
    play(&g, 3);
    scene(&g, 1, (mn_item_t[]){{80, 40, MN_DIAMOND, 1}});
    g.goal = 100;
    mn_press(&g);
    run(&g, 800);
    g.time_ms = 20;
    run(&g, 60);
    assert(g.phase == MN_CLEAR && g.score == 500U && g.best == 500U);
    const uint32_t goal1 = g.goal;
    mn_press(&g);
    assert(g.phase == MN_CLEAR);
    run(&g, 700);
    mn_press(&g);
    assert(g.phase == MN_INTRO && g.level == 2 && g.score == 500U && g.level_base == 500U && g.dynamite == 2 && g.time_ms == 57000U && alive(&g) == g.count);
    run(&g, MN_INTRO_MS + 20U);
    assert(g.phase == MN_PLAY);
    (void)goal1;
    /* an unanswered result goes on by itself */
    g.time_ms = 20; g.claw = MN_SWING;
    g.level_base = g.score; g.goal = 60; /* this one is lost */
    run(&g, 60);
    assert(g.phase == MN_OVER && !g.new_best && g.best == 500U);
    mn_press(&g);
    assert(g.phase == MN_OVER); /* too early */
    run(&g, 900);
    mn_press(&g);
    assert(g.phase == MN_INTRO && g.level == 1 && g.score == 0 && g.dynamite == 1 && g.best == 500U && g.time_ms == 60000U);
    /* a new record is announced */
    run(&g, MN_INTRO_MS + 20U);
    scene(&g, 1, (mn_item_t[]){{80, 40, MN_GOLD_L, 1}});
    g.goal = 9000;
    mn_press(&g);
    run(&g, 3000);
    g.time_ms = 20;
    run(&g, 60);
    assert(g.phase == MN_OVER && g.score == 250U && !g.new_best);
    g.best = 100;
    g.phase = MN_PLAY; g.time_ms = 20; g.claw = MN_SWING;
    run(&g, 60);
    assert(g.phase == MN_OVER && g.new_best && g.best == 250U);
    /* a clear that arrives on its own after the result time */
    play(&g, 11);
    g.goal = 60; g.score = 100;
    g.time_ms = 20;
    run(&g, 60);
    assert(g.phase == MN_CLEAR);
    run(&g, MN_RESULT_MS + 40U);
    assert(g.phase == MN_INTRO && g.level == 2);
}

int main(void)
{
    test_tables_and_levels();
    test_swing();
    test_flow_and_claw();
    test_loads();
    test_dynamite_and_barrels();
    test_levels_end();
    puts("miner core: layouts, swing, claw and loads, dynamite, barrels, bags, goals and results passed");
    return 0;
}
