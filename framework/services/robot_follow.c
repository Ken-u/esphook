#include "robot_follow.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAD_TO_DEG 57.2957795131f
#define STALE_MS 800
#define HOLD_MS 1500
#define MATCH_MM 700.0f

static float clamp(float v, float lo, float hi) { return fminf(hi, fmaxf(lo, v)); }
robot_config_t robot_default_config(void)
{
    return (robot_config_t){.center_us = 1500, .limit_deg = 30, .speed_dps = 35};
}
bool robot_config_valid(const robot_config_t *c)
{
    if (c->center_us < 1400 || c->center_us > 1600 ||
        c->limit_deg < 5 || c->limit_deg > 45 ||
        c->speed_dps < 5 || c->speed_dps > 90) return false;
    int span = (c->limit_deg * 1000 + 89) / 90;
    return c->center_us - span >= 1000 && c->center_us + span <= 2000;
}
/* The queue line of robot_servo_calibrate(): "cal:<center_us>,<limit>,<speed>,<reverse>[,save]". */
bool robot_cal_parse(const char *text, robot_config_t *out, bool *save)
{
    if (!text || !out || !save || strncmp(text, "cal:", 4)) return false;
    long v[4];
    const char *p = text + 4;
    for (int i = 0; i < 4; ++i) {
        if (!((*p >= '0' && *p <= '9') || *p == '-')) return false; /* strtol alone would skip blanks and take a '+' */
        errno = 0;
        char *end;
        v[i] = strtol(p, &end, 10);
        if (errno || end == p || v[i] < -10000 || v[i] > 10000) return false;
        p = end;
        if (i < 3) { if (*p != ',') return false; ++p; }
    }
    bool store = false;
    if (*p == ',') { if (strcmp(p, ",save")) return false; store = true; }
    else if (*p != '\0') return false;
    const robot_config_t c = {.center_us = (int)v[0], .limit_deg = (int)v[1], .speed_dps = (int)v[2], .reverse = v[3] == 1};
    if ((v[3] != 0 && v[3] != 1) || !robot_config_valid(&c)) return false;
    *out = c;
    *save = store;
    return true;
}
int robot_cal_format(char *buf, size_t size, const robot_config_t *c, bool save)
{
    return snprintf(buf, size, "cal:%d,%d,%d,%d%s", c->center_us, c->limit_deg, c->speed_dps, c->reverse ? 1 : 0,
                    save ? ",save" : "");
}
void robot_follow_init(robot_follow_t *f, const robot_config_t *c, uint64_t now)
{
    memset(f, 0, sizeof *f);
    f->config = *c;
    f->started_ms = f->step_ms = now;
    f->state = ROBOT_ACQUIRING;
}
static bool usable(const ld2450_target_t *t)
{
    float range = hypotf(t->x_mm, t->y_mm);
    return t->valid && t->y_mm > 0 && range >= 200 && range <= 4000 &&
           fabsf(atan2f(t->x_mm, t->y_mm) * RAD_TO_DEG) <= 60;
}
void robot_follow_frame(robot_follow_t *f, const ld2450_frame_t *frame, uint64_t now)
{
    /* A gap must expire association BEFORE a new report can revive it. */
    if (f->locked && now - f->seen_ms > HOLD_MS) f->locked = false;
    if (!f->have_frame || now - f->frame_ms > STALE_MS) f->candidate_hits = 0;
    f->have_frame = true;
    f->frame_ms = now;
    const ld2450_target_t *best = NULL;
    float score = INFINITY;
    for (size_t i = 0; i < LD2450_TARGETS; ++i) {
        const ld2450_target_t *t = &frame->targets[i];
        if (!usable(t)) continue;
        float d = f->locked ? hypotf(t->x_mm - f->x_mm, t->y_mm - f->y_mm)
                            : hypotf(t->x_mm, t->y_mm);
        if (f->locked && d > MATCH_MM) continue;
        if (d < score) { best = t; score = d; }
    }
    if (!best) {
        f->candidate_hits = 0;
        f->state = f->locked ? ROBOT_LOST : ROBOT_IDLE;
        return;
    }
    if (!f->locked) {
        if (!f->candidate_hits || hypotf(best->x_mm - f->candidate_x,
                                        best->y_mm - f->candidate_y) > MATCH_MM)
            f->candidate_hits = 0;
        f->candidate_x = best->x_mm;
        f->candidate_y = best->y_mm;
        if (++f->candidate_hits < 3) { f->state = ROBOT_ACQUIRING; return; }
        f->x_mm = best->x_mm;
        f->y_mm = best->y_mm;
        f->locked = true;
    } else {
        f->x_mm += 0.3f * (best->x_mm - f->x_mm);
        f->y_mm += 0.3f * (best->y_mm - f->y_mm);
    }
    f->seen_ms = now;
    f->state = ROBOT_TRACKING;
    float angle = clamp(atan2f(f->x_mm, f->y_mm) * RAD_TO_DEG,
                        -f->config.limit_deg, f->config.limit_deg);
    if (fabsf(angle - f->goal_deg) >= 1.5f) f->goal_deg = angle;
}
float robot_slew(float current, float goal, float speed, float seconds)
{
    /* A stalled task must not create a large catch-up movement. */
    float step = speed * clamp(seconds, 0, 0.1f);
    return current + clamp(goal - current, -step, step);
}
void robot_follow_tick(robot_follow_t *f, uint64_t now)
{
    bool stale = !f->have_frame ? now - f->started_ms > STALE_MS
                               : now - f->frame_ms > STALE_MS;
    if (stale) f->state = ROBOT_FAULT;
    if (f->locked && now - f->seen_ms > HOLD_MS) {
        f->locked = false;
        f->candidate_hits = 0;
        f->goal_deg = 0;
        if (!stale) f->state = ROBOT_IDLE;
    }
    if (!f->locked && (stale || f->state == ROBOT_IDLE)) f->goal_deg = 0;
    f->angle_deg = robot_slew(f->angle_deg, f->goal_deg, f->config.speed_dps,
                              (now - f->step_ms) / 1000.0f);
    f->step_ms = now;
}
int robot_pulse_us(const robot_config_t *c, float angle)
{
    float v = clamp(angle, -c->limit_deg, c->limit_deg);
    int pulse = c->center_us + (int)lroundf((c->reverse ? -v : v) * 1000.0f / 90.0f);
    return (int)clamp(pulse, 1000, 2000);
}
const char *robot_state_name(robot_state_t s)
{
    static const char *const names[] = {"idle", "acquiring", "tracking", "lost", "radar_fault"};
    return (unsigned)s < sizeof names / sizeof names[0] ? names[s] : "unknown";
}
