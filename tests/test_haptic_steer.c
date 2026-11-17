/* Host tests for the servo "feel" helper (app_haptic.h) and the radar/key steering helper (app_steer.h). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_haptic.h"
#include "app_steer.h"

static app_servo_t g_servo;
static int g_requests, g_last_action, g_last_value, g_arm_requests;
static bool g_accept = true;
void app_servo_read(app_servo_t *out) { *out = g_servo; }
bool app_servo_request(app_context_t *ctx, app_servo_action_t action, int value)
{
    (void)ctx;
    if (!g_accept) return false;
    ++g_requests;
    g_last_action = action;
    g_last_value = value;
    if (action == APP_SERVO_ARM) { ++g_arm_requests; g_servo.armed = true; }
    return true;
}

static void servo(bool available, bool armed, bool fault)
{
    memset(&g_servo, 0, sizeof g_servo);
    g_servo.available = available;
    g_servo.armed = armed;
    g_servo.fault = fault;
    g_servo.limit_deg = 30;
    g_servo.speed_dps = 35;
    g_requests = g_last_action = g_last_value = g_arm_requests = 0;
    g_accept = true;
}
static int absi(int v) { return v < 0 ? -v : v; }

static void test_haptic_shake(void)
{
    haptic_t h;
    haptic_init(&h);
    int angle = 99;
    assert(!haptic_step(&h, 1000, 30, 35, &angle)); /* idle: the servo is left alone */
    haptic_shake(&h, 1000, 4, 120, 480, 35);
    /* the half period is stretched until the 35 dps servo can swing +-4 degrees */
    assert(h.half_ms >= 2 * 4 * 1000 / 35);
    int sent = 0, max = 0, min = 0, flips = 0, prev = 0;
    for (uint64_t t = 1000; t < 1700; t += 10) {
        if (haptic_step(&h, t, 30, 35, &angle)) {
            ++sent;
            if (angle > max) max = angle;
            if (angle < min) min = angle;
            if (sent > 1 && ((angle > 0) != (prev > 0))) ++flips;
            prev = angle;
        }
    }
    assert(sent >= 3 && sent <= 12); /* rate limited */
    assert(max >= 3 && max <= 4 && min <= -1 && min >= -4);
    assert(flips >= 1);
    assert(!h.shaking);
    /* back at the base angle once it is over */
    assert(angle == 0);
    /* The amplitude never leaves the stops. */
    haptic_init(&h);
    haptic_shake(&h, 5000, 30, 100, 2000, 90);
    for (uint64_t t = 5000; t < 7100; t += 10)
        if (haptic_step(&h, t, 10, 90, &angle)) assert(absi(angle) <= 9);
    /* A weaker shake does not cut a stronger one short; a stronger one replaces a weak one. */
    haptic_init(&h);
    haptic_shake(&h, 100, 10, 200, 1000, 90);
    unsigned strong = h.half_ms;
    haptic_shake(&h, 200, 2, 80, 300, 90);
    assert(h.amp_x10 == 100 && h.half_ms == strong && h.end_ms == 1100);
    haptic_shake(&h, 300, 20, 200, 500, 90);
    assert(h.amp_x10 == 200 && h.end_ms == 800);
    /* too small to feel and nonsense input are ignored */
    haptic_init(&h);
    haptic_shake(&h, 100, 0, 200, 500, 35);
    haptic_shake(&h, 100, 3, 200, 0, 35);
    assert(!h.shaking);
    assert(!haptic_step(&h, 200, 1, 35, &angle)); /* limit below 2 degrees: nothing to do */
}

static void test_haptic_base(void)
{
    haptic_t h;
    haptic_init(&h);
    int angle = 0;
    haptic_base(&h, 123); /* 12.3 degrees */
    assert(haptic_step(&h, 100, 30, 35, &angle) && angle == 12);
    assert(!haptic_step(&h, 300, 30, 35, &angle)); /* nothing changed */
    haptic_base(&h, 124);
    assert(!haptic_step(&h, 400, 30, 35, &angle)); /* still the same whole degree */
    haptic_base(&h, 1000); /* far past the stops */
    assert(haptic_step(&h, 500, 30, 35, &angle) && angle == 29);
    haptic_base(&h, -1000);
    assert(!haptic_step(&h, 520, 30, 35, &angle)); /* the 60 ms gap */
    assert(haptic_step(&h, 600, 30, 35, &angle) && angle == -29);
    haptic_stop(&h);
    assert(haptic_step(&h, 700, 30, 35, &angle) && angle == 0);
}

