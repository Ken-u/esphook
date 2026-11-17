/* Host tests for the fortune slips (apps/fortune/firmware/fortune_core.c). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fortune_core.h"

static unsigned glyphs(const char *s)
{
    unsigned n = 0;
    for (; *s; ++s) n += ((unsigned char)*s & 0xC0U) != 0x80U;
    return n;
}
static void run(ft_t *f, uint32_t ms)
{
    while (ms) {
        const uint32_t d = ms > 20U ? 20U : ms;
        ft_advance(f, d);
        ms -= d;
    }
}

static void test_tables(void)
{
    for (unsigned c = 0; c < FT_CATEGORIES; ++c) {
        assert(glyphs(ft_category_name(c)) == 2U);
        for (unsigned l = 0; l < FT_LEVELS; ++l)
            for (unsigned t = 0; t < FT_TEXTS; ++t) {
                const char *text = ft_text(c, l, t);
                assert(glyphs(text) >= 5U && glyphs(text) <= 10U); /* two lines of seven in the 12 px font, never more */
                for (unsigned o = 0; o < FT_TEXTS; ++o)
                    if (o != t) assert(strcmp(text, ft_text(c, l, o)) != 0);
            }
    }
    for (unsigned l = 0; l < FT_LEVELS; ++l) assert(glyphs(ft_level_name(l)) == 2U);
    assert(!strcmp(ft_level_name(FT_BIG), "大吉") && !strcmp(ft_level_name(FT_MID), "中吉") && !strcmp(ft_level_name(FT_SMALL), "小吉"));
    assert(!strcmp(ft_category_name(FT_CAREER), "事业") && !strcmp(ft_category_name(FT_PEOPLE), "人际") &&
           !strcmp(ft_category_name(FT_FAMILY), "家庭") && !strcmp(ft_category_name(FT_STUDY), "学业") &&
           !strcmp(ft_category_name(FT_LOVE), "爱情"));
    for (unsigned k = 0; k < FT_COLOURS; ++k) assert(glyphs(ft_colour_name(k)) == 1U && ft_colour_rgb(k) <= 0xFFFFFFU);
    /* No two slips of the same area say the same thing, whatever their grade. */
    for (unsigned c = 0; c < FT_CATEGORIES; ++c)
        for (unsigned a = 0; a < FT_LEVELS * FT_TEXTS; ++a)
            for (unsigned b = a + 1; b < FT_LEVELS * FT_TEXTS; ++b)
                assert(strcmp(ft_text(c, a / FT_TEXTS, a % FT_TEXTS), ft_text(c, b / FT_TEXTS, b % FT_TEXTS)) != 0);
}

static void test_distribution(void)
{
    uint32_t rng = 12345U;
    unsigned cell[FT_CATEGORIES][FT_LEVELS] = {{0}}, level[FT_LEVELS] = {0}, number[100] = {0}, colour[FT_COLOURS] = {0};
    const unsigned n = 60000U;
    for (unsigned i = 0; i < n; ++i) {
        const ft_sign_t s = ft_pick(&rng);
        assert(s.category < FT_CATEGORIES && s.level < FT_LEVELS && s.text < FT_TEXTS && s.number >= 1U && s.number <= 99U && s.colour < FT_COLOURS);
        ++cell[s.category][s.level]; ++level[s.level]; ++number[s.number]; ++colour[s.colour];
    }
    /* Every kind of slip, every number and every colour comes up; the grades follow 25 / 40 / 35 percent. */
    for (unsigned c = 0; c < FT_CATEGORIES; ++c)
        for (unsigned l = 0; l < FT_LEVELS; ++l) assert(cell[c][l] > n / 100U);
    for (unsigned v = 1; v <= 99U; ++v) assert(number[v] > n / 300U);
    for (unsigned k = 0; k < FT_COLOURS; ++k) assert(colour[k] > n / 12U);
    assert(level[FT_BIG] > n * 22U / 100U && level[FT_BIG] < n * 28U / 100U);
    assert(level[FT_MID] > n * 37U / 100U && level[FT_MID] < n * 43U / 100U);
    assert(level[FT_SMALL] > n * 32U / 100U && level[FT_SMALL] < n * 38U / 100U);
    /* Areas are even, and consecutive draws are not stuck on one slip. */
    for (unsigned c = 0; c < FT_CATEGORIES; ++c) {
        unsigned total = cell[c][0] + cell[c][1] + cell[c][2];
        assert(total > n * 18U / 100U && total < n * 22U / 100U);
    }
    rng = 1U;
    ft_sign_t a = ft_pick(&rng), b = ft_pick(&rng);
    assert(memcmp(&a, &b, sizeof a) != 0);
    uint32_t again = 1U;
    ft_sign_t c = ft_pick(&again);
    assert(memcmp(&a, &c, sizeof a) == 0); /* the generator is deterministic */
}

