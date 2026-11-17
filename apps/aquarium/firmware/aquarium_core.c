#include "aquarium_core.h"
#include <string.h>
#include "esphook/app_math.h"
#include "esphook/app_rand.h"

#define X_MIN 10
#define X_MAX 150
#define Y_MIN (AQ_SURFACE + 6)
#define Y_MAX (AQ_FLOOR - 5)
#define CRAB_Y (AQ_FLOOR - 2)
#define PXS(v) ((v) * 256 / 100) /* px per second -> 1/256 px per step */

/* kind: body length, cruising speed (px/s), mouth reach is half the length */
static const struct { int len, speed; } k_kind[] = {
    [AQ_CLOWN] = {16, 15}, [AQ_TANG] = {20, 26}, [AQ_ANGEL] = {22, 13},
    [AQ_TETRA] = {12, 30}, [AQ_JELLY] = {12, 0},  [AQ_CRAB] = {12, 9},
};
static const int8_t k_sin32[32] = {0,   25,  49,  71,  90,  106, 117, 125, 127, 125,  117, 106, 90,  71,  49,  25,
                                   0,   -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25};

int aq_fish_len(unsigned kind) { return kind <= AQ_CRAB ? k_kind[kind].len : 12; }

int aq_hand_x(const aq_t *a)
{
    const uint32_t ph = a->clock_ms % AQ_HAND_PERIOD_MS, half = AQ_HAND_PERIOD_MS / 2U;
    const uint32_t d = ph < half ? ph : AQ_HAND_PERIOD_MS - ph;
    return AQ_HAND_MIN + (int)(d * (uint32_t)(AQ_HAND_MAX - AQ_HAND_MIN) / half);
}
int aq_food_x(const aq_food_t *f)
{
    int sway = f->landed ? 0 : k_sin32[(f->age / 4U) & 31U] * 3 / 127;
    return f->x / 256 + sway;
}
unsigned aq_food_live(const aq_t *a)
{
    unsigned n = 0;
    for (unsigned i = 0; i < AQ_FOOD; ++i) n += a->food[i].alive ? 1U : 0U;
    return n;
}

static void spawn_bubble(aq_t *a, int x_px, int y_px, unsigned size)
{
    for (unsigned i = 0; i < AQ_BUBBLES; ++i) {
        aq_bubble_t *b = &a->bubble[i];
        if (b->alive) continue;
        b->x = (int16_t)x_px;
        b->y = (int16_t)(y_px * 256);
        b->speed = (uint8_t)app_rand_range(&a->rng, 16, 40);
        b->size = (uint8_t)size;
        b->alive = 1;
        return;
    }
}

