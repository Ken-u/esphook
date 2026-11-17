#include "miner_core.h"
#include <string.h>
#include "esphook/app_math.h"
#include "esphook/app_rand.h"

static const mn_info_t INFO[MN_KINDS] = {
    {4, 100, 50},  /* small gold */
    {6, 66, 100},  /* gold */
    {9, 34, 250},  /* a big nugget: worth it, and slow */
    {5, 56, 12},   /* small rock */
    {8, 24, 20},   /* boulder: the slowest thing there is */
    {3, 130, 500}, /* diamond: tiny and quick */
    {5, 84, 0},    /* mystery bag: rolled when it is home */
    {5, 70, 0},    /* dynamite barrel: blows up whatever is near it */
};
static const int16_t BAG_AVERAGE = 150;

const mn_info_t *mn_info(unsigned kind) { return &INFO[kind < MN_KINDS ? kind : 0U]; }

unsigned mn_level_time_ms(unsigned level)
{
    const unsigned cut = (level > 1U ? level - 1U : 0U) * 3000U;
    return cut > 20000U ? 40000U : 60000U - cut;
}
unsigned mn_seconds(const mn_t *g) { return (g->time_ms + 999U) / 1000U; }

void mn_dir(const mn_t *g, int *dx256, int *dy256)
{
    const unsigned phase = (g->swing_ms % MN_SWING_MS) * 256U / MN_SWING_MS;
    const int x = (int)(phase & 127U), v = x * (128 - x) / 16; /* a parabola for a sine: slow at the ends */
    const int s = (phase & 128U) ? -v : v;
    const int dx = s * 240 / 256;
    *dx256 = dx;
    *dy256 = (int)app_isqrt((uint32_t)(65536 - dx * dx));
}
void mn_tip(const mn_t *g, int *x, int *y)
{
    int dx, dy;
    mn_dir(g, &dx, &dy);
    *x = MN_ORIGIN_X + dx * g->len16 / 4096;
    *y = MN_ORIGIN_Y + dy * g->len16 / 4096;
}

