#include "ski_game.h"
#include <math.h>
#include <string.h>

#define STEP_S 0.001f
#define STALL_MS 2000U /* a longer gap between ticks is treated as a pause, not simulated */
#define SLOPE_BASE 0.12f
#define HILL_A 7.0f
#define HILL_LEN 80.0f
#define BUMP_A 1.5f
#define BUMP_LEN 26.0f
#define RAD_TO_DEG 57.29578f
#define WALL_TOL 6.0f  /* a rim this far above the rider is a wall, less is climbed */
#define FALL_DEPTH 3.0f
#define CAM_FOLLOW 0.012f
#define CAM_ABOVE_MAX 34.0f /* the rider may rise this far above the camera's reference before it is pushed up */
#define CAM_BELOW_MAX 14.0f

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
static float lattice(uint32_t seed, int32_t i)
{
    uint32_t z = seed + (uint32_t)i * 0x9e3779b9U;
    z ^= z >> 16; z *= 0x85ebca6bU;
    z ^= z >> 13; z *= 0xc2b2ae35U;
    z ^= z >> 16;
    return (float)(z & 0xFFFFU) / 32767.5f - 1.0f;
}
/* Smooth value noise in [-1, 1] and its derivative. */
static void layer(uint32_t seed, double x, float length, float *value, float *slope)
{
    double t = x / (double)length, whole = floor(t);
    float f = (float)(t - whole);
    int32_t i = (int32_t)whole;
    float a = lattice(seed, i), b = lattice(seed, i + 1), s = f * f * (3.0f - 2.0f * f);
    *value = a + (b - a) * s;
    *slope = (b - a) * 6.0f * f * (1.0f - f) / length;
}
float ski_terrain(uint32_t seed, double x)
{
    float hill, bump, unused;
    layer(seed, x, HILL_LEN, &hill, &unused);
    layer(seed ^ 0x5bd1e995U, x, BUMP_LEN, &bump, &unused);
    return -(float)(SLOPE_BASE * x) + HILL_A * hill + BUMP_A * bump;
}
float ski_terrain_slope(uint32_t seed, double x)
{
    float unused, hill, bump;
    layer(seed, x, HILL_LEN, &unused, &hill);
    layer(seed ^ 0x5bd1e995U, x, BUMP_LEN, &unused, &bump);
    return -SLOPE_BASE + HILL_A * hill + BUMP_A * bump;
}
static float slope_deg(float slope) { return atanf(slope) * RAD_TO_DEG; }
static float wrap180(float degrees)
{
    degrees = fmodf(degrees + 180.0f, 360.0f);
    if (degrees < 0.0f) degrees += 360.0f;
    return degrees - 180.0f;
}
static float speed_at(uint64_t run_ms)
{
    float speed = SKI_SPEED_START + SKI_SPEED_GAIN * ((float)run_ms / 1000.0f);
    return speed < SKI_SPEED_MAX ? speed : SKI_SPEED_MAX;
}
static unsigned score_of(const ski_game_t *g)
{
    return (unsigned)(g->x / SKI_PX_PER_POINT) + g->bonus + g->gems * SKI_GEM_POINTS;
}
static void init_world(ski_game_t *g)
{
    g->seed = draw(&g->random);
    g->x = 0.0;
    g->speed = SKI_SPEED_START;
    g->y = g->ground_y = g->cam_y = ski_terrain(g->seed, 0.0);
    g->vy = g->rot = 0.0f;
    g->base = g->angle = slope_deg(ski_terrain_slope(g->seed, 0.0));
    g->grounded = true;
    g->held = g->record = g->popup_perfect = false;
    g->crash = SKI_NO_CRASH;
    g->air_ms = g->flips = g->gems = g->bonus = g->combo = g->popup_points = g->popup_flips = 0;
    g->run_ms = g->popup_ms = g->gem_ms = 0;
    g->count = 0;
    g->next_rel = SKI_FIRST_OBSTACLE_PX;
}
void ski_start(ski_game_t *g, uint64_t now, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->phase = SKI_READY;
    g->clock_ms = now;
    g->random = seed;
    init_world(g);
}
static void push(ski_game_t *g, ski_kind_t kind, unsigned w, unsigned h, float rel, float y)
{
    if (g->count < SKI_MAX_THINGS) g->things[g->count++] = (ski_thing_t){(uint8_t)kind, (uint8_t)w, (uint8_t)h, 0, rel, y};
}
/* Five gems along the path of a jump that clears the object around `centre` (a rel position). */
static void gem_arc(ski_game_t *g, float centre)
{
    static const float lift[5] = {10.0f, 18.0f, 21.0f, 18.0f, 10.0f};
    for (int i = 0; i < 5; ++i) {
        float rel = centre + (float)(i - 2) * 11.0f - 2.0f;
        push(g, SKI_GEM, 5, 5, rel, ski_terrain(g->seed, g->x + (double)rel) + lift[i]);
    }
}
/* Looks for a chasm site at or after *rel whose two rims are close in height and not too steep. */
static bool place_chasm(ski_game_t *g, float *rel_io, unsigned *width, float *rim)
{
    unsigned w = 22U + draw(&g->random) % 9U;
    float rel = *rel_io;
    for (int k = 0; k < 12; ++k, rel += 6.0f) {
        double left = g->x + (double)rel, right = left + (double)w;
        float hl = ski_terrain(g->seed, left), hr = ski_terrain(g->seed, right);
        if (fabsf(hr - hl) <= 8.0f && fabsf(ski_terrain_slope(g->seed, left)) < 0.35f &&
            fabsf(ski_terrain_slope(g->seed, right)) < 0.35f) {
            *rel_io = rel;
            *width = w;
            *rim = hl < hr ? hl : hr;
            push(g, SKI_CHASM, w, 0, rel, *rim);
            return true;
        }
    }
    return false;
}
static void spawn_group(ski_game_t *g)
{
    float rel = g->next_rel, extent;
    float gap = SKI_GAP_MIN_S + (SKI_GAP_MAX_S - SKI_GAP_MIN_S) * unit(&g->random);
    unsigned width = 0, roll = draw(&g->random) % 100U;
    bool chasm = false;
    float rim = 0.0f;
    if (g->x >= SKI_CHASM_AFTER_PX && roll < 38U) chasm = place_chasm(g, &rel, &width, &rim);
    if (chasm) {
        extent = (float)width;
        gap += 0.4f; /* landing room after a chasm */
    } else {
        unsigned w = 8U + 2U * (draw(&g->random) % 3U), h = 5U + 2U * (draw(&g->random) % 3U);
        push(g, SKI_ROCK, w, h, rel, ski_terrain(g->seed, g->x + (double)rel + (double)w / 2.0));
        extent = (float)w;
    }
    if (draw(&g->random) % 100U < 75U) gem_arc(g, rel + extent / 2.0f);
    g->next_rel = rel + extent + g->speed * gap;
}
static void crash(ski_game_t *g, ski_crash_t cause)
{
    g->phase = SKI_OVER;
    g->crash = cause;
    g->over_ms = g->clock_ms;
    g->held = false;
    g->record = g->best > 0 && score_of(g) > g->best; /* the first run has nothing to beat */
    if (score_of(g) > g->best) g->best = score_of(g);
}
static void land(ski_game_t *g, float ground)
{
    float theta = slope_deg(ski_terrain_slope(g->seed, g->x));
    float delta = wrap180(g->base + g->rot - theta);
    g->y = ground;
    g->vy = 0.0f;
    if (fabsf(delta) > SKI_LAND_TOL_DEG) { crash(g, SKI_CRASH_LANDING); return; }
    unsigned turns = (unsigned)(g->rot / 360.0f);
    if (!turns) {
        if (g->air_ms > 150U) g->combo = 0; /* micro hops do not break a streak, a plain jump does */
    } else {
        if (g->combo < SKI_COMBO_MAX) ++g->combo;
        unsigned points = SKI_FLIP_POINTS * turns * g->combo;
        bool perfect = fabsf(delta) <= SKI_PERFECT_DEG;
        if (perfect) points += points / 2U;
        g->bonus += points;
        g->flips += turns;
        g->popup_points = points;
        g->popup_flips = turns;
        g->popup_perfect = perfect;
        g->popup_ms = g->clock_ms;
    }
    g->grounded = true;
    g->rot = 0.0f;
    g->air_ms = 0;
}
static const ski_thing_t *chasm_under(const ski_game_t *g)
{
    for (unsigned i = 0; i < g->count; ++i) {
        const ski_thing_t *t = &g->things[i];
        if (t->kind == SKI_CHASM && t->rel <= 0.0f && t->rel + (float)t->w > 0.0f) return t;
    }
    return NULL;
}
static void leave_ground(ski_game_t *g, float vy)
{
    g->grounded = false;
    g->vy = vy;
    g->base = slope_deg(ski_terrain_slope(g->seed, g->x));
    g->rot = 0.0f;
    g->air_ms = 0;
}
static void step(ski_game_t *g)
{
    ++g->run_ms;
    g->speed = speed_at(g->run_ms);
    const float advance = g->speed * STEP_S;
    g->x += (double)advance;
    g->next_rel -= advance;
    unsigned kept = 0;
    for (unsigned i = 0; i < g->count; ++i) {
        g->things[i].rel -= advance;
        if (g->things[i].rel + (float)g->things[i].w > -(SKI_RIDER_X + 24.0f)) g->things[kept++] = g->things[i];
    }
    g->count = kept;
    if (g->next_rel < SKI_SPAWN_AHEAD) spawn_group(g);

    const ski_thing_t *chasm = chasm_under(g);
    float ground = 0.0f;
    if (!chasm) g->ground_y = ground = ski_terrain(g->seed, g->x);
    if (g->grounded) {
        if (chasm) leave_ground(g, ski_terrain_slope(g->seed, g->x) * g->speed); /* rode into a chasm */
        else g->y = ground;
    }
    if (!g->grounded) {
        ++g->air_ms;
        g->vy -= SKI_GRAVITY * STEP_S;
        g->y += g->vy * STEP_S;
        if (g->held) {
            g->rot += SKI_SPIN_DPS * STEP_S;
        } else { /* level off to the nearest whole turn: never undoes a completed flip */
            float target = 360.0f * floorf(g->rot / 360.0f + 0.5f), d = target - g->rot, s = SKI_LEVEL_DPS * STEP_S;
            g->rot = fabsf(d) <= s ? target : g->rot + (d > 0.0f ? s : -s);
        }
        if (chasm) {
            if (g->y < chasm->y - FALL_DEPTH) { crash(g, SKI_CRASH_FALL); return; }
        } else if (g->y <= ground) {
            if (ground - g->y > WALL_TOL) { crash(g, SKI_CRASH_FALL); return; } /* flew into the far rim */
            land(g, ground);
            if (g->phase != SKI_PLAY) return;
        }
    }
    for (unsigned i = 0; i < g->count; ++i) {
        ski_thing_t *t = &g->things[i];
        if (t->rel > 12.0f || t->rel < -40.0f) continue;
        if (t->kind == SKI_ROCK) {
            /* The rider is about 6 px wide where it touches; the rock's slopes are not solid at the very edge. */
            if (t->rel < 3.0f && t->rel + (float)t->w > -3.0f && g->y < t->y + (float)t->h - 1.5f) {
                crash(g, SKI_CRASH_ROCK);
                return;
            }
        } else if (t->kind == SKI_GEM) {
            if (t->rel + 2.5f > -6.0f && t->rel + 2.5f < 6.0f && fabsf(t->y - (g->y + 5.0f)) < 7.0f) {
                ++g->gems;
                g->gem_ms = g->clock_ms;
                g->things[i--] = g->things[--g->count];
            }
        }
    }
    float reference = g->ground_y;
    g->cam_y += (reference - g->cam_y) * CAM_FOLLOW;
    if (g->y - g->cam_y > CAM_ABOVE_MAX) g->cam_y = g->y - CAM_ABOVE_MAX;
    if (g->cam_y - g->y > CAM_BELOW_MAX) g->cam_y = g->y + CAM_BELOW_MAX;
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
    if (g->phase == SKI_OVER) {
        if (g->clock_ms - g->over_ms < SKI_RESTART_DELAY_MS) return false;
        init_world(g);
        g->phase = SKI_PLAY;
    } else if (g->phase == SKI_READY) {
        g->phase = SKI_PLAY;
    }
    g->held = true;
    if (g->grounded) {
        float vy = SKI_JUMP_V + ski_terrain_slope(g->seed, g->x) * g->speed;
        if (vy < SKI_JUMP_V * 0.6f) vy = SKI_JUMP_V * 0.6f; /* leaving a steep drop or climb stays a normal hop */
        if (vy > SKI_JUMP_V * 1.1f) vy = SKI_JUMP_V * 1.1f;
        leave_ground(g, vy);
        g->y += 0.001f;
    }
    return true;
}
void ski_release(ski_game_t *g, uint64_t now)
{
    ski_tick(g, now);
    g->held = false;
}
void ski_snapshot(const ski_game_t *g, ski_view_t *out)
{
    uint64_t popup_age = g->clock_ms - g->popup_ms;
    bool popup = g->popup_points > 0 && popup_age < 1600U;
    float angle = g->grounded ? slope_deg(ski_terrain_slope(g->seed, g->x)) : g->base + g->rot;
    *out = (ski_view_t){.phase = g->phase, .crash = g->crash, .score = score_of(g), .best = g->best, .flips = g->flips,
        .gems = g->gems, .combo = g->combo, .speed_pps = (unsigned)lroundf(g->speed), .flips_air = g->grounded ? 0U : (unsigned)(g->rot / 360.0f),
        .popup_points = popup ? g->popup_points : 0U, .popup_flips = g->popup_flips, .popup_age_ms = (unsigned)popup_age,
        .gem_age_ms = g->gems ? (unsigned)(g->clock_ms - g->gem_ms) : 100000U,
        .over_age_ms = g->phase == SKI_OVER ? (unsigned)(g->clock_ms - g->over_ms) : 0U, .count = g->count,
        .seed = g->seed, .x = g->x, .y = g->y, .cam_y = g->cam_y, .angle = angle, .rot = g->rot,
        .grounded = g->grounded, .held = g->held, .new_best = g->phase == SKI_OVER && g->record,
        .can_restart = g->phase == SKI_OVER && g->clock_ms - g->over_ms >= SKI_RESTART_DELAY_MS,
        .popup_perfect = g->popup_perfect};
    memcpy(out->things, g->things, sizeof out->things);
}
