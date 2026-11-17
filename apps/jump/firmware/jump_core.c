#include "jump_core.h"
#include <string.h>
#include "esphook/app_math.h"
#include "esphook/app_rand.h"

int jp_hw_max(unsigned score)
{
    const int cut = (int)(score / 12U);
    return cut > 4 ? 7 : 11 - cut;
}
const jp_plat_t *jp_plat(const jp_t *g, int which)
{
    return &g->plat[((int)g->cur + which + 2 * (int)JP_PLATS) % (int)JP_PLATS];
}
static jp_plat_t *plat_at(jp_t *g, int which) { return (jp_plat_t *)jp_plat(g, which); }
int jp_next_distance16(const jp_t *g)
{
    const jp_plat_t *n = jp_plat(g, 1);
    const int va = n->a * 16 - (int)g->pa16, vb = n->b * 16 - (int)g->pb16;
    return (int)app_isqrt((uint32_t)(va * va + vb * vb));
}
unsigned jp_charge_for(int dist16)
{
    return dist16 <= JP_MIN_D16 ? 0U : (unsigned)(dist16 - JP_MIN_D16) * 1000U / JP_RATE_D16_PER_S;
}
static bool overlaps(const jp_plat_t *x, const jp_plat_t *y)
{
    const int reach = x->hw + y->hw + 2;
    return y->hw && app_abs(x->a - y->a) < reach && app_abs(x->b - y->b) < reach;
}
/* A new box after the last one: either diagonal, a gap of a few units between the edges, a size that shrinks with the
 * score, and never touching a box already there. */
