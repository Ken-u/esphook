#include "esphook/app.h"
#include "esphook/app_command.h"
#include "esphook/app_screen.h"
#include "esphook/app_ui.h"
#include <stdio.h>
#include <string.h>

/* 屏幕测试: a test pattern for a new 0.96" ST7735 module and the way to set it right (app_screen.h).
 *
 * The pattern shows what goes wrong with the wrong flavour at a glance: a white frame that must touch all four
 * edges, a grid whose vertical lines must stay vertical (a picture that slants a pixel more with every row has the
 * wrong x offset), colour squares in the corners (red, green, blue, yellow from top left, clockwise), the primary
 * colours and a grey ramp (every colour inverted means the polarity is wrong).
 *
 * K1 sets it right without a PC. A short press moves on to the next of four combinations of polarity and offsets
 * (plugin, plugin with the colours the other way round, plain, plain inverted) - press until the picture is right -
 * and a 500 ms hold stores what is on screen and leaves. The PC does the same with `robot screentest plain` /
 * `plugin` / `invert` / `gap` / `raw` / `save` / `mhz` (framework/include/esphook/app_screen.h).
 *
 * Only the hold stores. Any other way out (the triple press, a command, the idle timeout below) puts the panel back
 * the way it was when the app started - the triple press also delivers its first clicks as short presses, which must
 * not leave a changed panel behind.
 *
 * A board that was never set up (no stored tuning) starts here: `first_run` makes this the first screen at boot,
 * because a garbled screen is too hard to navigate to the menu with. In that run the triple press is off, the hold
 * stores and ends the run with app_idle() - the board goes on to the standby screen - and 30 s without a key ends
 * it the same way without storing, so a board nobody is watching still starts (and asks again at the next boot).
 * Opened from the menu the app is the same, and the hold returns to the menu.
 *
 * command() runs on the USB task. It only parses, asks the screen service, and answers; the service is thread safe
 * and nothing here touches LVGL or the app's session state. */
#define PLUGIN_X 1
#define PLUGIN_Y 26
#define PLAIN_X 0
#define PLAIN_Y 24
#define FIRST_RUN_IDLE_MS 30000U

/* Polarity and offsets a module is known to need, in the order K1 steps through them (likeliest first). */
static const struct { bool invert; uint8_t x, y; } presets[] = {
    {true, PLUGIN_X, PLUGIN_Y}, {false, PLUGIN_X, PLUGIN_Y}, {false, PLAIN_X, PLAIN_Y}, {true, PLAIN_X, PLAIN_Y},
};
#define PRESETS (sizeof presets / sizeof presets[0])

typedef struct {
    lv_obj_t *variant, *values, *hold;
    app_screen_t shown, entry; /* entry: what was on screen when the app started */
    uint64_t key_ms; /* the last key, for the first run's idle timeout */
    bool drawn, changed;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "screentest state too large");

static void rects(lv_obj_t *root)
{
    app_ui_rect(root, 0, 0, APP_WIDTH, APP_HEIGHT, 0x000000, 0);
    for (int x = 20; x < APP_WIDTH; x += 20) app_ui_rect(root, x, 0, 1, APP_HEIGHT, 0x383838, 0);
    for (int y = 20; y < APP_HEIGHT; y += 20) app_ui_rect(root, 0, y, APP_WIDTH, 1, 0x383838, 0);
    app_ui_rect(root, 0, 0, APP_WIDTH, 1, 0xFFFFFF, 0);
    app_ui_rect(root, 0, APP_HEIGHT - 1, APP_WIDTH, 1, 0xFFFFFF, 0);
    app_ui_rect(root, 0, 0, 1, APP_HEIGHT, 0xFFFFFF, 0);
    app_ui_rect(root, APP_WIDTH - 1, 0, 1, APP_HEIGHT, 0xFFFFFF, 0);
    app_ui_rect(root, 2, 2, 6, 6, 0xFF0000, 0);
    app_ui_rect(root, APP_WIDTH - 8, 2, 6, 6, 0x00FF00, 0);
    app_ui_rect(root, APP_WIDTH - 8, APP_HEIGHT - 8, 6, 6, 0xFFFF00, 0);
    app_ui_rect(root, 2, APP_HEIGHT - 8, 6, 6, 0x0000FF, 0);
    static const uint32_t bars[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x00FFFF, 0xFF00FF, 0xFFFF00};
    for (int i = 0; i < 7; ++i) app_ui_rect(root, 24 + i * 16, 22, 16, 8, bars[i], 0);
    for (int i = 0; i < 8; ++i) {
        const uint32_t g = (uint32_t)i * 255U / 7U;
        app_ui_rect(root, 24 + i * 14, 67, 14, 9, (g << 16) | (g << 8) | g, 0);
    }
}