static void test_haptic_glue(void)
{
    haptic_t h;
    app_context_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.now_ms = 1000;
    haptic_init(&h);
    servo(false, false, false);
    assert(haptic_state() == HAPTIC_NONE);
    assert(!haptic_arm(&ctx, &h) && g_requests == 0);
    haptic_shake(&h, 1000, 4, 150, 500, 35);
    haptic_pump(&ctx, &h);
    assert(g_requests == 0);

    servo(true, false, false);
    assert(haptic_state() == HAPTIC_READY);
    haptic_pump(&ctx, &h); /* never arms by itself */
    assert(g_requests == 0 && !g_servo.armed);
    assert(haptic_arm(&ctx, &h) && g_arm_requests == 1 && g_last_action == APP_SERVO_ARM);
    assert(haptic_state() == HAPTIC_ARMED);
    assert(haptic_arm(&ctx, &h) && g_arm_requests == 1); /* already armed: no second request */

    haptic_init(&h);
    haptic_shake(&h, 1000, 4, 150, 500, 35);
    haptic_pump(&ctx, &h);
    assert(g_last_action == APP_SERVO_ANGLE && g_last_value >= 3);
    int before = g_requests;
    ctx.now_ms = 1010;
    haptic_pump(&ctx, &h);
    assert(g_requests == before); /* rate limited */

    /* a refused request is retried later */
    haptic_init(&h);
    haptic_base(&h, 100);
    g_accept = false;
    ctx.now_ms = 2000;
    haptic_pump(&ctx, &h);
    assert(!h.sent_valid);
    g_accept = true;
    ctx.now_ms = 2100;
    haptic_pump(&ctx, &h);
    assert(g_last_value == 10);

    servo(true, true, true); /* a fault silences everything */
    assert(haptic_state() == HAPTIC_FAULT);
    ctx.now_ms = 3000;
    haptic_pump(&ctx, &h);
    assert(g_requests == 0);
    assert(!haptic_arm(&ctx, &h));
}

