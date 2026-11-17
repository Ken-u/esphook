#include "fishing_core.h"
#include <string.h>

static const fs_tier_t k_tier[FS_TIERS] = {
    /* name      len need win  appr pause hold% hold_min hold_max amp half weight   min_g   max_g */
    {"鲫鱼", 14, 2, 260, 900, 300, 0, 0, 0, 3, 220, 40, 150, 500},
    {"鲈鱼", 18, 3, 230, 820, 280, 25, 500, 800, 5, 190, 30, 600, 1700},
    {"鲤鱼", 22, 5, 200, 740, 250, 35, 550, 950, 8, 160, 18, 1800, 4500},
    {"金枪鱼", 26, 7, 170, 680, 220, 40, 600, 1100, 12, 130, 9, 6000, 14000},
    {"金龙鱼", 30, 9, 150, 620, 200, 45, 650, 1200, 16, 110, 3, 18000, 40000},
};
#define BASE_X 132

const fs_tier_t *fs_tier(unsigned tier) { return &k_tier[tier < FS_TIERS ? tier : FS_TIERS - 1U]; }
static uint32_t rnd(fs_t *f)
{
    f->rng ^= f->rng << 13;
    f->rng ^= f->rng >> 17;
    f->rng ^= f->rng << 5;
    return f->rng;
}
static int rr(fs_t *f, int lo, int hi) { return lo + (int)(rnd(f) % (uint32_t)(hi - lo + 1)); }
static int iabs(int v) { return v < 0 ? -v : v; }
static uint32_t sat_add(uint32_t v, uint32_t d) { return v + d > FS_AGE_MAX ? FS_AGE_MAX : v + d; }
static void set_phase(fs_t *f, fs_phase_t p)
{
    f->phase = (uint8_t)p;
    f->phase_ms = 0;
}
static void shake(fs_t *f, int amp, unsigned half, unsigned dur)
{
    if (amp < 1) amp = 1;
    if (f->shake_pending && amp < f->shake_amp) return; /* a stronger request is already waiting */
    f->shake_pending = 1;
    f->shake_amp = (uint8_t)amp;
    f->shake_half = (uint16_t)half;
    f->shake_dur = (uint16_t)(dur > 5000U ? 5000U : dur);
    ++f->shake_seq;
}
bool fs_take_shake(fs_t *f, int *amp_deg, unsigned *half_ms, unsigned *duration_ms)
{
    if (!f->shake_pending) return false;
    f->shake_pending = 0;
    *amp_deg = f->shake_amp;
    *half_ms = f->shake_half;
    *duration_ms = f->shake_dur;
    return true;
}

/* ---- the fish ------------------------------------------------------------------------------------------------ */
static int base_x(const fs_t *f)
{
    const int span = BASE_X - FS_FISH_X_MIN - 12;
    return BASE_X - span * (f->need ? (int)f->done * 1000 / (int)f->need : 0) / 1000; /* it is reeled in step by step */
}
static void new_goal(fs_t *f, bool dash)
{
    const int bx = base_x(f);
    int x = bx + rr(f, -10, 10), y = rr(f, FS_FISH_Y_MIN, FS_FISH_Y_MAX);
    if (dash) x += rr(f, 6, 14); /* a pull: it lunges away from the angler */
    if (x < FS_FISH_X_MIN) x = FS_FISH_X_MIN;
    if (x > FS_FISH_X_MAX) x = FS_FISH_X_MAX;
    f->gx = (int16_t)(x * 16);
    f->gy = (int16_t)(y * 16);
    f->move_wait = (uint16_t)rr(f, 35, 80);
}
static void fish_step(fs_t *f)
{
    f->fx = (int16_t)(f->fx + (f->gx - f->fx) / 14);
    f->fy = (int16_t)(f->fy + (f->gy - f->fy) / 14);
    if (f->move_wait) --f->move_wait;
    else if (f->phase == FS_FIGHT) new_goal(f, false);
}
int fs_fish_x(const fs_t *f) { return f->fx / 16; }
int fs_fish_y(const fs_t *f) { return f->fy / 16; }