static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    rects(ctx->root);
    lv_obj_t *hint = app_ui_label(ctx->root, &aihook_font_12, 14, 3, 132, 16, LV_LABEL_LONG_MODE_CLIP);
    app_ui_text(hint, "短按 切换 长按 确认");
    app_ui_center(hint);
    s->hold = app_ui_hold_bar(ctx->root, 65, 0xFFFFFF);
    s->key_ms = ctx->now_ms;
    app_screen_read(&s->entry);
    if (ctx->first_run) app_set_triple_exit(ctx, false); /* the way on is the confirmation (or the idle timeout) */
    s->variant = app_ui_label(ctx->root, &aihook_font_12, 14, 33, 132, 16, LV_LABEL_LONG_MODE_CLIP);
    s->values = app_ui_label(ctx->root, &aihook_font_12, 14, 49, 132, 16, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->variant);
    app_ui_center(s->values);
}

static bool first_run(void)
{
    app_screen_t now;
    app_screen_read(&now);
    return !now.configured;
}

static void leave(app_context_t *ctx)
{
    const state_t *s = ctx->state;
    if (s->changed) (void)app_screen_apply(&s->entry, false); /* not confirmed by the hold: the panel goes back */
}

static size_t preset_of(const app_screen_t *s)
{
    for (size_t i = 0; i < PRESETS; ++i)
        if (s->invert == presets[i].invert && s->x_gap == presets[i].x && s->y_gap == presets[i].y) return i;
    return PRESETS;
}

static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    s->key_ms = ctx->now_ms;
    if (e->kind != APP_EVENT_SHORT && e->kind != APP_EVENT_CONFIRM) return;
    app_screen_t now;
    app_screen_read(&now);
    if (e->kind == APP_EVENT_SHORT) {
        const size_t at = preset_of(&now);
        const size_t next = at == PRESETS ? 0 : (at + 1U) % PRESETS; /* a value set from the PC starts the round again */
        now.invert = presets[next].invert;
        now.x_gap = presets[next].x;
        now.y_gap = presets[next].y;
        if (app_screen_apply(&now, false)) s->changed = true;
        return;
    }
    if (s->changed || !now.configured) { /* an unchanged, stored tuning needs no flash write */
        now.mhz = now.saved_mhz; /* the clock is changed only on purpose, by `mhz` */
        if (!app_screen_apply(&now, true)) return; /* busy: the hold can be repeated */
    }
    s->changed = false;
    if (ctx->first_run) app_idle(ctx);
    else app_menu(ctx);
}

static void tick(app_context_t *ctx)
{
    const state_t *s = ctx->state;
    if (ctx->first_run && ctx->now_ms - s->key_ms >= FIRST_RUN_IDLE_MS) app_idle(ctx);
}