static void radar(steer_t *s, uint64_t now, int x, int y, bool valid)
{
    ld2450_target_t t[LD2450_TARGETS];
    memset(t, 0, sizeof t);
    t[0].x_mm = (int16_t)-x; /* x is how far the player is to their own right; the sensor's +X is the player's left */
    t[0].y_mm = (int16_t)y;
    t[0].valid = valid;
    steer_radar(s, now, true, t);
}
/* People move continuously: walk the target to x over a number of 50 ms radar frames. */
static void lean(steer_t *s, uint64_t *now, int from, int to, int frames)
{
    for (int i = 1; i <= frames; ++i) {
        *now += 50;
        radar(s, *now, from + (to - from) * i / frames, 1500, true);
        steer_tick(s, *now);
    }
}
static void test_steer_radar(void)
{
    steer_t s;
    steer_init(&s);
    uint64_t now = 1000;
    assert(steer_tick(&s, now) == 0 && s.source == STEER_NONE);
    /* the first person seen defines the centre */
    for (int i = 0; i < 20; ++i) { now += 20; radar(&s, now, 300, 1500, true); steer_tick(&s, now); }
    assert(s.have_centre && s.centre_mm == 300 && absi(s.steer) < 5 && s.source == STEER_RADAR);
    /* leaning right steers right, up to full lock; left is symmetric */
    lean(&s, &now, 300, 700, 8);
    lean(&s, &now, 700, 700, 10);
    assert(s.steer > 240 && s.steer <= STEER_MAX);
    lean(&s, &now, 700, -100, 16);
    lean(&s, &now, -100, -100, 10);
    assert(s.steer < -240 && s.steer >= -STEER_MAX);
    /* inside the dead zone nothing moves */
    lean(&s, &now, -100, 320, 8);
    lean(&s, &now, 320, 320, 20);
    assert(absi(s.steer) < 3);
    /* half way is half way */
    lean(&s, &now, 320, 485, 6);
    lean(&s, &now, 485, 485, 20);
    assert(s.steer > 110 && s.steer < 150);
    /* the person stays out of view: the steering is held, then relaxes to the centre */
    uint64_t gone = now;
    while (now < gone + 800) { now += 20; steer_tick(&s, now); }
    assert(s.steer > 100 && s.locked);
    while (now < gone + 4000) { now += 20; steer_tick(&s, now); }
    assert(absi(s.steer) < 5 && !s.locked && s.source == STEER_NONE);
    /* centre is kept, so coming back to the same place is still centre */
    for (int i = 0; i < 30; ++i) { now += 20; radar(&s, now, 300, 1500, true); steer_tick(&s, now); }
    assert(absi(s.steer) < 5 && s.centre_mm == 300);
    steer_recentre(&s);
    lean(&s, &now, -200, -200, 5);
    assert(s.centre_mm == -200);
}
static void test_steer_selection(void)
{
    steer_t s;
    steer_init(&s);
    ld2450_target_t t[LD2450_TARGETS];
    memset(t, 0, sizeof t);
    /* invalid, too close, too far and too wide targets are ignored; the nearest valid one wins */
    t[0] = (ld2450_target_t){.x_mm = 0, .y_mm = 200, .valid = true};
    t[1] = (ld2450_target_t){.x_mm = 100, .y_mm = 2500, .valid = true};
    t[2] = (ld2450_target_t){.x_mm = -50, .y_mm = 1200, .valid = true};
    steer_radar(&s, 100, true, t);
    assert(s.locked && s.target_mm == 50); /* the sensor's -50 is 50 mm to the player's right */
    /* somebody else walking by does not steal the lock */
    t[2].x_mm = -60;
    t[0] = (ld2450_target_t){.x_mm = 900, .y_mm = 900, .valid = true};
    steer_radar(&s, 200, true, t);
    assert(s.target_mm == 60);
    t[0] = (ld2450_target_t){.x_mm = 0, .y_mm = 6000, .valid = true};
    t[1].valid = t[2].valid = false;
    steer_init(&s);
    steer_radar(&s, 300, true, t);
    assert(!s.locked);
    steer_radar(&s, 300, false, t); /* offline radar: ignored */
    steer_radar(&s, 300, true, NULL);
    assert(!s.locked);
    /* The player faces the machine: stepping to their own right is towards the sensor's -X, and that steers right. */
    steer_init(&s);
    uint64_t now = 1000;
    for (int i = 0; i < 12; ++i, now += 50) {
        t[0] = (ld2450_target_t){.x_mm = (int16_t)(-30 * i), .y_mm = 1500, .valid = true};
        steer_radar(&s, now, true, t);
        (void)steer_tick(&s, now);
    }
    assert(s.steer > 100);
    for (int i = 0; i < 24; ++i, now += 50) {
        t[0] = (ld2450_target_t){.x_mm = (int16_t)(-330 + 30 * i), .y_mm = 1500, .valid = true};
        steer_radar(&s, now, true, t);
        (void)steer_tick(&s, now);
    }
    assert(s.steer < -100); /* and the sensor's +X steers left */
}
static void test_steer_keys(void)
{
    steer_t s;
    steer_init(&s);
    uint64_t now = 1000;
    steer_tick(&s, now);
    steer_key(&s, 2, true);
    for (int i = 0; i < 5; ++i) { now += 20; steer_tick(&s, now); }
    int part = s.steer;
    assert(part > 0 && part < STEER_MAX && s.source == STEER_KEYS); /* ramps up, no instant full lock */
    for (int i = 0; i < 30; ++i) { now += 20; steer_tick(&s, now); }
    assert(s.steer == STEER_MAX);
    steer_key(&s, 2, false);
    for (int i = 0; i < 4; ++i) { now += 20; steer_tick(&s, now); }
    assert(s.steer > 0 && s.steer < STEER_MAX);
    for (int i = 0; i < 20; ++i) { now += 20; steer_tick(&s, now); }
    assert(s.steer == 0 && s.source == STEER_NONE);
    steer_key(&s, 1, true);
    for (int i = 0; i < 40; ++i) { now += 20; steer_tick(&s, now); }
    assert(s.steer == -STEER_MAX);
    /* both keys cancel */
    steer_key(&s, 2, true);
    for (int i = 0; i < 40; ++i) { now += 20; steer_tick(&s, now); }
    assert(s.steer == 0);
    steer_release_keys(&s);
    /* keys win over the radar while they are held */
    steer_key(&s, 2, true);
    lean(&s, &now, -300, -300, 20);
    assert(s.steer == STEER_MAX && s.source == STEER_KEYS);
    /* a long stall in the caller is not a jump */
    steer_release_keys(&s);
    now += 5000;
    steer_tick(&s, now);
    assert(s.steer > 0);
}

int main(void)
{
    test_haptic_shake();
    test_haptic_base();
    test_haptic_glue();
    test_steer_radar();
    test_steer_selection();
    test_steer_keys();
    puts("haptic/steer tests passed");
    return 0;
}