/* ---- phases -------------------------------------------------------------------------------------------------- */
static unsigned pick_tier(fs_t *f)
{
    int roll = rr(f, 0, 99), acc = 0;
    for (unsigned i = 0; i < FS_TIERS; ++i) {
        acc += k_tier[i].weight;
        if (roll < acc) return i;
    }
    return 0;
}
static void start_wait(fs_t *f)
{
    set_phase(f, FS_WAIT);
    f->wait_ms = (uint32_t)rr(f, (int)FS_WAIT_MIN_MS, (int)FS_WAIT_MAX_MS);
    f->nibbles = 0;
    f->nibble_at_ms = f->wait_ms * 35U / 100U;
}
static void start_bite(fs_t *f)
{
    set_phase(f, FS_BITE);
    f->tier = (uint8_t)pick_tier(f);
    const fs_tier_t *t = &k_tier[f->tier];
    f->weight_g = t->min_g + rnd(f) % (t->max_g - t->min_g + 1U);
    f->bite_ms = 900U - (uint32_t)f->tier * 80U;
    shake(f, 4 + f->tier, 150, 520);
}
static void start_fight(fs_t *f)
{
    const fs_tier_t *t = &k_tier[f->tier];
    set_phase(f, FS_FIGHT);
    f->need = t->need;
    f->done = 0;
    f->lives = FS_LIVES;
    f->combo = 0;
    f->last_result = FR_NONE;
    memset(&f->p, 0, sizeof f->p);
    f->p.state = FP_NONE;
    f->next_start_ms = (uint32_t)rr(f, 500, 1100); /* the fish starts moving on its own, at a random moment */
    f->fx = (int16_t)(BASE_X * 16);
    f->fy = (int16_t)(44 * 16);
    new_goal(f, false);
    shake(f, 3 + f->tier, 140, 400);
}
static void spawn_prompt(fs_t *f)
{
    const fs_tier_t *t = &k_tier[f->tier];
    const bool hold = t->hold_pct && (unsigned)rr(f, 0, 99) < t->hold_pct;
    f->p.kind = hold ? FP_HOLD : FP_TAP;
    f->p.state = FP_APPROACH;
    f->p.win_ms = t->win_ms;
    f->p.approach_ms = (uint16_t)(t->approach_ms + (unsigned)rr(f, -60, 100));
    f->p.hold_ms = hold ? (uint16_t)rr(f, t->hold_min_ms, t->hold_max_ms) : 0U;
    f->p.start_ms = f->phase_ms;
    f->p.target_ms = f->phase_ms + f->p.approach_ms;
    f->p.press_quality = 0;
    shake(f, t->amp_deg + rr(f, -1, 1), t->half_ms, (unsigned)f->p.approach_ms + f->p.hold_ms + 150U);
    new_goal(f, true);
}
static void resolve(fs_t *f, fs_result_t r)
{
    const fs_tier_t *t = &k_tier[f->tier];
    f->p.state = FP_DONE;
    f->p.res_ms = f->phase_ms;
    f->last_result = (uint8_t)r;
    f->result_age_ms = 0;
    if (r == FR_PERFECT || r == FR_GOOD) {
        ++f->done;
        ++f->combo;
        if (r == FR_PERFECT) ++f->perfect;
        shake(f, 2, 120, 260);
        new_goal(f, false);
    } else {
        --f->lives;
        f->combo = 0;
        shake(f, t->amp_deg + 4, t->half_ms, 500); /* the line jerks */
    }
    if (!f->lives) {
        ++f->lost;
        set_phase(f, FS_LOST);
        shake(f, t->amp_deg + 6, t->half_ms, 700);
        return;
    }
    if (f->done >= f->need) {
        ++f->caught;
        if (f->weight_g > f->best_g) { f->best_g = f->weight_g; f->best_tier = f->tier; }
        set_phase(f, FS_LAND);
        shake(f, 5, 130, 500);
        return;
    }
    f->next_start_ms = f->phase_ms + t->pause_ms + (uint32_t)rr(f, 0, 200);
}
static void fight_tick(fs_t *f)
{
    if (f->p.state == FP_NONE || f->p.state == FP_DONE) {
        if (f->phase_ms >= f->next_start_ms) spawn_prompt(f);
    } else if (f->p.state == FP_APPROACH) {
        if (f->phase_ms > f->p.target_ms + f->p.win_ms) resolve(f, FR_LATE);
    } else if (f->p.state == FP_HELD) {
        if (f->phase_ms > f->p.target_ms + f->p.hold_ms + f->p.win_ms + 40U) resolve(f, FR_LATE);
    }
}

