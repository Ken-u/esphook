#include "dino_game.h"
#include <math.h>
#include <string.h>

#define STEP_S 0.001f
#define STALL_MS 2000U /* a longer gap between ticks is treated as a pause, not simulated */

/* Counter-based generator: any seed gives a distinct, well mixed stream. */
static uint32_t draw(uint32_t *state)
{
    uint32_t z = (*state += 0x9e3779b9U);
    z ^= z >> 16; z *= 0x85ebca6bU;
    z ^= z >> 13; z *= 0xc2b2ae35U;
    return z ^ (z >> 16);
}
static float unit(uint32_t *state)
{
    return (float)(draw(state) % 10001U) / 10000.0f;
}
void dino_kind_size(dino_kind_t kind, float *width, float *height, float *bottom)
{
    static const float table[DINO_KINDS][3] = {
        [DINO_CACTUS_SMALL] = {8, 12, 0}, [DINO_CACTUS_LARGE] = {10, 17, 0}, [DINO_CACTUS_PAIR] = {18, 12, 0},
        [DINO_BIRD_LOW] = {14, 9, 4}, [DINO_BIRD_HIGH] = {14, 9, 22}};
    if (kind >= DINO_KINDS) kind = DINO_CACTUS_SMALL;
    *width = table[kind][0]; *height = table[kind][1]; *bottom = table[kind][2];
}
static float speed_at(uint64_t run_ms)
{
    float speed = DINO_SPEED_START + DINO_SPEED_GAIN * ((float)run_ms / 1000.0f);
    return speed < DINO_SPEED_MAX ? speed : DINO_SPEED_MAX;
}
/* Next gap is measured in time at the current speed, so it always leaves room for a full jump. */
static void schedule_next(dino_game_t *g)
{
    float seconds = DINO_GAP_MIN_S + (DINO_GAP_MAX_S - DINO_GAP_MIN_S) * unit(&g->random);
    g->travel_left = g->speed * seconds;
}
static void begin_run(dino_game_t *g, uint64_t now)
{
    g->phase = DINO_PLAY;
    g->clock_ms = now;
    g->run_ms = 0;
    g->distance = 0;
    g->speed = DINO_SPEED_START;
    g->y = g->vy = 0;
    g->held = g->cut = g->record = false;
    g->count = 0;
    g->travel_left = 130.0f; /* first obstacle shows up after a short run-up */
    g->land_ms = g->jump_ms = now;
}
void dino_start(dino_game_t *g, uint64_t now, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->phase = DINO_READY;
    g->clock_ms = now;
    g->random = seed;
    g->speed = DINO_SPEED_START;
}
static void spawn(dino_game_t *g)
{
    dino_kind_t kind;
    bool birds = g->distance >= DINO_BIRD_AFTER_PX;
    unsigned roll = draw(&g->random) % 100U;
    if (birds && roll < 25U) kind = (draw(&g->random) & 1U) ? DINO_BIRD_HIGH : DINO_BIRD_LOW;
    else {
        unsigned cactus = draw(&g->random) % 100U;
        kind = cactus < 40U ? DINO_CACTUS_SMALL : cactus < 75U ? DINO_CACTUS_LARGE : DINO_CACTUS_PAIR;
    }
    if (g->count < DINO_MAX_OBSTACLES) g->obstacles[g->count++] = (dino_obstacle_t){kind, DINO_FIELD_W + 4.0f};
    schedule_next(g);
}
static bool hit(const dino_game_t *g)
{
    const float left = DINO_X + DINO_BOX_LEFT, right = DINO_X + DINO_BOX_RIGHT;
    for (unsigned i = 0; i < g->count; ++i) {
        float width, height, bottom;
        dino_kind_size(g->obstacles[i].kind, &width, &height, &bottom);
        float o_left = g->obstacles[i].x + 1.5f, o_right = g->obstacles[i].x + width - 1.5f; /* sprites have soft edges */
        float o_bottom = bottom > 0.0f ? bottom + 1.0f : 0.0f, o_top = bottom + height - 2.0f;
        if (right > o_left && left < o_right && g->y + DINO_BOX_H > o_bottom && g->y < o_top) return true;
    }
    return false;
}
static unsigned score_of(const dino_game_t *g)
{
    return (unsigned)(g->distance / 10.0);
}
static void crash(dino_game_t *g)
{
    g->phase = DINO_OVER;
    g->over_ms = g->clock_ms;
    g->held = g->cut = false;
    g->record = g->best > 0 && score_of(g) > g->best; /* the first run has nothing to beat */
    if (score_of(g) > g->best) g->best = score_of(g);
}
static void step(dino_game_t *g)
{
    ++g->run_ms;
    g->speed = speed_at(g->run_ms);
    float advance = g->speed * STEP_S;
    g->distance += advance;
    unsigned kept = 0;
    for (unsigned i = 0; i < g->count; ++i) {
        float width, height, bottom;
        g->obstacles[i].x -= advance;
        dino_kind_size(g->obstacles[i].kind, &width, &height, &bottom);
        if (g->obstacles[i].x + width > 0.0f) g->obstacles[kept++] = g->obstacles[i];
    }
    g->count = kept;
    g->travel_left -= advance;
    if (g->travel_left <= 0.0f) spawn(g);
    if (g->y > 0.0f || g->vy > 0.0f) {
        g->vy -= DINO_GRAVITY * STEP_S;
        /* Releasing early shortens the jump once it has cleared a minimum height. */
        if (g->cut && g->y >= DINO_MIN_HOP_Y && g->vy > DINO_DROP_V) g->vy = DINO_DROP_V;
        g->y += g->vy * STEP_S;
        if (g->y <= 0.0f) { g->y = g->vy = 0.0f; g->cut = false; g->land_ms = g->clock_ms; }
    }
    if (hit(g)) crash(g);
}
void dino_tick(dino_game_t *g, uint64_t now)
{
    if (now <= g->clock_ms) return;
    if (now - g->clock_ms > STALL_MS) g->clock_ms = now - 1;
    while (g->clock_ms < now) {
        ++g->clock_ms;
        if (g->phase == DINO_PLAY) step(g);
    }
}
bool dino_press(dino_game_t *g, uint64_t now)
{
    dino_tick(g, now);
    if (g->phase == DINO_OVER) {
        if (g->clock_ms - g->over_ms < DINO_RESTART_DELAY_MS) return false;
        begin_run(g, g->clock_ms);
    } else if (g->phase == DINO_READY) {
        begin_run(g, g->clock_ms);
    } else if (g->y > 0.0f || g->vy > 0.0f) {
        return false;
    }
    g->vy = DINO_JUMP_V;
    g->y = 0.001f; /* airborne from this step on */
    g->jump_ms = g->clock_ms;
    g->held = true;
    g->cut = false;
    return true;
}
void dino_release(dino_game_t *g, uint64_t now)
{
    dino_tick(g, now);
    g->held = false;
    if (g->phase == DINO_PLAY && g->vy > 0.0f) g->cut = true;
}
void dino_snapshot(const dino_game_t *g, dino_view_t *out)
{
    unsigned score = score_of(g);
    *out = (dino_view_t){.phase = g->phase, .score = score, .best = g->best, .speed_pps = (unsigned)lroundf(g->speed),
        .frame = (unsigned)(g->distance / 10.0), .scroll = (unsigned)g->distance, .count = g->count, .y = g->y,
        .airborne = g->y > 0.0f || g->vy > 0.0f, .new_best = g->phase == DINO_OVER && g->record,
        .can_restart = g->phase == DINO_OVER && g->clock_ms - g->over_ms >= DINO_RESTART_DELAY_MS,
        .since_land_ms = (unsigned)(g->clock_ms - g->land_ms > 10000U ? 10000U : g->clock_ms - g->land_ms),
        .since_jump_ms = (unsigned)(g->clock_ms - g->jump_ms > 10000U ? 10000U : g->clock_ms - g->jump_ms)};
    memcpy(out->obstacles, g->obstacles, sizeof out->obstacles);
}
