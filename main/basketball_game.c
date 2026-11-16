#include "basketball_game.h"
#include <math.h>
#include <string.h>

static unsigned next_speed(basketball_game_t *g, unsigned old)
{
    uint32_t x = g->random ? g->random : 0x9e3779b9U;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->random = x;
    const unsigned count = BASKETBALL_MAX_SPEED - BASKETBALL_MIN_SPEED + 1;
    unsigned speed = BASKETBALL_MIN_SPEED + x % count;
    /* Every completed attempt changes the pace, including a miss. */
    if (speed == old) speed = BASKETBALL_MIN_SPEED + (speed - BASKETBALL_MIN_SPEED + 1) % count;
    return speed;
}
void basketball_start(basketball_game_t *g, uint64_t now, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->phase = BASKETBALL_PLAY;
    g->started_ms = g->round_ms = now;
    g->random = seed;
    g->speed_pps = next_speed(g, 0);
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
    } else if (g->in_flight && now >= g->round_ms) {
        g->in_flight = false;
    }
}
static float hoop_x(const basketball_game_t *g, uint64_t now)
{
    if (g->in_flight && now < g->round_ms) return g->shot_x;
    uint64_t elapsed = now >= g->round_ms ? now - g->round_ms : 0;
    return 16.0f + fmodf((float)elapsed * g->speed_pps / 1000.0f, 128.0f);
}
bool basketball_shoot(basketball_game_t *g, uint64_t now)
{
    basketball_tick(g, now);
    if (g->phase != BASKETBALL_PLAY || g->in_flight || now < g->round_ms) return false;
    g->shot_x = hoop_x(g, now);
    g->last_hit = fabsf(g->shot_x - 80.0f) <= BASKETBALL_HIT_PX;
    ++g->shots;
    if (g->last_hit) ++g->made;
    g->shot_ms = now;
    g->round_ms = now + BASKETBALL_SHOT_MS;
    g->in_flight = true;
    g->speed_pps = next_speed(g, g->speed_pps);
    return true;
}
void basketball_snapshot(const basketball_game_t *g, uint64_t now, basketball_view_t *out)
{
    *out = (basketball_view_t){.phase = g->phase, .shots = g->shots, .made = g->made,
        .score = g->made * 2, .speed_pps = g->speed_pps, .last_hit = g->last_hit,
        .in_flight = g->phase == BASKETBALL_PLAY && g->in_flight && now >= g->shot_ms && now < g->round_ms,
        .hoop_x = hoop_x(g, now), .ball_x = 80, .ball_y = 57};
    uint64_t elapsed = now >= g->started_ms ? now - g->started_ms : 0;
    if (g->phase == BASKETBALL_PLAY && elapsed < BASKETBALL_DURATION_MS)
        out->seconds_left = (unsigned)((BASKETBALL_DURATION_MS - elapsed + 999) / 1000);
    if (out->in_flight) {
        float t = (float)(now - g->shot_ms);
        if (t <= 450) {
            float p = t / 450;
            out->ball_y = 57 - 23 * p - 120 * p * (1 - p);
        } else {
            float p = fminf(1, (t - 450) / 200);
            out->ball_y = 34 + (g->last_hit ? 16 : 32) * p;
            if (!g->last_hit) out->ball_x += (g->shot_x < 80 ? 15 : -15) * p;
        }
    }
}