static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    app_ui_hold_line(s->hold, app_hold_ms(ctx), true, 0);
    app_screen_t now;
    app_screen_read(&now);
    if (s->drawn && !memcmp(&now, &s->shown, sizeof now)) return;
    s->shown = now;
    s->drawn = true;
    char line[40];
    const size_t index = preset_of(&now);
    if (index < PRESETS) snprintf(line, sizeof line, "%u/%u %s", (unsigned)index + 1U, (unsigned)PRESETS, now.invert ? "INV" : "NORM");
    else snprintf(line, sizeof line, "-/%u %s", (unsigned)PRESETS, now.invert ? "INV" : "NORM"); /* set from the PC: none of the four */
    app_ui_text(s->variant, line);
    snprintf(line, sizeof line, "gap %u,%u  %uMHz", (unsigned)now.x_gap, (unsigned)now.y_gap, (unsigned)now.mhz);
    app_ui_text(s->values, line);
}

static void status(const char *prefix, const app_screen_t *s)
{
    (void)app_command_replyf("%sinvert=%u x=%u y=%u mhz=%u saved_mhz=%u variant=%s configured=%u", prefix,
                             s->invert ? 1U : 0U, (unsigned)s->x_gap, (unsigned)s->y_gap, (unsigned)s->mhz,
                             (unsigned)s->saved_mhz, app_screen_variant(s), s->configured ? 1U : 0U);
}

static bool command(const char *line)
{
    app_screen_cmd_t cmd;
    if (!app_screen_parse(line, &cmd)) return false;
    app_screen_t now;
    app_screen_read(&now);
    switch (cmd.kind) {
    case APP_SCREEN_CMD_STATUS:
        status("", &now);
        break;
    case APP_SCREEN_CMD_SET:
        if (cmd.has_invert) now.invert = cmd.value.invert;
        if (cmd.has_gap) { now.x_gap = cmd.value.x_gap; now.y_gap = cmd.value.y_gap; }
        if (app_screen_apply(&now, false)) status("OK (not saved; `robot screentest save`) ", &now);
        else (void)app_command_reply("ERR busy, repeat");
        break;
    case APP_SCREEN_CMD_SAVE: {
        const uint8_t in_use = now.mhz;
        now.mhz = cmd.has_mhz ? cmd.value.mhz : now.saved_mhz;
        if (app_screen_apply(&now, true)) { /* the flash write follows within a few tens of milliseconds */
            now.saved_mhz = now.mhz;
            now.mhz = in_use;
            status(cmd.has_mhz ? "OK saving, the clock needs a power cycle " : "OK saving ", &now);
        } else {
            (void)app_command_reply("ERR busy, repeat");
        }
        break;
    }
    case APP_SCREEN_CMD_RESET:
        (void)app_command_reply(app_screen_forget() ? "OK forgotten, defaults applied, the next boot sets the screen first"
                                                    : "ERR busy, repeat");
        break;
    case APP_SCREEN_CMD_RAW:
        (void)app_command_reply(app_screen_raw(cmd.raw, cmd.raw_length) ? "OK sent" : "ERR busy, repeat");
        break;
    case APP_SCREEN_CMD_OPEN:
        (void)app_command_reply(app_command_open("screentest") ? "OK screentest queued" : "ERR queue full");
        break;
    case APP_SCREEN_CMD_MENU:
        (void)app_command_menu();
        (void)app_command_reply("OK menu queued");
        break;
    case APP_SCREEN_CMD_BAD:
    default:
        (void)app_command_replyf("ERR %s", cmd.error ? cmd.error : "bad command");
        break;
    }
    return true;
}

static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x + 3, y + 5, 22, 18, selected ? 0xEDF4FB : 0x8A8196, 2);
    app_ui_rect(parent, x + 5, y + 7, 18, 14, 0x000000, 1);
    app_ui_rect(parent, x + 5, y + 7, 6, 14, 0xFF3B30, 0);
    app_ui_rect(parent, x + 11, y + 7, 6, 14, 0x34C759, 0);
    app_ui_rect(parent, x + 17, y + 7, 6, 14, 0x0A84FF, 0);
}

const app_descriptor_t esphook_app_screentest = {.api_version = APP_API_VERSION, .id = "screentest", .title = "屏幕测试",
    .refresh_ms = 50, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command, .first_run = first_run};