static void test_ceremony(void)
{
    ft_t f;
    ft_init(&f, 7U);
    assert(f.phase == FT_IDLE && f.draws == 0);
    run(&f, 5000);
    assert(f.phase == FT_IDLE); /* nothing happens by itself */
    ft_release(&f);
    assert(f.phase == FT_IDLE); /* a stray release is nothing */
    /* A tap shakes for the minimum, then the stick pops, the slip unfolds, and it stays. */
    ft_press(&f);
    assert(f.phase == FT_SHAKE && !ft_slip_ready(&f));
    const ft_sign_t first = f.sign;
    ft_release(&f);
    run(&f, FT_SHAKE_MIN_MS - 20U);
    assert(f.phase == FT_SHAKE);
    run(&f, 20U);
    assert(f.phase == FT_POP && !ft_slip_ready(&f));
    run(&f, FT_POP_MS);
    assert(f.phase == FT_UNFOLD && ft_slip_ready(&f) && f.draws == 0);
    run(&f, FT_UNFOLD_MS);
    assert(f.phase == FT_SLIP && ft_slip_ready(&f) && f.draws == 1 && f.counts[first.level] == 1);
    assert(!memcmp(&first, &f.sign, sizeof first)); /* the slip does not change while it is shown */
    run(&f, 9000);
    assert(f.phase == FT_SLIP);
    /* A press while a draw is under way is ignored (it cannot be redrawn midway) ... */
    ft_press(&f);
    ft_release(&f);
    run(&f, 100U);
    const ft_sign_t before = f.sign;
    ft_press(&f);
    assert(!memcmp(&before, &f.sign, sizeof before)); /* still shaking from the press above: unchanged */
    /* ... but a held key shakes for as long as it is held, up to the maximum. */
    ft_init(&f, 9U);
    ft_press(&f);
    run(&f, FT_SHAKE_MAX_MS - 20U);
    assert(f.phase == FT_SHAKE);
    run(&f, 20U);
    assert(f.phase == FT_POP);
    /* Released late: the shake ends at the release, not at the minimum. */
    ft_init(&f, 9U);
    ft_press(&f);
    run(&f, 2500U);
    assert(f.phase == FT_SHAKE);
    ft_release(&f);
    run(&f, 20U);
    assert(f.phase == FT_POP);
    run(&f, FT_POP_MS + FT_UNFOLD_MS);
    assert(f.phase == FT_SLIP && f.draws == 1);
    /* Another draw from the slip screen: a new slip, the old one counted. */
    ft_press(&f);
    assert(f.phase == FT_SHAKE);
    ft_release(&f);
    run(&f, FT_SHAKE_MIN_MS + FT_POP_MS + FT_UNFOLD_MS + 40U);
    assert(f.phase == FT_SLIP && f.draws == 2 && f.counts[0] + f.counts[1] + f.counts[2] == 2);
    /* A long gap between calls is a pause, not a backlog. */
    ft_init(&f, 9U);
    ft_press(&f);
    ft_release(&f);
    ft_advance(&f, 60000U);
    assert(f.phase == FT_SHAKE);
}

static void test_distinct_sessions(void)
{
    /* Two devices (or two boots) with different clocks draw different first slips most of the time. */
    unsigned same = 0;
    for (uint32_t seed = 1; seed <= 200U; ++seed) {
        ft_t a, b;
        ft_init(&a, seed);
        ft_init(&b, seed + 1U);
        ft_press(&a);
        ft_press(&b);
        same += !memcmp(&a.sign, &b.sign, sizeof a.sign);
    }
    assert(same < 10U);
}

int main(void)
{
    test_tables();
    test_distribution();
    test_ceremony();
    test_distinct_sessions();
    puts("fortune: tables, grades, ceremony timing, held shake and fresh draws passed");
    return 0;
}
