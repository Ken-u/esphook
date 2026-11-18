#include "esphook/app.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include <stdio.h>
#include <string.h>

/* 蜂鸣测试: hear the GPIO7 passive buzzer without a PC (docs/hardware.md).
 *
 * One list, K1 only: a short press moves to the next entry, a 500 ms hold plays it. The entries are the five named
 * sounds the firmware uses (notification, confirm, key, error, alarm ring), a sweep that runs the pitch from 500 to
 * 5000 Hz while the screen shows where it is (a passive buzzer is only loud near its resonance, usually somewhere
 * between 2 and 4 kHz: the sweep finds it), single notes from 1000 to 4500 Hz to compare, and last the mute switch.
 * Any key stops a sweep that is running. Nothing is stored except the mute switch, which is the same one as the USB
 * `sound on|off`; a muted device starts on that entry.
 *
 * Every sound goes through the public buzzer services (app_beep, app_beep_tone), which queue it for the buzzer's own
 * task: this app never blocks, never touches the LEDC timers, and so cannot disturb the servo's PWM. A muted device
 * says so instead of playing silently. */
#define SWEEP_FROM_HZ 500U
#define SWEEP_TO_HZ 5000U
#define SWEEP_STEP_HZ 100U
#define SWEEP_NOTE_MS 100U
#define SWEEP_GAP_MS 105U /* a little over the note, so the buzzer's queue never fills */
#define TONE_MS 400U
#define TOAST_MS 2200U

typedef enum { K_MUTE, K_SOUND, K_SWEEP, K_TONE } kind_t;
typedef struct {
    kind_t kind;
    app_beep_t sound;
    unsigned hz;
    const char *name;
} item_t;

static const item_t items[] = {
    {K_SOUND, APP_BEEP_NOTIFY, 0, "通知"},
    {K_SOUND, APP_BEEP_CONFIRM, 0, "确认"},
    {K_SOUND, APP_BEEP_KEY, 0, "按键"},
    {K_SOUND, APP_BEEP_ERROR, 0, "错误"},
    {K_SOUND, APP_BEEP_ALARM, 0, "闹钟响铃"},
    {K_SWEEP, APP_BEEP_NOTIFY, 0, "扫频"},
    {K_TONE, APP_BEEP_NOTIFY, 1000, "音调"},
    {K_TONE, APP_BEEP_NOTIFY, 1500, "音调"},
    {K_TONE, APP_BEEP_NOTIFY, 2000, "音调"},
    {K_TONE, APP_BEEP_NOTIFY, 2500, "音调"},
    {K_TONE, APP_BEEP_NOTIFY, 3000, "音调"},
    {K_TONE, APP_BEEP_NOTIFY, 3500, "音调"},
    {K_TONE, APP_BEEP_NOTIFY, 4000, "音调"},
    {K_TONE, APP_BEEP_NOTIFY, 4500, "音调"},
    {K_MUTE, APP_BEEP_NOTIFY, 0, "声音"},
};
#define ITEMS (sizeof items / sizeof items[0])
#define MUTE_ITEM (ITEMS - 1U) /* the last entry of the round */

typedef struct {
    lv_obj_t *mute, *name, *desc, *hint, *hold;
    uint64_t toast_until, play_until, sweep_next_ms;
    unsigned index, sweep_hz; /* sweep_hz: the next note of a running sweep, 0 when none */
    char toast[24];
    char shown[4][40];
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "buzztest state too large");

static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    app_ui_rect(ctx->root, 0, 0, APP_WIDTH, APP_HEIGHT, 0x071018, 0);
    app_ui_rect(ctx->root, 0, 0, APP_WIDTH, 16, 0x16283A, 0);
    lv_obj_t *title = app_ui_label(ctx->root, &aihook_font_12, 4, 0, 80, 16, LV_LABEL_LONG_MODE_CLIP);
    app_ui_text(title, "蜂鸣测试");
    s->mute = app_ui_label(ctx->root, &aihook_font_12, 84, 0, 72, 16, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s->mute, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s->name = app_ui_label(ctx->root, &aihook_font_16, 4, 20, 152, 20, LV_LABEL_LONG_MODE_CLIP);
    s->desc = app_ui_label(ctx->root, &aihook_font_12, 4, 42, 152, 16, LV_LABEL_LONG_MODE_CLIP);
    s->hint = app_ui_label(ctx->root, &aihook_font_12, 3, 64, 154, 16, LV_LABEL_LONG_MODE_CLIP);
    s->hold = app_ui_hold_bar(ctx->root, 16, 0xFFFFFF);
    app_ui_center(s->name);
    app_ui_center(s->desc);
    app_ui_center(s->hint);
    s->index = app_beep_enabled() ? 0U : (unsigned)MUTE_ITEM; /* a muted device starts on the way to turn it back on */
}

static void toast(app_context_t *ctx, const char *text)
{
    state_t *s = ctx->state;
    snprintf(s->toast, sizeof s->toast, "%s", text);
    s->toast_until = ctx->now_ms + TOAST_MS;
}

