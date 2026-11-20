#include "esphook/app.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include <stdio.h>
#include <string.h>

/* 舵机校准: sets the servo's centre pulse, travel, direction and speed with K1 alone (docs/robot-servocal.md).
 *
 * Five pages. 1 asks for the explicit go-ahead - a long press arms the servo at its centre, which is why the horn is
 * not on yet; the servo is never armed any other way. 2 centre pulse (short press +10 us, the servo follows at once),
 * 3 travel to each side (+5 degrees, the servo sweeps the new range), 4 direction (flips it; the servo turns toward the
 * player's left when the direction is right: positive angles go to the sensor's +X, the player's left when they face the
 * machine), 5 speed (the sweep shows it). A long press moves on; on the last page it stores the calibration, disarms and
 * leaves. On page 1 a short press skips for now (nothing is stored; a robot with no stored calibration is asked again
 * at the next boot).
 *
 * A robot that never stored a calibration starts here (`first_run`), after the screen setup: the triple press is off,
 * the way on is the long press or the skip, and 30 s without a key on the first page (60 s on the others) goes on to the
 * standby screen without storing anything, so the question is asked again at the next boot. Any other way out of the
 * app (the triple press from the menu, a command, the timeout) puts back the calibration the app started with and
 * disarms: only the long press on the last page stores. Everything goes through the public servo services, so the
 * motion service keeps enforcing its limits and slew rate; leaving the app disarms the servo.
 *
 * Without a servo (the `auto` and `screen` profiles never start the servo PWM, so a connected servo does nothing) the
 * first page says why and offers the way on: a long press saves the robot hardware profile and restarts, after which
 * the first run starts here by itself. That is the one deliberate switch of hardware profile outside the settings app;
 * it only starts the servo's PWM, which stays disarmed until the go-ahead on page 1. */
enum { P_INTRO, P_CENTER, P_LIMIT, P_DIRECTION, P_SPEED, P_COUNT };
#define IDLE_INTRO_MS 30000U
#define IDLE_PAGE_MS 60000U
#define CENTER_MIN 1400
#define CENTER_MAX 1600
#define CENTER_STEP 10
#define LIMIT_MIN 5
#define LIMIT_MAX 45
#define LIMIT_STEP 5
#define TRACK_X 20
#define TRACK_W 120
#define TRACK_Y 55
#define TRACK_HALF_DEG 45

static const int speeds[] = {15, 25, 35, 50, 70, 90};
#define SPEEDS (sizeof speeds / sizeof speeds[0])

typedef struct {
    lv_obj_t *title, *value, *desc, *hint, *hold, *marker, *tick_l, *tick_r;
    robot_config_t entry, cur; /* entry: what the servo used when the app started */
    uint64_t key_ms, sweep_at;
    uint32_t save_seq;
    unsigned page, phase;
    bool changed, armed, apply_due, arm_failed, save_failed, saving;
    char shown[4][40];
    int marker_x, tick_x;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "servocal state too large");

/* Why there is no servo to calibrate, and so what a press can do about it. */
typedef enum { M_ENABLE, M_RESTART, M_FAULT } missing_t;

static missing_t missing(void)
{
    app_hardware_info_t h;
    app_hardware_info(&h);
    if (h.configured != APP_PROFILE_ROBOT) return M_ENABLE;
    if (h.active != APP_PROFILE_ROBOT) return M_RESTART;
    return M_FAULT;
}

static bool first_run(void)
{
    app_servo_cal_t cal;
    app_servo_cal_read(&cal);
    return cal.available && !cal.stored;
}

