#include "basketball_game.h"
#include <math.h>
#include <string.h>

/* Counter-based generator: any seed (including 0 and 1) gives a well mixed, distinct stream. */
static uint32_t draw(uint32_t *state)
{
    uint32_t z = (*state += 0x9e3779b9U);
    z ^= z >> 16; z *= 0x85ebca6bU;
    z ^= z >> 13; z *= 0xc2b2ae35U;
    return z ^ (z >> 16);
}
static float clamp_x(float x)
{
    return fminf(BASKETBALL_HOOP_MAX_X, fmaxf(BASKETBALL_HOOP_MIN_X, x));
}
/* Multiply before dividing: integer speeds and times then give exact positions. */
static float path_x(const basketball_path_t *p, uint64_t at)
{
    uint64_t dt = at > p->leg_ms ? at - p->leg_ms : 0;
    return clamp_x(p->x0 + p->v * (float)dt / 1000.0f);
}
/* Starts a new leg at `at`: random speed, 50% chance to turn around, never into a wall. */
static void redraw(basketball_path_t *p, uint64_t at, float x)
{
    const unsigned speeds = BASKETBALL_MAX_SPEED - BASKETBALL_MIN_SPEED + 1;
    unsigned speed = BASKETBALL_MIN_SPEED + draw(&p->random) % speeds;
    bool reverse = draw(&p->random) % 2 != 0;
    float direction = p->v < 0.0f ? -1.0f : 1.0f;
    if (reverse) direction = -direction;
    if (x <= BASKETBALL_HOOP_MIN_X) direction = 1.0f;
    else if (x >= BASKETBALL_HOOP_MAX_X) direction = -1.0f;
    p->leg_ms = at;
    p->x0 = x;
    p->v = direction * (float)speed;
    p->redraw_ms = at + BASKETBALL_LEG_MIN_MS + draw(&p->random) % (BASKETBALL_LEG_MAX_MS - BASKETBALL_LEG_MIN_MS + 1);
}
static uint64_t wall_ms(const basketball_path_t *p)
{
    float distance = p->v > 0.0f ? BASKETBALL_HOOP_MAX_X - p->x0 : p->x0 - BASKETBALL_HOOP_MIN_X;
    return p->leg_ms + (uint64_t)ceilf(fmaxf(distance, 0.0f) * 1000.0f / fabsf(p->v));
}
/* Commits every leg boundary up to `to`. Boundaries depend only on the path itself, so the result
 * is the same whether this runs every frame or once. */
static void advance(basketball_path_t *p, uint64_t to)
{
    if (p->v == 0.0f) return; /* never started */
    for (;;) {
        uint64_t wall = wall_ms(p);
        bool bounce = p->redraw_ms > wall;
        uint64_t at = bounce ? wall : p->redraw_ms;
        if (at > to) return;
        if (bounce) {
            p->x0 = p->v > 0.0f ? BASKETBALL_HOOP_MAX_X : BASKETBALL_HOOP_MIN_X;
            p->leg_ms = at;
            p->v = -p->v;
        } else {
            redraw(p, at, path_x(p, at));
        }
    }
}
void basketball_start(basketball_game_t *g, uint64_t now, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->phase = BASKETBALL_PLAY;
    g->started_ms = g->ready_ms = now;
    g->path.random = seed;
    /* Neither the start position nor the first direction is fixed. */
    float start = BASKETBALL_HOOP_MIN_X + (float)(draw(&g->path.random) % 1001U) / 1000.0f
        * (BASKETBALL_HOOP_MAX_X - BASKETBALL_HOOP_MIN_X);
    redraw(&g->path, now, start);
}
void basketball_stop(basketball_game_t *g)
{
    g->phase = BASKETBALL_OFF;
    g->in_flight = false;
}
void basketball_tick(basketball_game_t *g, uint64_t now)
{
    if (g->phase != BASKETBALL_PLAY) return;
    if (now >= g->started_ms && now - g->started_ms >= BASKETBALL_DURATION_MS) {
        g->phase = BASKETBALL_RESULT;
        g->in_flight = false;
        return;
    }
    /* Legs are committed late so that an input stamped a little before this tick, and read-backs of
     * the last second, still see the exact position. Committing only caches the path. */
    advance(&g->path, now > BASKETBALL_COMMIT_LAG_MS ? now - BASKETBALL_COMMIT_LAG_MS : 0);
    if (g->in_flight && now >= g->ready_ms) g->in_flight = false;
}
float basketball_hoop_at(const basketball_game_t *g, uint64_t at)
{
    basketball_path_t p = g->path;
    advance(&p, at);
    return path_x(&p, at);
}
bool basketball_shoot(basketball_game_t *g, uint64_t now)
{
    basketball_tick(g, now);
    if (g->phase != BASKETBALL_PLAY || g->in_flight || now < g->ready_ms) return false;
    g->rim_x = basketball_hoop_at(g, now + BASKETBALL_RIM_MS);
    g->last_hit = fabsf(g->rim_x - BASKETBALL_CENTER_X) <= BASKETBALL_HIT_PX;
    ++g->shots;
    if (g->last_hit) ++g->made;
    g->shot_ms = now;
    g->ready_ms = now + BASKETBALL_SHOT_MS;
    g->in_flight = true;
    return true;
}
void basketball_snapshot(const basketball_game_t *g, uint64_t now, basketball_view_t *out)
{
    basketball_path_t path = g->path;
    advance(&path, now);
    bool flying = g->phase == BASKETBALL_PLAY && g->in_flight && now >= g->shot_ms && now < g->ready_ms;
    bool reached = flying && now - g->shot_ms >= BASKETBALL_RIM_MS;
    /* A made shot is counted when it is released but shown only when the ball gets to the rim. */
    uint32_t made = g->made - (flying && !reached && g->last_hit ? 1 : 0);
    *out = (basketball_view_t){.phase = g->phase, .shots = g->shots, .made = made, .score = made * 2,
        .speed_pps = (unsigned)lroundf(fabsf(path.v)), .last_hit = g->last_hit, .in_flight = flying,
        .reached_rim = reached, .hoop_x = path_x(&path, now), .ball_x = BASKETBALL_CENTER_X, .ball_y = 57};
    uint64_t elapsed = now >= g->started_ms ? now - g->started_ms : 0;
    if (g->phase == BASKETBALL_PLAY && elapsed < BASKETBALL_DURATION_MS)
        out->seconds_left = (unsigned)((BASKETBALL_DURATION_MS - elapsed + 999) / 1000);
    if (flying) {
        float t = (float)(now - g->shot_ms);
        if (!reached) {
            float p = t / (float)BASKETBALL_RIM_MS;
            out->ball_y = 57 - 23 * p - 120 * p * (1 - p);
        } else {
            float p = fminf(1, (t - (float)BASKETBALL_RIM_MS) / 200);
            out->ball_y = 34 + (g->last_hit ? 16 : 32) * p;
            if (g->last_hit) /* falls through the net, which keeps moving along its path */
                out->ball_x = BASKETBALL_CENTER_X + (out->hoop_x - BASKETBALL_CENTER_X) * p;
            else
                out->ball_x += (g->rim_x < BASKETBALL_CENTER_X ? 15 : -15) * p;
        }
    }
}