/* Starts the current entry. */
static void play(app_context_t *ctx, state_t *s)
{
    const item_t *item = &items[s->index];
    if (item->kind == K_MUTE) {
        const bool now_on = !app_beep_enabled();
        if (!app_beep_enable(now_on)) toast(ctx, now_on ? "已开启 未保存" : "已静音 未保存");
        else toast(ctx, now_on ? "声音已开启" : "已静音");
        if (now_on) (void)app_beep(APP_BEEP_CONFIRM);
        return;
    }
    if (!app_beep_enabled()) { toast(ctx, "已静音 先开声音"); return; }
    switch (item->kind) {
    case K_SWEEP:
        s->sweep_hz = SWEEP_FROM_HZ;
        s->sweep_next_ms = ctx->now_ms;
        return;
    case K_TONE:
        if (!app_beep_tone(item->hz, TONE_MS)) { toast(ctx, "稍等再试"); return; }
        s->play_until = ctx->now_ms + TONE_MS;
        return;
    default:
        if (!app_beep(item->sound)) { toast(ctx, "稍等再试"); return; }
        s->play_until = ctx->now_ms + app_sound_length_ms(item->sound);
        return;
    }
}

static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->kind != APP_EVENT_SHORT && e->kind != APP_EVENT_CONFIRM) return;
    s->toast_until = 0; /* the last message is about the last press */
    if (s->sweep_hz) { /* any key stops a sweep, and is spent on that */
        s->sweep_hz = 0;
        toast(ctx, "扫频已停");
        return;
    }
    if (e->kind == APP_EVENT_SHORT) s->index = (s->index + 1U) % (unsigned)ITEMS;
    else play(ctx, s);
}

static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->sweep_hz || ctx->now_ms < s->sweep_next_ms) return;
    if (!app_beep_enabled()) { s->sweep_hz = 0; return; } /* muted while it ran */
    if (!app_beep_tone(s->sweep_hz, SWEEP_NOTE_MS)) { s->sweep_next_ms = ctx->now_ms + 20U; return; } /* queue full: again soon */
    s->sweep_next_ms = ctx->now_ms + SWEEP_GAP_MS;
    s->sweep_hz += SWEEP_STEP_HZ;
    if (s->sweep_hz > SWEEP_TO_HZ) {
        s->sweep_hz = 0;
        s->play_until = ctx->now_ms + SWEEP_NOTE_MS;
        toast(ctx, "扫频结束");
    }
}

static void show(lv_obj_t *label, char *shown, const char *text)
{
    if (strcmp(shown, text)) { snprintf(shown, 40, "%s", text); app_ui_text(label, text); }
}

static void describe(const item_t *item, char *name, size_t name_size, char *desc, size_t desc_size)
{
    switch (item->kind) {
    case K_MUTE:
        snprintf(name, name_size, "声音 %s", app_beep_enabled() ? "开" : "关");
        snprintf(desc, desc_size, "长按 切换静音");
        break;
    case K_SWEEP:
        snprintf(name, name_size, "扫频");
        snprintf(desc, desc_size, "%u-%uHz 约%u秒", SWEEP_FROM_HZ, SWEEP_TO_HZ,
                 (SWEEP_TO_HZ - SWEEP_FROM_HZ) / SWEEP_STEP_HZ * SWEEP_GAP_MS / 1000U + 1U);
        break;
    case K_TONE:
        snprintf(name, name_size, "音调 %uHz", item->hz);
        snprintf(desc, desc_size, "单音 %ums", TONE_MS);
        break;
    default: {
        const app_tone_t *steps;
        const size_t count = app_sound_pattern(item->sound, &steps);
        snprintf(name, name_size, "%s", item->name);
        unsigned notes = 0;
        for (size_t i = 0; i < count; ++i) notes += steps[i].hz != 0U;
        if (count == 1U) snprintf(desc, desc_size, "%uHz %ums", (unsigned)steps[0].hz, (unsigned)steps[0].ms);
        else snprintf(desc, desc_size, "%u 声 %ums", notes, app_sound_length_ms(item->sound));
        break;
    }
    }
}

static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    app_ui_hold_line(s->hold, app_hold_ms(ctx), true, 0);
    char name[40], desc[40], hint[40], mute[24];
    describe(&items[s->index], name, sizeof name, desc, sizeof desc);
    if (s->sweep_hz) {
        snprintf(name, sizeof name, "扫频 %uHz", s->sweep_hz);
        snprintf(desc, sizeof desc, "按任意键停止");
    }
    if (ctx->now_ms < s->toast_until) snprintf(hint, sizeof hint, "%s", s->toast);
    else if (s->sweep_hz || ctx->now_ms < s->play_until) snprintf(hint, sizeof hint, "播放中");
    else snprintf(hint, sizeof hint, "短按 %u/%u 长按 播放", s->index + 1U, (unsigned)ITEMS);
    snprintf(mute, sizeof mute, "%s", app_beep_enabled() ? "声音 开" : "已静音");
    show(s->mute, s->shown[0], mute);
    show(s->name, s->shown[1], name);
    show(s->desc, s->shown[2], desc);
    show(s->hint, s->shown[3], hint);
}

static void icon(void *parent, int x, int y, bool selected)
{
    const uint32_t body = selected ? 0xEDF4FB : 0x8A8196;
    app_ui_rect(parent, x + 3, y + 10, 6, 8, body, 1);   /* the magnet */
    app_ui_rect(parent, x + 8, y + 6, 5, 16, body, 1);   /* the cone */
    app_ui_rect(parent, x + 16, y + 9, 2, 10, 0xFDB927, 1); /* sound waves */
    app_ui_rect(parent, x + 20, y + 5, 2, 18, 0xFDB927, 1);
}

const app_descriptor_t esphook_app_buzztest = {.api_version = APP_API_VERSION, .id = "buzztest", .title = "蜂鸣测试",
    .refresh_ms = 50, .state_size = sizeof(state_t), .on_enter = enter, .on_event = event, .on_tick = tick,
    .render = render, .icon = icon};
