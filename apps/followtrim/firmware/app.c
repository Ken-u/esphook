#include "esphook/app.h"
#include "esphook/app_command.h"
#include "esphook/app_math.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define EXIT_HOLD_MS 2000U

typedef enum { CENTER, LEFT, RIGHT, RESET, OPTION_COUNT } option_t;
typedef struct {
    lv_obj_t *value, *detail, *hint, *footer, *hold;
    option_t option;
    int draft_deg;
    uint32_t save_seq;
    uint64_t down_ms;
    bool editing, preview_due, confirm_save, saving, failed, needs_target;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "followtrim state too large");

static lv_obj_t *label(app_context_t *ctx, const lv_font_t *font, int y, int height, uint32_t color)
{
    lv_obj_t *obj = app_ui_label(ctx->root, font, 0, y, APP_WIDTH, height, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(obj);
    lv_obj_set_style_text_color(obj, app_ui_color(color), LV_PART_MAIN);
    return obj;
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    app_ui_rect(ctx->root, 0, 0, APP_WIDTH, APP_HEIGHT, APP_COLOR_BG, 0);
    app_ui_rect(ctx->root, 0, 0, APP_WIDTH, 16, 0x16283A, 0);
    app_ui_text(label(ctx, &aihook_font_12, 0, 16, APP_COLOR_TEXT), "摆头补偿");
    s->value = label(ctx, &aihook_font_16, 18, 20, APP_COLOR_ACCENT);
    s->detail = label(ctx, &aihook_font_12, 37, 14, APP_COLOR_TEXT);
    s->hint = label(ctx, &aihook_font_12, 51, 14, APP_COLOR_TEXT);
    s->footer = label(ctx, &aihook_font_12, 65, 14, APP_COLOR_MUTED);
    s->hold = app_ui_hold_bar(ctx->root, 16, APP_COLOR_ACCENT);
    app_set_double_back(ctx, false);
}

static int limit_trim(int degrees)
{
    return app_clamp(degrees, -ROBOT_FOLLOW_TRIM_MAX_DEG, ROBOT_FOLLOW_TRIM_MAX_DEG);
}
static bool capture_center(state_t *s, const app_follow_trim_t *trim)
{
    s->needs_target = !trim->online || trim->tracking != ROBOT_TRACKING;
    if (s->needs_target) return false;
    s->draft_deg = limit_trim(-(int)lroundf(trim->aim_deg));
    return true;
}
static void start_preview(app_context_t *ctx, state_t *s, const app_follow_trim_t *trim)
{
    s->draft_deg = s->option == RESET ? 0 : trim->saved_deg;
    if (s->option == CENTER && !capture_center(s, trim)) return;
    s->failed = !app_servo_request(ctx, APP_SERVO_ARM, 0);
    if (s->failed) return;
    s->editing = s->preview_due = true;
    app_set_triple_exit(ctx, false);
    app_set_exit_hold(ctx, EXIT_HOLD_MS);
}
static void save(app_context_t *ctx, state_t *s)
{
    app_follow_trim_t trim;
    app_follow_trim_read(&trim);
    s->save_seq = trim.save_seq;
    s->saving = app_follow_trim_apply(ctx, s->draft_deg, true);
    s->failed = !s->saving;
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->key || s->saving) return;
    if (e->kind == APP_EVENT_CANCEL) { s->confirm_save = false; return; }
    if (e->kind == APP_EVENT_DOWN) { s->down_ms = e->ms; return; }
    if (e->kind == APP_EVENT_UP && s->confirm_save) {
        s->confirm_save = false;
        if (e->ms - s->down_ms >= EXIT_HOLD_MS) app_menu(ctx);
        else save(ctx, s);
        return;
    }
    if (e->kind != APP_EVENT_SHORT && e->kind != APP_EVENT_CONFIRM) return;
    app_follow_trim_t trim;
    app_follow_trim_read(&trim);
    if (!trim.available) { if (e->kind == APP_EVENT_SHORT) app_menu(ctx); return; }
    if (e->kind == APP_EVENT_CONFIRM) {
        if (s->editing) s->confirm_save = true;
        else start_preview(ctx, s, &trim);
        return;
    }
    s->failed = s->needs_target = false;
    if (!s->editing) s->option = (s->option + 1) % OPTION_COUNT;
    else if (s->option == CENTER) s->preview_due |= capture_center(s, &trim);
    else if (s->option != RESET) {
        s->draft_deg = limit_trim(s->draft_deg + (s->option == LEFT ? 1 : -1));
        s->preview_due = true;
    }
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->saving) {
        app_follow_trim_t trim;
        app_follow_trim_read(&trim);
        if (trim.save_seq == s->save_seq) return;
        s->saving = false;
        s->failed = !trim.save_ok;
        if (!s->failed) { app_idle(ctx); return; }
    }
    if (s->preview_due && app_follow_trim_apply(ctx, s->draft_deg, false)) s->preview_due = false;
}
static void render(app_context_t *ctx)
{
    const state_t *s = ctx->state;
    app_follow_trim_t trim;
    app_follow_trim_read(&trim);
    const int degrees = s->editing ? s->draft_deg : trim.saved_deg;
    char value[24];
    if (degrees) snprintf(value, sizeof value, "%s %d°", degrees > 0 ? "左" : "右", abs(degrees));
    else snprintf(value, sizeof value, "0°");
    app_ui_text(s->value, value);
    app_ui_hold_line(s->hold, s->editing ? app_exit_hold_progress(ctx) : app_hold_ms(ctx), trim.available,
                     s->editing ? EXIT_HOLD_MS : 0);
    const char *detail, *hint, *footer;
    if (!trim.available) {
        detail = "雷达或舵机不可用";
        hint = "短按返回";
        footer = "";
    } else if (!s->editing) {
        static const char *const options[] = {"正前取中", "向左调", "向右调", "清零"};
        detail = s->failed ? "试转失败" : s->needs_target && trim.tracking != ROBOT_TRACKING ? "等待雷达目标" : options[s->option];
        hint = "短按换项 长按试转";
        footer = s->option == CENTER ? "坐正前方再取中" : "";
    } else {
        static const char *const instructions[] = {"短按再取 长按保存", "短按左调 长按保存", "短按右调 长按保存", "长按保存"};
        detail = s->saving ? "正在保存" : s->failed ? "保存失败 再试一次" :
                 trim.preview && trim.tracking == ROBOT_TRACKING ? "跟随预览" : "等待雷达目标";
        hint = s->confirm_save ? "松手保存" : instructions[s->option];
        footer = "按住2秒退出";
    }
    app_ui_text(s->detail, detail);
    app_ui_text(s->hint, hint);
    app_ui_text(s->footer, footer);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot followtrim status")) return false;
    app_follow_trim_t trim;
    app_follow_trim_read(&trim);
    (void)app_command_replyf("followtrim saved=%d active=%d aim=%.1f preview=%u tracking=%u ready=%u saves=%lu",
        trim.saved_deg, trim.active_deg, (double)trim.aim_deg, trim.preview ? 1U : 0U,
        (unsigned)trim.tracking, trim.available ? 1U : 0U, (unsigned long)trim.save_seq);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    const uint32_t color = selected ? APP_COLOR_ACCENT : APP_COLOR_MUTED;
    app_ui_rect(parent, x + 3, y + 13, 22, 2, color, 1);
    app_ui_rect(parent, x + 3, y + 9, 2, 10, color, 1);
    app_ui_rect(parent, x + 23, y + 9, 2, 10, color, 1);
    app_ui_rect(parent, x + 11, y + 7, 6, 14, APP_COLOR_TEXT, 2);
}
const app_descriptor_t esphook_app_followtrim = {
    .api_version = APP_API_VERSION, .id = "followtrim", .title = "摆头补偿",
    .menu_group = APP_MENU_TOOLS, .refresh_ms = 50, .state_size = sizeof(state_t),
    .on_enter = enter, .on_tick = tick, .on_event = event, .render = render, .icon = icon, .command = command,
};
