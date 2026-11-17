#include "ski_game.h"
#include <math.h>
#include <string.h>

#define STEP_S 0.001f
#define STALL_MS 2000U
#define SEG_MIN_ROWS 5U /* 70 px of descent: at top speed a turn is never needed twice within 0.7 s */
#define SEG_ROWS_SPAN 6U
/* The safe-line band (SKI_ROUTE_MIN..MAX) must fit two minimum segments, 2 * SEG_MIN_ROWS * SKI_SLOPE * SKI_ROW_PX
 * = 98 px of 100 px: then the band's edge never cuts a segment short, every pair of turns stays at least
 * SEG_MIN_ROWS rows apart and the menu's triple press never happens. tests/test_ski.c checks the resulting turn gaps,
 * so retuning the band, slope or row spacing is caught there. */
#define YETI_RISE_PPS 20.0f
#define YETI_FOLLOW_PPS 55.0f

static uint32_t draw(uint32_t *state)
{
    uint32_t z = (*state += 0x9e3779b9U);
    z ^= z >> 16; z *= 0x85ebca6bU;
    z ^= z >> 13; z *= 0xc2b2ae35U;
    return z ^ (z >> 16);
}
void ski_obstacle_half_size(ski_kind_t kind, float *half_w, float *half_h)
{
    *half_w = 3.0f;
    *half_h = kind == SKI_ROCK ? 2.5f : 4.0f;
}
static float speed_at(uint64_t run_ms)
{
    float speed = SKI_SPEED_START + SKI_SPEED_GAIN * ((float)run_ms / 1000.0f);
    return speed < SKI_SPEED_MAX ? speed : SKI_SPEED_MAX;
}
float ski_route_x(const ski_game_t *g, float wy)
{
    float row = wy / SKI_ROW_PX;
    if (row < 0.0f) row = 0.0f;
    uint32_t index = (uint32_t)row;
    float frac = row - (float)index;
    return g->route[index % SKI_ROUTE_ROWS] * (1.0f - frac) + g->route[(index + 1) % SKI_ROUTE_ROWS] * frac;
}
/* Moves the safe line one row downhill. It turns only at segment ends or at the edge of its band. */
static void advance_route(ski_game_t *g)
{
    float step = (float)g->route_dir * SKI_SLOPE * SKI_ROW_PX;
    bool turn = g->seg_rows_left <= 1;
    if (g->route_x + step > SKI_ROUTE_MAX || g->route_x + step < SKI_ROUTE_MIN) turn = true;
    if (turn) {
        /* Mostly zig-zag, sometimes carry on in the same direction for another stretch. */
        bool flip = draw(&g->random) % 100U < 80U || g->route_x + step > SKI_ROUTE_MAX || g->route_x + step < SKI_ROUTE_MIN;
        if (flip) g->route_dir = (int8_t)-g->route_dir;
        g->seg_rows_left = (uint8_t)(SEG_MIN_ROWS + draw(&g->random) % SEG_ROWS_SPAN);
        step = (float)g->route_dir * SKI_SLOPE * SKI_ROW_PX;
    } else {
        --g->seg_rows_left;
    }
    g->route_x += step;
}
static void make_row(ski_game_t *g)
{
    uint32_t row = g->gen_row++;
    float base = (float)row * SKI_ROW_PX;
    g->route[row % SKI_ROUTE_ROWS] = g->route_x;
    advance_route(g);
    g->route[(row + 1) % SKI_ROUTE_ROWS] = g->route_x;
    if (base < SKI_SAFE_START) return;
    unsigned wanted = 1U + draw(&g->random) % 3U + (g->distance > 2400.0 ? 1U : 0U);
    float placed[5];
    unsigned placed_count = 0;
    for (unsigned k = 0; k < wanted; ++k) {
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            float x = 8.0f + (float)(draw(&g->random) % 145U);
            float wy = base + (float)(draw(&g->random) % 7U) - 3.0f;
            if (fabsf(x - ski_route_x(g, wy)) < SKI_CORRIDOR_HALF) continue;
            bool crowded = false;
            for (unsigned i = 0; i < placed_count; ++i) crowded |= fabsf(placed[i] - x) < 14.0f;
            if (crowded || g->count >= SKI_MAX_OBSTACLES) continue;
            uint32_t kind_roll = draw(&g->random);
            placed[placed_count++] = x;
            g->obstacles[g->count++] = (ski_obstacle_t){.x = x, .wy = wy,
                .kind = kind_roll % 100U < 80U ? SKI_TREE : SKI_ROCK, .variant = (uint8_t)((kind_roll >> 8) & 1U)};
            break;
        }
    }
}
static void generate(ski_game_t *g)
{
    while ((double)g->gen_row * SKI_ROW_PX < g->distance + SKI_AHEAD) make_row(g);
}
static void reset_field(ski_game_t *g, uint64_t now)
{
    uint32_t best = g->best_m, random = g->random;
    memset(g, 0, sizeof *g);
    g->best_m = best;
    g->random = random;
    g->clock_ms = now;
    g->speed = SKI_SPEED_START;
    g->x = g->route_x = 80.0f;
    g->dir = g->route_dir = 1;
    g->lives = SKI_LIVES;
    g->seg_rows_left = 8;
    g->yeti_y = -40.0f;
    g->yeti_x = 80.0f;
    generate(g);
}
void ski_start(ski_game_t *g, uint64_t now, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->random = seed;
    reset_field(g, now);
    g->phase = SKI_READY;
}
static unsigned meters_of(const ski_game_t *g)
{
    return (unsigned)(g->distance / SKI_PX_PER_METER);
}
static float yeti_target(unsigned lives)
{
    /* Each crash brings the yeti closer; with one life left it is right behind the skier. */
    return lives >= 3 ? -40.0f : lives == 2 ? -2.0f : 5.0f;
}
static void move_towards(float *value, float target, float max_delta)
{
    float delta = target - *value;
    *value += delta > max_delta ? max_delta : delta < -max_delta ? -max_delta : delta;
}
static void crash(ski_game_t *g)
{
    --g->lives;
    g->last_crash_ms = g->clock_ms;
    g->invuln_until = g->clock_ms + SKI_INVULN_MS;
    g->stun_until = g->clock_ms + SKI_STUN_MS;
    if (g->lives == 0) {
        g->phase = SKI_OVER;
        g->over_ms = g->clock_ms;
        g->record = g->best_m > 0 && meters_of(g) > g->best_m; /* the first run has nothing to beat */
        if (meters_of(g) > g->best_m) g->best_m = meters_of(g);
    }
}
static void collide(ski_game_t *g)
{
    if (g->clock_ms < g->invuln_until) return;
    /* Soft-float target (no FPU): one double conversion per step, then cheap float math per obstacle, rejecting by
     * distance first. A float holds the ~20000 px of a long run to 0.002 px. */
    const float here = (float)g->distance;
    for (unsigned i = 0; i < g->count; ++i) {
        ski_obstacle_t *o = &g->obstacles[i];
        float dy = o->wy - here;
        if (o->hit || fabsf(dy) >= SKI_SKIER_HALF_H + 4.0f) continue; /* 4.0 is the tallest obstacle half height */
        float half_w, half_h;
        ski_obstacle_half_size((ski_kind_t)o->kind, &half_w, &half_h);
        if (fabsf(dy) >= SKI_SKIER_HALF_H + half_h || fabsf(o->x - g->x) >= SKI_SKIER_HALF_W + half_w) continue;
        o->hit = true;
        crash(g);
        return; /* One crash per step; invulnerability covers the rest. */
    }
}
static void step(ski_game_t *g)
{
    ++g->run_ms;
    float speed = speed_at(g->run_ms);
    if (g->clock_ms < g->stun_until) speed *= 0.45f;
    g->speed = speed;
    float advance = speed * STEP_S;
    g->distance += advance;
    g->x += (float)g->dir * SKI_SLOPE * advance;
    if (g->x < SKI_X_MIN) g->x = SKI_X_MIN;
    if (g->x > SKI_X_MAX) g->x = SKI_X_MAX;
    move_towards(&g->yeti_y, yeti_target(g->lives), YETI_RISE_PPS * STEP_S);
    move_towards(&g->yeti_x, g->x, YETI_FOLLOW_PPS * STEP_S);
    generate(g);
    unsigned kept = 0;
    const float behind = (float)(g->distance - SKI_BEHIND);
    for (unsigned i = 0; i < g->count; ++i)
        if (g->obstacles[i].wy >= behind) g->obstacles[kept++] = g->obstacles[i];
    g->count = kept;
    collide(g);
    if (g->phase == SKI_PLAY && g->lives < SKI_LIVES && g->clock_ms - g->last_crash_ms >= SKI_REGEN_MS) {
        ++g->lives;
        g->last_crash_ms = g->clock_ms;
    }
}
void ski_tick(ski_game_t *g, uint64_t now)
{
    if (now <= g->clock_ms) return;
    if (now - g->clock_ms > STALL_MS) g->clock_ms = now - 1;
    while (g->clock_ms < now) {
        ++g->clock_ms;
        if (g->phase == SKI_PLAY) step(g);
    }
}
bool ski_press(ski_game_t *g, uint64_t now)
{
    ski_tick(g, now);
    if (g->phase == SKI_PLAY) {
        g->dir = (int8_t)-g->dir;
        return true;
    }
    if (g->phase == SKI_OVER && g->clock_ms - g->over_ms < SKI_RESTART_DELAY_MS) return false;
    reset_field(g, g->clock_ms);
    g->phase = SKI_PLAY;
    return true;
}
void ski_snapshot(const ski_game_t *g, ski_view_t *out)
{
    unsigned meters = meters_of(g);
    *out = (ski_view_t){.phase = g->phase, .meters = meters, .best = g->best_m, .lives = g->lives,
        .speed_pps = (unsigned)lroundf(g->speed), .scroll = (unsigned)g->distance, .dir = g->dir, .skier_x = g->x, .yeti_x = g->yeti_x, .yeti_y = g->yeti_y,
        .invulnerable = g->clock_ms < g->invuln_until, .stunned = g->clock_ms < g->stun_until,
        .new_best = g->phase == SKI_OVER && g->record, .caught = g->phase == SKI_OVER,
        .can_restart = g->phase == SKI_OVER && g->clock_ms - g->over_ms >= SKI_RESTART_DELAY_MS};
    for (unsigned i = 0; i < g->count; ++i) {
        float y = SKI_SKIER_Y + (float)((double)g->obstacles[i].wy - g->distance);
        if (y < -12.0f || y > (float)SKI_VIEW_H + 12.0f) continue;
        out->sprites[out->count++] = (ski_sprite_t){.x = (int16_t)lroundf(g->obstacles[i].x), .y = (int16_t)lroundf(y),
            .kind = g->obstacles[i].kind, .variant = g->obstacles[i].variant};
    }
}