static void make_next(jp_t *g)
{
    const jp_plat_t last = *plat_at(g, 1);
    const int hw_max = jp_hw_max((unsigned)g->score), hw_min = 5;
    const int hw = hw_min + (int)(app_rand_next(&g->rng) % (unsigned)(hw_max - hw_min + 1));
    const int gap_max = 6 + (int)(g->score > 30U ? 6U : g->score / 5U);
    int gap = 3 + (int)(app_rand_next(&g->rng) % (unsigned)(gap_max - 2));
    const bool turn = (app_rand_next(&g->rng) & 1U) != 0U;
    jp_plat_t fresh = last;
    fresh.hw = (uint8_t)hw;
    fresh.color = (uint8_t)((last.color + 1U + app_rand_next(&g->rng) % 6U) % 8U);
    for (unsigned tries = 0; tries < 6U; ++tries, gap += 3) {
        const int dist = last.hw + gap + hw;
        fresh.a = (int16_t)(last.a + (turn ? 0 : dist));
        fresh.b = (int16_t)(last.b + (turn ? dist : 0));
        if (!overlaps(&fresh, plat_at(g, 0)) && !overlaps(&fresh, plat_at(g, -1))) break;
    }
    *plat_at(g, 2) = fresh;
}
static void new_game(jp_t *g)
{
    const uint32_t best = g->best, rng = g->rng;
    memset(g, 0, sizeof *g);
    g->best = best; g->rng = rng;
    g->popup_ms = 0xFFFFFFFFU;
    g->plat[0] = (jp_plat_t){0, 0, 11, 3};
    g->plat[1] = (jp_plat_t){25, 0, 10, 5}; /* the first hop is a comfortable one: a big box, a gap of four units */
    make_next(g);
    g->phase = JP_READY;
}
void jp_init(jp_t *g, uint32_t seed)
{
    const uint32_t best = g->best;
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1U; g->best = best;
    g->popup_ms = 0xFFFFFFFFU;
    g->plat[0] = (jp_plat_t){0, 0, 11, 3};
    g->phase = JP_TITLE;
}
void jp_press(jp_t *g)
{
    switch (g->phase) {
    case JP_TITLE: new_game(g); break;
    case JP_READY: g->phase = JP_CHARGE; g->charge_ms = 0; g->phase_ms = 0; break;
    case JP_OVER: if (g->phase_ms >= JP_RESTART_MS) new_game(g); break;
    default: break;
    }
}
static bool inside(const jp_plat_t *p, int32_t a16, int32_t b16, int slack16)
{
    return app_abs((int)(a16 - p->a * 16)) <= p->hw * 16 + slack16 && app_abs((int)(b16 - p->b * 16)) <= p->hw * 16 + slack16;
}
void jp_release(jp_t *g)
{
    if (g->phase != JP_CHARGE) return;
    const jp_plat_t *cur = jp_plat(g, 0), *next = jp_plat(g, 1);
    g->dist16 = (int16_t)(JP_MIN_D16 + (int)(g->charge_ms * JP_RATE_D16_PER_S / 1000U));
    const int va = next->a * 16 - (int)g->pa16, vb = next->b * 16 - (int)g->pb16, len = jp_next_distance16(g);
    g->ua = (int16_t)(len ? va * 256 / len : 256);
    g->ub = (int16_t)(len ? vb * 256 / len : 0);
    g->la16 = g->pa16 + g->dist16 * g->ua / 256;
    g->lb16 = g->pb16 + g->dist16 * g->ub / 256;
    if (inside(next, g->la16, g->lb16, 0)) {
        g->result = app_abs(g->dist16 - len) <= JP_PERFECT_D16 ? JP_PERFECT : JP_GOOD; /* the aim is on the line, so only the length can be off */
    } else if (inside(cur, g->la16, g->lb16, 0)) g->result = JP_STAYED;
    else g->result = JP_MISSED;
    g->fly_ms = (uint16_t)(330 + g->dist16 * 6 / 16);
    g->phase = JP_FLY; g->phase_ms = 0;
    ++g->jumps;
}
static void settle(jp_t *g)
{
    g->pa16 = g->la16; g->pb16 = g->lb16;
    if (g->result == JP_MISSED) { g->phase = JP_FALL; g->phase_ms = 0; return; }
    if (g->result == JP_STAYED) { g->combo = 0; g->phase = JP_LAND; g->phase_ms = 0; return; }
    int gain = 1;
    if (g->result == JP_PERFECT) {
        if (g->combo < 6U) ++g->combo;
        gain = 2 * (int)g->combo;
        ++g->perfects;
    } else g->combo = 0;
    g->score += (uint32_t)gain;
    g->popup = (int16_t)gain; g->popup_ms = 0;
    g->phase = JP_LAND; g->phase_ms = 0;
}
void jp_advance(jp_t *g, uint32_t dt_ms)
{
    ++g->frames;
    if (g->popup_ms != 0xFFFFFFFFU) g->popup_ms += dt_ms;
    g->phase_ms += dt_ms;
    switch (g->phase) {
    case JP_CHARGE:
        g->charge_ms += dt_ms;
        if (g->charge_ms >= JP_MAX_CHARGE_MS) { g->charge_ms = JP_MAX_CHARGE_MS; jp_release(g); }
        break;
    case JP_FLY:
        if (g->phase_ms >= g->fly_ms) settle(g);
        break;
    case JP_LAND:
        if (g->phase_ms >= JP_LAND_MS) {
            if (g->result == JP_GOOD || g->result == JP_PERFECT) { g->cur = (uint8_t)((g->cur + 1U) % JP_PLATS); make_next(g); }
            g->phase = JP_READY; g->phase_ms = 0;
        }
        break;
    case JP_FALL:
        if (g->phase_ms >= JP_FALL_MS) {
            g->phase = JP_OVER; g->phase_ms = 0;
            g->new_best = g->score > g->best;
            if (g->new_best) g->best = g->score;
        }
        break;
    default: break;
    }
}
unsigned jp_squash(const jp_t *g)
{
    if (g->phase == JP_CHARGE) { const unsigned c = g->charge_ms > 900U ? 900U : g->charge_ms; return c * 256U / 900U; }
    if (g->phase == JP_LAND) return g->phase_ms < 130U ? 128U - g->phase_ms * 128U / 130U : 0U; /* a squash on landing that springs back */
    return 0;
}
void jp_pawn(const jp_t *g, int *a16, int *b16, int *lift16)
{
    int a = (int)g->pa16, b = (int)g->pb16, lift = 0;
    if (g->phase == JP_FLY) {
        const int t = (int)(g->phase_ms * 256U / g->fly_ms);
        a += (int)(g->la16 - g->pa16) * t / 256;
        b += (int)(g->lb16 - g->pb16) * t / 256;
        lift = 16 * (8 + g->dist16 / 48) * 4 * t * (256 - t) / (256 * 256);
    } else if (g->phase == JP_FALL || (g->phase == JP_OVER && g->result == JP_MISSED)) {
        const int t = g->phase == JP_FALL && g->phase_ms < JP_FALL_MS ? (int)g->phase_ms : (int)JP_FALL_MS;
        lift = -(t * t * 16 / 20000);
    }
    *a16 = a; *b16 = b; *lift16 = lift;
}