static void build_level(mn_t *g)
{
    memset(g->items, 0, sizeof g->items);
    g->count = 0; g->level_total = 0;
    uint8_t weights[MN_KINDS] = {20, 18, 8, 14, 10, 6, 8, 6};
    weights[MN_STONE_S] = (uint8_t)(weights[MN_STONE_S] + g->level);
    weights[MN_STONE_L] = (uint8_t)(weights[MN_STONE_L] + g->level);
    weights[MN_TNT] = (uint8_t)(weights[MN_TNT] + g->level / 2U);
    unsigned total_weight = 0;
    for (unsigned k = 0; k < MN_KINDS; ++k) total_weight += weights[k];
    const unsigned want = 9U + (g->level > 4U ? 3U : g->level - 1U);
    for (unsigned attempt = 0; attempt < want * 40U && g->count < want; ++attempt) {
        unsigned pick = app_rand_next(&g->rng) % total_weight, kind = 0;
        while (pick >= weights[kind]) pick -= weights[kind++];
        const int r = INFO[kind].radius;
        const int x = r + 6 + (int)(app_rand_next(&g->rng) % (unsigned)(MN_W - 2 * r - 12));
        const int y = MN_ORIGIN_Y + r + 10 + (int)(app_rand_next(&g->rng) % (unsigned)(MN_H - MN_ORIGIN_Y - 2 * r - 12));
        bool free = true;
        for (unsigned i = 0; i < g->count && free; ++i) {
            const int dx = x - g->items[i].x, dy = y - g->items[i].y, need = r + INFO[g->items[i].kind].radius + 3;
            free = dx * dx + dy * dy >= need * need;
        }
        if (!free) continue;
        g->items[g->count++] = (mn_item_t){(int16_t)x, (int16_t)y, (uint8_t)kind, 1};
        g->level_total = (uint16_t)(g->level_total + (kind == MN_BAG ? BAG_AVERAGE : INFO[kind].value));
    }
    unsigned pct = 45U + 4U * (g->level - 1U);
    if (pct > 85U) pct = 85U;
    unsigned goal = g->level_total * pct / 100U / 10U * 10U;
    g->goal = goal < 60U ? 60U : goal;
}
static void start_level(mn_t *g)
{
    build_level(g);
    g->phase = MN_INTRO; g->phase_ms = 0;
    g->time_ms = mn_level_time_ms(g->level);
    g->claw = MN_SWING; g->len16 = 0; g->rem = 0; g->hooked = -1; g->timeup = 0;
    g->swing_ms = MN_SWING_MS / 8U;
    g->level_base = g->score;
    if (g->level > 1U && g->dynamite < MN_MAX_DYNAMITE) ++g->dynamite;
    g->pop_ms = g->boom_ms = 0xFFFFFFFFU;
}
static void new_game(mn_t *g)
{
    g->score = 0; g->level = 1; g->dynamite = 1; g->caught = 0; g->new_best = 0;
    start_level(g);
}
void mn_init(mn_t *g, uint32_t seed)
{
    const uint32_t best = g->best;
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1U; g->best = best;
    g->level = 1; g->hooked = -1; g->dynamite = 1;
    g->pop_ms = g->boom_ms = 0xFFFFFFFFU;
    g->phase = MN_TITLE;
}
static void finish_level(mn_t *g)
{
    g->phase_ms = 0;
    if (g->score - g->level_base >= g->goal) { g->phase = MN_CLEAR; if (g->score > g->best) g->best = g->score; return; }
    g->phase = MN_OVER;
    g->new_best = g->score > g->best;
    if (g->new_best) g->best = g->score;
}
void mn_press(mn_t *g)
{
    switch (g->phase) {
    case MN_TITLE: new_game(g); break;
    case MN_INTRO: if (g->phase_ms > 300U) g->phase_ms = MN_INTRO_MS; break;
    case MN_CLEAR:
        if (g->phase_ms > 600U) { if (g->level < MN_MAX_LEVEL) ++g->level; start_level(g); }
        break;
    case MN_OVER: if (g->phase_ms > 800U) new_game(g); break;
    default:
        if (g->claw == MN_SWING) { g->claw = MN_EXTEND; g->len16 = 6 * 16; g->rem = 0; }
        else if (g->claw == MN_RETRACT && g->hooked >= 0 && g->dynamite) {
            int x, y;
            mn_tip(g, &x, &y);
            --g->dynamite;
            g->items[g->hooked].alive = 0; /* the load is blown to pieces and the claw comes home empty */
            g->hooked = -1;
            g->boom_x = (int16_t)x; g->boom_y = (int16_t)y; g->boom_ms = 0;
        }
        break;
    }
}
static void blast(mn_t *g, int x, int y)
{
    for (unsigned i = 0; i < g->count; ++i) {
        const int dx = g->items[i].x - x, dy = g->items[i].y - y;
        if (g->items[i].alive && dx * dx + dy * dy <= MN_BLAST_RADIUS * MN_BLAST_RADIUS) g->items[i].alive = 0;
    }
    g->boom_x = (int16_t)x; g->boom_y = (int16_t)y; g->boom_ms = 0;
}
static void bring_home(mn_t *g)
{
    mn_item_t *it = &g->items[g->hooked];
    int value = INFO[it->kind].value;
    g->pop_kind = 0;
    if (it->kind == MN_BAG) {
        static const int16_t prizes[5] = {20, 80, 150, 300, 600};
        const unsigned roll = app_rand_next(&g->rng) % 10U;
        if (roll < 2U) { if (g->dynamite < MN_MAX_DYNAMITE) ++g->dynamite; value = 0; g->pop_kind = 1; }
        else value = prizes[roll % 5U];
    }
    it->alive = 0;
    g->score += (uint32_t)value;
    ++g->caught;
    g->pop_value = (int16_t)value; g->pop_x = MN_ORIGIN_X; g->pop_y = MN_ORIGIN_Y; g->pop_ms = 0;
    g->hooked = -1;
}
static void step_claw(mn_t *g, unsigned step)
{
    if (g->claw == MN_SWING) { g->swing_ms += step; return; }
    unsigned pps = MN_EXTEND_PPS;
    if (g->claw == MN_RETRACT) pps = g->hooked >= 0 ? INFO[g->items[g->hooked].kind].pull_pps : MN_EMPTY_PPS;
    const uint32_t acc = pps * 16U * step + g->rem;
    const int32_t move = (int32_t)(acc / 1000U);
    g->rem = (uint16_t)(acc % 1000U);
    if (g->claw == MN_EXTEND) {
        g->len16 += move;
        int x, y;
        mn_tip(g, &x, &y);
        for (unsigned i = 0; i < g->count; ++i) {
            if (!g->items[i].alive) continue;
            const int dx = x - g->items[i].x, dy = y - g->items[i].y, reach = INFO[g->items[i].kind].radius + 1;
            if (dx * dx + dy * dy > reach * reach) continue;
            g->claw = MN_RETRACT;
            if (g->items[i].kind == MN_TNT) blast(g, x, y); /* the barrel goes up with everything around it */
            else { g->hooked = (int8_t)i; g->hooked_kind = g->items[i].kind; }
            return;
        }
        if (x < 2 || x > MN_W - 3 || y > MN_H - 2) g->claw = MN_RETRACT; /* nothing there */
        return;
    }
    g->len16 -= move;
    if (g->len16 <= 8 * 16) {
        g->len16 = 0; g->claw = MN_SWING; g->rem = 0;
        if (g->hooked >= 0) bring_home(g);
        return;
    }
    if (g->hooked >= 0) {
        int x, y;
        mn_tip(g, &x, &y);
        g->items[g->hooked].x = (int16_t)x; g->items[g->hooked].y = (int16_t)y;
    }
}
static void step_once(mn_t *g, unsigned step)
{
    ++g->frames;
    if (g->pop_ms != 0xFFFFFFFFU) g->pop_ms += step;
    if (g->boom_ms != 0xFFFFFFFFU) g->boom_ms += step;
    g->phase_ms += step;
    switch (g->phase) {
    case MN_INTRO: if (g->phase_ms >= MN_INTRO_MS) { g->phase = MN_PLAY; g->phase_ms = 0; } break;
    case MN_CLEAR: if (g->phase_ms >= MN_RESULT_MS) { if (g->level < MN_MAX_LEVEL) ++g->level; start_level(g); } break;
    case MN_PLAY:
        if (g->time_ms > step) g->time_ms -= step;
        else { g->time_ms = 0; g->timeup = 1; }
        step_claw(g, step);
        if (g->timeup && g->claw == MN_SWING) finish_level(g); /* a haul that is still on its way home still counts */
        break;
    default: break;
    }
}
void mn_advance(mn_t *g, uint32_t dt_ms)
{
    while (dt_ms) {
        const unsigned step = dt_ms > 10U ? 10U : dt_ms;
        step_once(g, step);
        dt_ms -= step;
    }
}