static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    app_ui_rect(ctx->root, 0, 0, APP_WIDTH, APP_HEIGHT, 0x071018, 0);
    app_ui_rect(ctx->root, 0, 0, APP_WIDTH, 16, 0x16283A, 0);
    s->title = app_ui_label(ctx->root, &aihook_font_12, 3, 0, 154, 16, LV_LABEL_LONG_MODE_CLIP);
    s->value = app_ui_label(ctx->root, &aihook_font_16, 4, 18, 152, 20, LV_LABEL_LONG_MODE_CLIP);
    s->desc = app_ui_label(ctx->root, &aihook_font_12, 4, 38, 152, 16, LV_LABEL_LONG_MODE_CLIP);
    app_ui_rect(ctx->root, TRACK_X, TRACK_Y + 3, TRACK_W, 2, 0x384A5C, 0);
    app_ui_rect(ctx->root, TRACK_X + TRACK_W / 2 - 1, TRACK_Y, 2, 8, 0x8A8196, 0);
    s->tick_l = app_ui_rect(ctx->root, TRACK_X, TRACK_Y + 1, 2, 6, 0x34C759, 0);
    s->tick_r = app_ui_rect(ctx->root, TRACK_X + TRACK_W - 2, TRACK_Y + 1, 2, 6, 0x34C759, 0);
    s->marker = app_ui_rect(ctx->root, TRACK_X + TRACK_W / 2 - 2, TRACK_Y - 1, 5, 10, APP_COLOR_ACCENT, 1);
    s->hint = app_ui_label(ctx->root, &aihook_font_12, 3, 64, 154, 16, LV_LABEL_LONG_MODE_CLIP);
    s->hold = app_ui_hold_bar(ctx->root, 16, 0xFFFFFF);
    app_ui_center(s->title);
    app_ui_center(s->value);
    app_ui_center(s->desc);
    app_ui_center(s->hint);
    app_servo_cal_t cal;
    app_servo_cal_read(&cal);
    s->entry = s->cur = cal.config;
    s->key_ms = ctx->now_ms;
    s->marker_x = s->tick_x = -1000;
    if (ctx->first_run) app_set_triple_exit(ctx, false); /* the way on is the long press, the skip or the timeout */
}

static void leave(app_context_t *ctx)
{
    const state_t *s = ctx->state;
    if (!s->changed || s->saving) return; /* an explicitly requested save owns the final settings */
    (void)app_servo_request(ctx, APP_SERVO_DISARM, 0);
    (void)app_servo_cal_apply(ctx, &s->entry, false); /* not stored: the servo goes back to what it used */
}

static void done(app_context_t *ctx)
{
    if (ctx->first_run) app_idle(ctx);
    else app_menu(ctx);
}

static void restart_sweep(app_context_t *ctx, state_t *s)
{
    s->sweep_at = ctx->now_ms;
    s->phase = 0;
}

/* The next centre/travel/speed after the current one that the motion service accepts (the wrap-around skips a value that
 * does not fit with the other one); the current one when none does. */
static void step(state_t *s)
{
    robot_config_t c = s->cur;
    for (unsigned i = 0; i < 40; ++i) {
        switch (s->page) {
        case P_CENTER: c.center_us += CENTER_STEP; if (c.center_us > CENTER_MAX) c.center_us = CENTER_MIN; break;
        case P_LIMIT: c.limit_deg += LIMIT_STEP; if (c.limit_deg > LIMIT_MAX) c.limit_deg = LIMIT_MIN; break;
        case P_DIRECTION: c.reverse = !c.reverse; break;
        default: {
            unsigned k = 0;
            while (k < SPEEDS && speeds[k] <= c.speed_dps) ++k;
            c.speed_dps = speeds[k % SPEEDS];
            break;
        }
        }
        if (app_servo_cal_valid(&c)) { s->cur = c; return; }
    }
}

