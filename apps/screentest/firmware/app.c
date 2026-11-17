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
 * colours and a grey ramp (every colour inverted means the polarity is wrong). K1 sets it right without a PC:
 * a short press inverts the colours, a 500 ms hold swaps the offsets between the two flavours, and leaving the app
 * (three presses) stores what is on screen. The PC does the same with `robot screentest plain` / `plugin` / `invert` /
 * `gap` / `raw` / `save` / `mhz` (framework/include/esphook/app_screen.h).
 *
 * command() runs on the USB task. It only parses, asks the screen service, and answers; the service is thread safe
 * and nothing here touches LVGL or the app's session state. */
#define PLUGIN_X 1
#define PLUGIN_Y 26
#define PLAIN_X 0
#define PLAIN_Y 24

typedef struct {
    lv_obj_t *variant, *values;
    app_screen_t shown;
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
    app_ui_text(hint, "短按 反色 长按 偏移");
    app_ui_center(hint);
    s->variant = app_ui_label(ctx->root, &aihook_font_12, 14, 33, 132, 16, LV_LABEL_LONG_MODE_CLIP);
    s->values = app_ui_label(ctx->root, &aihook_font_12, 14, 49, 132, 16, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->variant);
    app_ui_center(s->values);
}

static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->changed) return;
    app_screen_t now;
    app_screen_read(&now);
    now.mhz = now.saved_mhz; /* the clock is changed only on purpose, by `mhz` */
    (void)app_screen_apply(&now, true);
}

static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->kind != APP_EVENT_SHORT && e->kind != APP_EVENT_CONFIRM) return;
    app_screen_t now;
    app_screen_read(&now);
    if (e->kind == APP_EVENT_SHORT) now.invert = !now.invert;
    else if (now.x_gap == PLUGIN_X && now.y_gap == PLUGIN_Y) { now.x_gap = PLAIN_X; now.y_gap = PLAIN_Y; }
    else { now.x_gap = PLUGIN_X; now.y_gap = PLUGIN_Y; }
    if (app_screen_apply(&now, false)) s->changed = true;
}

static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    app_screen_t now;
    app_screen_read(&now);
    if (s->drawn && !memcmp(&now, &s->shown, sizeof now)) return;
    s->shown = now;
    s->drawn = true;
    char line[40];
    snprintf(line, sizeof line, "%s %s", app_screen_variant(&now), now.invert ? "INV" : "NORM");
    app_ui_text(s->variant, line);
    snprintf(line, sizeof line, "gap %u,%u  %uMHz", (unsigned)now.x_gap, (unsigned)now.y_gap, (unsigned)now.mhz);
    app_ui_text(s->values, line);
}

static void status(const char *prefix, const app_screen_t *s)
{
    (void)app_command_replyf("%sinvert=%u x=%u y=%u mhz=%u saved_mhz=%u variant=%s", prefix, s->invert ? 1U : 0U,
                             (unsigned)s->x_gap, (unsigned)s->y_gap, (unsigned)s->mhz, (unsigned)s->saved_mhz,
                             app_screen_variant(s));
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
    .refresh_ms = 100, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .render = render, .icon = icon, .command = command};