void fs_init(fs_t *f, uint32_t seed, uint32_t now_ms)
{
    memset(f, 0, sizeof *f);
    f->rng = seed ? seed : 0x9E3779B1U;
    f->now_ms = now_ms;
    f->lives = FS_LIVES;
    f->result_age_ms = FS_AGE_MAX;
    f->fx = (int16_t)(BASE_X * 16);
    f->fy = (int16_t)(44 * 16);
    f->gx = f->fx;
    f->gy = f->fy;
    f->phase = FS_IDLE;
}
void fs_update(fs_t *f, uint32_t now_ms)
{
    uint32_t dt = now_ms > f->now_ms ? now_ms - f->now_ms : 0U;
    if (dt > FS_STALL_MS) dt = FS_STALL_MS;
    f->now_ms = now_ms;
    f->phase_ms += dt;
    f->result_age_ms = sat_add(f->result_age_ms, dt);
    f->move_carry_ms += dt;
    while (f->move_carry_ms >= 10U) {
        f->move_carry_ms -= 10U;
        fish_step(f);
    }
    switch (f->phase) {
    case FS_CAST:
        if (f->phase_ms >= FS_CAST_MS) start_wait(f);
        break;
    case FS_WAIT:
        if (f->nibbles < FS_NIBBLES && f->phase_ms >= f->nibble_at_ms && f->phase_ms + 400U < f->wait_ms) {
            ++f->nibbles;
            f->nibble_at_ms = f->wait_ms * (35U + 30U * f->nibbles) / 100U;
            shake(f, 2, 120, 300); /* a nibble: not yet */
        }
        if (f->phase_ms >= f->wait_ms) start_bite(f);
        break;
    case FS_BITE:
        if (f->phase_ms >= f->bite_ms) { ++f->missed_bites; start_wait(f); } /* it spat the hook out */
        break;
    case FS_FIGHT: fight_tick(f); break;
    case FS_LAND:
        if (f->phase_ms >= FS_LAND_MS) set_phase(f, FS_RESULT);
        break;
    case FS_LOST:
        if (f->phase_ms >= FS_LOST_MS) set_phase(f, FS_IDLE);
        break;
    default: break;
    }
}
static unsigned quality(unsigned off, unsigned win) { return off * 100U <= win * 40U ? FR_PERFECT : FR_GOOD; }
void fs_press(fs_t *f, uint32_t now_ms)
{
    fs_update(f, now_ms);
    switch (f->phase) {
    case FS_IDLE:
    case FS_RESULT:
        if (f->phase == FS_RESULT && f->phase_ms < FS_RESULT_MIN_MS) return;
        ++f->casts;
        set_phase(f, FS_CAST);
        break;
    case FS_WAIT: /* pulled the line in before anything bit */
        f->last_result = FR_EARLY;
        f->result_age_ms = 0;
        set_phase(f, FS_IDLE);
        break;
    case FS_BITE: start_fight(f); break;
    case FS_FIGHT: {
        if (f->p.state != FP_APPROACH) return; /* stray presses between pulls cost nothing */
        const int d = (int)f->phase_ms - (int)f->p.target_ms;
        if (iabs(d) <= f->p.win_ms) {
            const unsigned q = quality((unsigned)iabs(d), f->p.win_ms);
            if (f->p.kind == FP_TAP) resolve(f, (fs_result_t)q);
            else { f->p.state = FP_HELD; f->p.press_quality = (uint8_t)q; }
        } else resolve(f, FR_EARLY); /* the ring was still far away */
        break;
    }
    default: break;
    }
}
void fs_release(fs_t *f, uint32_t now_ms)
{
    fs_update(f, now_ms);
    if (f->phase != FS_FIGHT || f->p.state != FP_HELD) return;
    const int d = (int)f->phase_ms - (int)(f->p.target_ms + f->p.hold_ms);
    const unsigned win = (unsigned)f->p.win_ms + 40U;
    if (iabs(d) <= (int)win) {
        const unsigned q = quality((unsigned)iabs(d), win);
        resolve(f, (fs_result_t)(q == FR_PERFECT && f->p.press_quality == FR_PERFECT ? FR_PERFECT : FR_GOOD));
    } else resolve(f, FR_EARLY); /* let go too soon */
}
int fs_to_target(const fs_t *f)
{
    if (f->phase != FS_FIGHT) return INT32_MAX;
    if (f->p.state == FP_APPROACH) return (int)f->p.target_ms - (int)f->phase_ms;
    if (f->p.state == FP_HELD) return (int)(f->p.target_ms + f->p.hold_ms) - (int)f->phase_ms;
    return INT32_MAX;
}
int fs_ring(const fs_t *f)
{
    if (f->phase != FS_FIGHT || f->p.state != FP_APPROACH || !f->p.approach_ms) return 0;
    int left = (int)f->p.target_ms - (int)f->phase_ms;
    if (left < 0) left = 0;
    if (left > f->p.approach_ms) left = f->p.approach_ms;
    return left * 256 / f->p.approach_ms;
}
int fs_hold_fill(const fs_t *f)
{
    if (f->phase != FS_FIGHT || f->p.state != FP_HELD || !f->p.hold_ms) return 0;
    int v = ((int)f->phase_ms - (int)f->p.target_ms) * 256 / f->p.hold_ms;
    return v < 0 ? 0 : v > 256 ? 256 : v;
}