static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (s->saving) return;
    s->key_ms = ctx->now_ms;
    if (e->kind != APP_EVENT_SHORT && e->kind != APP_EVENT_CONFIRM) return;
    const bool hold = e->kind == APP_EVENT_CONFIRM;
    if (s->page == P_INTRO) {
        app_servo_cal_t cal;
        app_servo_cal_read(&cal);
        if (!cal.available) {
            const missing_t why = missing();
            if (hold && why == M_RESTART) { app_device_restart(); return; }
            if (hold && why == M_ENABLE) {
                s->save_failed = !app_hardware_set_profile(APP_PROFILE_ROBOT);
                if (!s->save_failed) app_device_restart();
                return;
            }
            done(ctx);
            return;
        }
        if (hold) {
            s->arm_failed = !app_servo_request(ctx, APP_SERVO_ARM, 0); /* the explicit go-ahead */
            if (s->arm_failed) return;
            s->armed = true;
            s->page = P_CENTER;
            restart_sweep(ctx, s);
        } else {
            done(ctx); /* skipped for now: nothing is stored, a first run is offered again at the next boot */
        }
        return;
    }
    if (!hold) {
        step(s);
        s->changed = s->apply_due = true;
        restart_sweep(ctx, s);
        return;
    }
    if (s->page + 1U < P_COUNT) {
        ++s->page;
        restart_sweep(ctx, s);
        return;
    }
    app_servo_cal_t cal;
    app_servo_cal_read(&cal);
    s->save_seq = cal.save_seq;
    s->saving = app_servo_cal_apply(ctx, &s->cur, true);
    s->save_failed = !s->saving;
}

/* Pages 3 to 5 move the servo so the choice can be seen: across the whole travel, a nod toward the positive side (the
 * direction), or across a part of it (the speed). */
static void sweep(app_context_t *ctx, state_t *s)
{
    if (!s->armed || s->page < P_LIMIT || s->apply_due || ctx->now_ms < s->sweep_at) return;
    const int amp = s->page == P_LIMIT ? s->cur.limit_deg : s->cur.limit_deg < 20 ? s->cur.limit_deg : 20;
    const bool nod = s->page == P_DIRECTION;
    const int to = s->phase ? (nod ? 0 : -amp) : amp;
    const int from = s->phase ? amp : (nod ? 0 : -amp);
    if (!app_servo_request(ctx, APP_SERVO_ANGLE, to)) { s->sweep_at = ctx->now_ms + 80U; return; }
    const unsigned travel = (unsigned)(to > from ? to - from : from - to);
    const unsigned speed = (unsigned)(s->cur.speed_dps > 0 ? s->cur.speed_dps : 35);
    s->sweep_at = ctx->now_ms + travel * 1000U / speed + (nod ? 700U : 350U);
    s->phase ^= 1U;
}

static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->saving) {
        app_servo_cal_t cal;
        app_servo_cal_read(&cal);
        if (cal.save_seq == s->save_seq) return;
        s->saving = false;
        s->save_failed = !cal.save_ok;
        if (cal.save_ok) {
            (void)app_servo_request(ctx, APP_SERVO_DISARM, 0);
            s->changed = false;
            done(ctx);
        }
        return;
    }
    if (s->apply_due && app_servo_cal_apply(ctx, &s->cur, false)) s->apply_due = false;
    sweep(ctx, s);
    if (ctx->first_run && ctx->now_ms - s->key_ms >= (s->page == P_INTRO ? IDLE_INTRO_MS : IDLE_PAGE_MS)) app_idle(ctx);
}

static void show(lv_obj_t *label, char *shown, const char *text)
{
    if (strcmp(shown, text)) { snprintf(shown, 40, "%s", text); app_ui_text(label, text); }
}