/* ---- fish ---------------------------------------------------------------------------------------------------- */
static void new_goal(aq_t *a, aq_fish_t *f)
{
    int gx, gy, steps;
    switch (f->kind) {
    case AQ_CLOWN:
        if (app_rand_next(&a->rng) % 5U) { gx = AQ_ANEMONE_X + app_rand_range(&a->rng, -26, 10); gy = AQ_FLOOR - 12 + app_rand_range(&a->rng, -10, 2); }
        else { gx = app_rand_range(&a->rng, 20, 140); gy = app_rand_range(&a->rng, 22, 46); }
        steps = app_rand_range(&a->rng, 150, 350);
        break;
    case AQ_TANG: gx = app_rand_range(&a->rng, 14, 146); gy = app_rand_range(&a->rng, 18, 44); steps = app_rand_range(&a->rng, 300, 600); break;
    case AQ_ANGEL: gx = app_rand_range(&a->rng, 14, 146); gy = app_rand_range(&a->rng, 18, 40); steps = app_rand_range(&a->rng, 300, 600); break;
    case AQ_TETRA: gx = a->school_x + app_rand_range(&a->rng, -14, 14); gy = a->school_y + app_rand_range(&a->rng, -6, 6); steps = app_rand_range(&a->rng, 60, 140); break;
    default: gx = app_rand_range(&a->rng, 14, 146); gy = app_rand_range(&a->rng, 20, 40); steps = app_rand_range(&a->rng, 300, 600); break;
    }
    f->gx = (int16_t)app_clamp(gx, X_MIN + 2, X_MAX - 2);
    f->gy = (int16_t)app_clamp(gy, Y_MIN + 2, Y_MAX - 2);
    f->wander = (uint16_t)steps;
}
/* Eases the velocity towards "speed towards the goal"; a steering step never leaves a fish stuck just short of it. */
static void steer(aq_fish_t *f, int gx, int gy, int speed)
{
    const int sx = (gx * 256 - f->x) / 16, sy = (gy * 256 - f->y) / 16; /* 1/16 px */
    int dist = (int)app_isqrt((uint32_t)(sx * sx + sy * sy));
    if (dist < 1) dist = 1;
    const int want_x = sx * speed / dist, want_y = sy * speed / dist;
    int dx = want_x - f->vx, dy = want_y - f->vy;
    int step_x = dx / 10, step_y = dy / 10;
    if (!step_x && dx) step_x = dx > 0 ? 1 : -1;
    if (!step_y && dy) step_y = dy > 0 ? 1 : -1;
    f->vx = (int16_t)(f->vx + step_x);
    f->vy = (int16_t)(f->vy + step_y);
}
static void integrate(aq_fish_t *f)
{
    f->x += f->vx;
    f->y += f->vy;
    if (f->x < X_MIN * 256) { f->x = X_MIN * 256; if (f->vx < 0) f->vx = 0; }
    if (f->x > X_MAX * 256) { f->x = X_MAX * 256; if (f->vx > 0) f->vx = 0; }
    if (f->y < Y_MIN * 256) { f->y = Y_MIN * 256; if (f->vy < 0) f->vy = 0; }
    if (f->y > Y_MAX * 256) { f->y = Y_MAX * 256; if (f->vy > 0) f->vy = 0; }
    if (f->vx > 5) f->dir = 1;
    else if (f->vx < -5) f->dir = -1;
}
static void move(aq_fish_t *f)
{
    integrate(f);
    const int speed = app_abs(f->vx) + app_abs(f->vy);
    f->tail = (uint16_t)(f->tail + 2U + (unsigned)speed / 24U);
}
static bool try_eat(aq_t *a, aq_fish_t *f)
{
    const int mx = f->x / 256 + f->dir * (aq_fish_len(f->kind) / 2), my = f->y / 256;
    for (unsigned i = 0; i < AQ_FOOD; ++i) {
        aq_food_t *p = &a->food[i];
        if (!p->alive || (f->kind == AQ_CRAB && !p->landed)) continue;
        if (app_abs(aq_food_x(p) - mx) > 5 || app_abs(p->y / 256 - my) > 5) continue;
        p->alive = 0;
        if (a->eaten < AQ_COUNT_MAX) ++a->eaten;
        a->since_eat_ms = 0;
        a->eat_x = (int16_t)mx;
        a->eat_y = (int16_t)my;
        spawn_bubble(a, mx, my - 2, 0);
        if (++f->eaten >= 3U) { f->eaten = 0; f->full = (uint16_t)(AQ_FULL_MS / AQ_STEP_MS); }
        return true;
    }
    return false;
}
/* The nearest pellet this fish would go for (within 90 px), or NULL. */
static const aq_food_t *seek(const aq_t *a, const aq_fish_t *f, bool landed_only)
{
    const aq_food_t *best = NULL;
    int best_d = 90 * 90 + 1;
    for (unsigned i = 0; i < AQ_FOOD; ++i) {
        const aq_food_t *p = &a->food[i];
        if (!p->alive || (landed_only && !p->landed)) continue;
        int dx = aq_food_x(p) - f->x / 256, dy = p->y / 256 - f->y / 256, d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = p; }
    }
    return best;
}
static void swim(aq_t *a, aq_fish_t *f)
{
    if (f->full) --f->full;
    const bool was_chasing = f->chase != 0U;
    f->chase = 0;
    const aq_food_t *food = f->full ? NULL : seek(a, f, false);
    if (food) {
        f->gx = (int16_t)aq_food_x(food);
        f->gy = (int16_t)(food->y / 256);
        f->chase = 1;
    } else {
        if (was_chasing) f->wander = 0;
        if (f->wander) --f->wander;
        if (!f->wander || (app_abs(f->gx - f->x / 256) < 3 && app_abs(f->gy - f->y / 256) < 3)) new_goal(a, f);
    }
    int speed = PXS(k_kind[f->kind].speed);
    if (f->chase) {
        speed = speed * 22 / 10;
        if (app_abs(f->gx - f->x / 256) < 10 && app_abs(f->gy - f->y / 256) < 8) speed = speed / 2;
    }
    steer(f, f->gx, f->gy, speed);
    move(f);
    if (!f->full) (void)try_eat(a, f);
}
static void jelly(aq_t *a, aq_fish_t *f)
{
    f->tail++;
    if (!f->wander || app_abs(f->gx - f->x / 256) < 4) new_goal(a, f);
    else --f->wander;
    const int y = f->y / 256;
    if (y > AQ_FLOOR - 16 && f->tail % 220U > 60U) f->tail = (uint16_t)(f->tail - f->tail % 220U); /* too low: pulse now */
    int want_vy = f->tail % 220U < 40U ? -32 : 5;
    if (y < AQ_SURFACE + 12) want_vy = 8;
    int want_vx = (f->gx - f->x / 256) > 0 ? 7 : -7;
    f->vy = (int16_t)(f->vy + (want_vy - f->vy) / 8);
    f->vx = (int16_t)(f->vx + (want_vx - f->vx) / 16);
    integrate(f); /* the pulse phase lives in tail, so no swim animation here */
}
static void crab(aq_t *a, aq_fish_t *f)
{
    if (f->full) --f->full;
    const aq_food_t *food = f->full ? NULL : seek(a, f, true);
    int speed = PXS(k_kind[AQ_CRAB].speed);
    if (food) { /* scuttles over to what has reached the sand */
        f->gx = (int16_t)aq_food_x(food);
        f->chase = 1;
        speed *= 2;
    } else {
        if (f->chase) { f->chase = 0; f->wander = 0; }
        if (f->wander) --f->wander;
        else {
            f->gx = app_rand_next(&a->rng) % 3U == 0U ? (int16_t)(f->x / 256) : (int16_t)app_rand_range(&a->rng, 16, 144); /* sometimes it just sits */
            f->wander = (uint16_t)app_rand_range(&a->rng, 150, 450);
        }
    }
    const int dx = f->gx - f->x / 256, want = app_abs(dx) < 2 ? 0 : dx > 0 ? speed : -speed;
    f->vx = (int16_t)(f->vx + (want - f->vx) / 6);
    f->vy = 0;
    f->x = app_clamp(f->x + f->vx, X_MIN * 256, X_MAX * 256);
    f->y = CRAB_Y * 256;
    if (f->vx > 4) f->dir = 1;
    else if (f->vx < -4) f->dir = -1;
    f->tail = (uint16_t)(f->tail + (unsigned)app_abs(f->vx) / 2U);
    if (!f->full) (void)try_eat(a, f);
}