static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    app_ui_hold_line(s->hold, app_hold_ms(ctx), true, 0);
    app_servo_cal_t cal;
    app_servo_t servo;
    app_servo_cal_read(&cal);
    app_servo_read(&servo);
    char title[40], value[40], desc[40], hint[40];
    snprintf(title, sizeof title, "舵机校准 %u/%u", s->page + 1U, (unsigned)P_COUNT);
    snprintf(hint, sizeof hint, "长按 下一步");
    switch (s->page) {
    case P_INTRO:
        if (!cal.available) {
            snprintf(hint, sizeof hint, "短按 返回");
            switch (missing()) {
            case M_ENABLE:
                snprintf(value, sizeof value, "舵机未启用");
                snprintf(desc, sizeof desc, "%s", s->save_failed ? "保存失败 再试一次" : "长按 切到机器人配置");
                if (!s->save_failed) snprintf(hint, sizeof hint, "会重启 短按 返回");
                break;
            case M_RESTART:
                snprintf(value, sizeof value, "舵机待重启");
                snprintf(desc, sizeof desc, "长按 重启生效");
                break;
            default:
                snprintf(value, sizeof value, "舵机初始化失败");
                snprintf(desc, sizeof desc, "检查舵机接线与5V");
                break;
            }
        } else {
            snprintf(value, sizeof value, "先别装舵盘");
            snprintf(desc, sizeof desc, "%s", s->arm_failed ? "解锁失败 再试一次" : "长按 解锁并回中");
            snprintf(hint, sizeof hint, "%s", ctx->first_run ? "短按 先跳过" : "短按 返回");
        }
        break;
    case P_CENTER:
        snprintf(value, sizeof value, "中位 %dus", s->cur.center_us);
        snprintf(desc, sizeof desc, "舵机应停在正中");
        snprintf(hint, sizeof hint, "短按 +%d 长按 下一步", CENTER_STEP);
        break;
    case P_LIMIT:
        snprintf(value, sizeof value, "摆幅 ±%d°", s->cur.limit_deg);
        snprintf(desc, sizeof desc, "左右各转这么多");
        snprintf(hint, sizeof hint, "短按 +%d 长按 下一步", LIMIT_STEP);
        break;
    case P_DIRECTION:
        snprintf(value, sizeof value, "方向 %s", s->cur.reverse ? "反向" : "正向");
        snprintf(desc, sizeof desc, "应先朝你的左边转");
        snprintf(hint, sizeof hint, "短按 反向 长按 对了");
        break;
    default:
        snprintf(value, sizeof value, "速度 %d°/s", s->cur.speed_dps);
        snprintf(desc, sizeof desc, "%s", s->saving ? "正在保存" : s->save_failed ? "保存失败 再试一次" :
                 servo.fault ? "舵机故障" : "越大转得越快");
        snprintf(hint, sizeof hint, "%s", s->saving ? "" : "短按 换档 长按 保存");
        break;
    }
    show(s->title, s->shown[0], title);
    show(s->value, s->shown[1], value);
    show(s->desc, s->shown[2], desc);
    show(s->hint, s->shown[3], hint);

    /* The track: the commanded angle, positive to the left (the player's left), with the travel marked at both ends. */
    const bool track = s->armed && servo.armed;
    app_ui_hide(s->marker, !track);
    app_ui_hide(s->tick_l, !track || s->page < P_LIMIT);
    app_ui_hide(s->tick_r, !track || s->page < P_LIMIT);
    const int mx = TRACK_X + TRACK_W / 2 - 2 - (int)(servo.angle_deg * (float)(TRACK_W / 2) / (float)TRACK_HALF_DEG);
    if (mx != s->marker_x) { s->marker_x = mx; lv_obj_set_x(s->marker, mx); }
    const int tx = s->cur.limit_deg * (TRACK_W / 2) / TRACK_HALF_DEG;
    if (tx != s->tick_x) {
        s->tick_x = tx;
        lv_obj_set_x(s->tick_l, TRACK_X + TRACK_W / 2 - tx - 1);
        lv_obj_set_x(s->tick_r, TRACK_X + TRACK_W / 2 + tx - 1);
    }
}

static void icon(void *parent, int x, int y, bool selected)
{
    const uint32_t body = selected ? 0xEDF4FB : 0x8A8196;
    app_ui_rect(parent, x + 4, y + 10, 16, 12, body, 2);
    app_ui_rect(parent, x + 6, y + 5, 5, 6, body, 1);
    app_ui_rect(parent, x + 13, y + 3, 3, 9, 0xFDB927, 1);
    app_ui_rect(parent, x + 10, y + 4, 9, 3, 0xFDB927, 1);
    app_ui_rect(parent, x + 9, y + 14, 6, 6, 0x071018, 3);
}

const app_descriptor_t esphook_app_servocal = {.api_version = APP_API_VERSION, .id = "servocal", .title = "舵机校准",
    .refresh_ms = 50, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .first_run = first_run};