/* ---- the tank ------------------------------------------------------------------------------------------------ */
static void place(aq_t *a, unsigned i, unsigned kind, int x, int y)
{
    aq_fish_t *f = &a->fish[i];
    f->kind = (uint8_t)kind;
    f->x = x * 256;
    f->y = y * 256;
    f->dir = (i & 1U) ? -1 : 1;
    f->gx = (int16_t)x;
    f->gy = (int16_t)y;
    f->tail = (uint16_t)(i * 37U);
    f->wander = (uint16_t)app_rand_range(&a->rng, 1, 120);
}
void aq_init(aq_t *a, uint32_t seed)
{
    memset(a, 0, sizeof *a);
    a->rng = seed ? seed : 0x2545F491U;
    a->since_feed_ms = a->since_eat_ms = APP_AGE_MAX;
    a->school_x = a->school_gx = 84;
    a->school_y = a->school_gy = 26;
    a->bubble_wait = 60;
    a->school_wait = 200;
    a->drop_x = 80;
    static const struct { unsigned kind; int x, y; } k_start[AQ_FISH] = {
        {AQ_CLOWN, 118, 44}, {AQ_CLOWN, 134, 48}, {AQ_TANG, 56, 30},  {AQ_ANGEL, 102, 26},
        {AQ_ANGEL, 36, 40},  {AQ_TETRA, 80, 24},  {AQ_TETRA, 88, 28}, {AQ_TETRA, 76, 30},
        {AQ_TETRA, 92, 22},  {AQ_TETRA, 84, 32},  {AQ_JELLY, 136, 24}, {AQ_CRAB, 84, CRAB_Y},
    };
    for (unsigned i = 0; i < AQ_FISH; ++i) place(a, i, k_start[i].kind, k_start[i].x, k_start[i].y);
}
bool aq_feed(aq_t *a)
{
    if (a->cooldown) return false;
    unsigned free_slots = 0;
    for (unsigned i = 0; i < AQ_FOOD; ++i) free_slots += a->food[i].alive ? 0U : 1U;
    if (!free_slots) return false;
    const int hx = aq_hand_x(a);
    unsigned dropped = 0;
    for (unsigned i = 0; i < AQ_FOOD && dropped < AQ_PELLETS; ++i) {
        aq_food_t *p = &a->food[i];
        if (p->alive) continue;
        p->alive = 1;
        p->age = (uint16_t)(app_rand_next(&a->rng) % 128U); /* a different sway phase for each */
        p->landed = 0;
        p->x = (int32_t)(hx + ((int)dropped - 1) * 3 + app_rand_range(&a->rng, -1, 1)) * 256;
        p->y = (int32_t)(AQ_SURFACE + 1 + (int)dropped) * 256; /* just under the fingers, staggered so they do not overlap */
        ++dropped;
    }
    a->drop_x = (int16_t)hx;
    if (a->thrown < AQ_COUNT_MAX) ++a->thrown;
    a->cooldown = (uint16_t)(AQ_FEED_COOLDOWN_MS / AQ_STEP_MS);
    a->since_feed_ms = 0;
    return true;
}
static void step(aq_t *a)
{
    a->clock_ms += AQ_STEP_MS;
    a->since_feed_ms = app_age_add(a->since_feed_ms, AQ_STEP_MS);
    a->since_eat_ms = app_age_add(a->since_eat_ms, AQ_STEP_MS);
    if (a->cooldown) --a->cooldown;
    /* pellets sink, sway, settle on the sand and dissolve there */
    for (unsigned i = 0; i < AQ_FOOD; ++i) {
        aq_food_t *p = &a->food[i];
        if (!p->alive) continue;
        if (!p->landed) {
            ++p->age;
            p->y += 20;
            if (p->y >= (AQ_FLOOR - 1) * 256) { p->y = (AQ_FLOOR - 1) * 256; p->landed = 1; }
        } else if (++p->landed > AQ_LANDED_MS / AQ_STEP_MS) p->alive = 0;
    }
    /* the school's meeting point drifts around the tank */
    if (a->school_wait) --a->school_wait;
    else {
        a->school_gx = (int16_t)app_rand_range(&a->rng, 30, 130);
        a->school_gy = (int16_t)app_rand_range(&a->rng, 20, 42);
        a->school_wait = (uint16_t)app_rand_range(&a->rng, 300, 600);
    }
    if (a->clock_ms % 80U == 0U) {
        a->school_x = (int16_t)(a->school_x + (a->school_gx > a->school_x) - (a->school_gx < a->school_x));
        a->school_y = (int16_t)(a->school_y + (a->school_gy > a->school_y) - (a->school_gy < a->school_y));
    }
    for (unsigned i = 0; i < AQ_FISH; ++i) {
        aq_fish_t *f = &a->fish[i];
        if (f->kind == AQ_JELLY) jelly(a, f);
        else if (f->kind == AQ_CRAB) crab(a, f);
        else swim(a, f);
    }
    for (unsigned i = 0; i < AQ_BUBBLES; ++i) {
        aq_bubble_t *b = &a->bubble[i];
        if (!b->alive) continue;
        b->y = (int16_t)(b->y - b->speed);
        if (b->y < (AQ_SURFACE + 1) * 256) b->alive = 0;
    }
    if (a->bubble_wait) --a->bubble_wait;
    else {
        spawn_bubble(a, AQ_CHEST_X + app_rand_range(&a->rng, -1, 1), AQ_FLOOR - 12, (unsigned)app_rand_range(&a->rng, 0, 2));
        a->bubble_wait = (uint16_t)app_rand_range(&a->rng, 80, 220);
    }
}
void aq_advance(aq_t *a, uint32_t dt_ms)
{
    for (unsigned n = app_step_take(&a->carry_ms, dt_ms, AQ_STEP_MS, AQ_MAX_ADVANCE_MS); n; --n) step(a);
}
