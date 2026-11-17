#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_haptic.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include "esphook/app_command.h"
#include "esphook/app_sync.h"
#include "alarm_core.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 闹钟 / 整点报时: a seven-segment clock that also rings. The scheduling is the framework's (app_alarm.h): this app
 * registers its alarm and its hourly chime as two entries, and the framework opens it over whatever is on screen (an
 * alarm) or over the standby screen (a chime) and tells it with APP_EVENT_ALARM. All it does then is show the ring and
 * answer it (snooze, done). The same ring screen shows alarms that other apps registered with this one as the target.
 * Settings are one key: short press = next, 500 ms hold = change. The servo shakes to remind only when the player
 * switched that on, only for this app's own entries, and only through the public servo service. */
#define BAND_Y 64
#define KEY "alarm"
#define ROWS 7U
#define VISIBLE 4U
#define ROW_H 15
#define HINT_SLOT_MS 2400U
#define TOAST_MS 2200U
#define LIT 0xFFB020
#define DIM 0x2A1D0C
#define BG_TOP 0x0C1630
#define BG_BOT 0x04060E
#define DASH 10U
#define BLANK 11U

typedef enum { V_CLOCK, V_MENU, V_EDIT, V_RING } view_t;
enum { ROW_ALARM, ROW_TIME, ROW_REPEAT, ROW_CHIME, ROW_RANGE, ROW_SERVO, ROW_BACK };
static const char *const ROW_NAMES[ROWS] = {"闹钟", "闹钟时间", "重复", "整点报时", "报时时段", "舵机提醒", "返回时钟"};

typedef struct {
    haptic_t haptic;
    lv_obj_t *plot, *band, *hold, *date, *atext, *big, *sub, *hint, *sel, *name[VISIBLE], *value[VISIBLE];
    uint64_t ring_ms, next_ms, beat_ms, toast_until;
    uint32_t drawn_key, text_key;
    unsigned ring_id;                                /* the registry entry being answered */
    uint8_t view, kind, sel_row, top_row, field, hour, m10, m1, beat, armed, snoozable;
    char toast[28], label[APP_ALARM_LABEL_MAX + 1];
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "alarm state too large");
typedef struct { bool active; uint8_t view, kind; uint32_t fired, chimes, frames; } status_t;
static status_t s_status;
/* The settings and the registry ids belong to the UI task alone (init, tick and events all run there). */
static al_config_t s_cfg;
static unsigned s_alarm_id, s_chime_id;

static uint16_t c565(uint32_t rgb) { return gfx_rgb565(rgb); }
/* Tells the framework what the settings now are. Saying the same thing again is harmless: a ring or snooze under way is
 * only ended by a real change. A refused entry (no free slot) leaves its id at 0. */
static void sync_alarms(void)
{
    app_alarm_spec_t spec;
    al_alarm_spec(&s_cfg, &spec);
    if (!s_alarm_id || !app_alarm_update(s_alarm_id, &spec)) s_alarm_id = app_alarm_add(&spec);
    al_chime_spec(&s_cfg, &spec);
    if (!s_chime_id || !app_alarm_update(s_chime_id, &spec)) s_chime_id = app_alarm_add(&spec);
}
static void save(void)
{
    (void)app_storage_save(KEY, &s_cfg, sizeof s_cfg);
    sync_alarms();
}
static bool init(void)
{
    al_config_t c;
    if (app_storage_load(KEY, &c, sizeof c) && al_config_valid(&c)) s_cfg = c;
    else al_defaults(&s_cfg);
    (void)app_alarm_cancel_owner("alarm"); /* the registry is RAM: start from a clean slate, also when init runs again */
    s_alarm_id = s_chime_id = 0;
    sync_alarms();
    return true;
}

/* ---- seven segments -------------------------------------------------------------------------------------------- */
typedef struct { int w, h, t, gap; } led_t;
static const uint8_t SEGMENTS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F}; /* a b c d e f g = bits 0..6 */
static void hseg(uint16_t *px, int x0, int x1, int y, int t, uint16_t c, uint16_t hi)
{
    for (int i = 0; i < t; ++i) gfx_hline(px, x0 + (i == 0 || i == t - 1), x1 - (i == 0 || i == t - 1), y + i, i == 1 ? hi : c);
}
static void vseg(uint16_t *px, int x, int y0, int y1, int t, uint16_t c, uint16_t hi)
{
    for (int i = 0; i < t; ++i) gfx_vline(px, x + i, y0 + (i == 0 || i == t - 1), y1 - (i == 0 || i == t - 1), i == 1 ? hi : c);
}
static void led_digit(uint16_t *px, int x, int y, const led_t *g, unsigned digit)
{
    const unsigned mask = digit < 10U ? SEGMENTS[digit] : digit == DASH ? 0x40U : 0U;
    const int w = g->w, h = g->h, t = g->t, gt = (h - t) / 2;
    const uint16_t lit = c565(LIT), hi = c565(gfx_mix(LIT, 0xFFFFFF, 110)), dim = c565(DIM);
    for (unsigned seg = 0; seg < 7U; ++seg) {
        const bool on = (mask >> seg) & 1U;
        const uint16_t c = on ? lit : dim, hc = on ? hi : dim;
        switch (seg) {
        case 0: hseg(px, x + 3, x + w - 4, y, t, c, hc); break;
        case 3: hseg(px, x + 3, x + w - 4, y + h - t, t, c, hc); break;
        case 6: hseg(px, x + 3, x + w - 4, y + gt, t, c, hc); break;
        case 5: vseg(px, x, y + t + 1, y + gt - 2, t, c, hc); break;
        case 1: vseg(px, x + w - t, y + t + 1, y + gt - 2, t, c, hc); break;
        case 4: vseg(px, x, y + gt + t + 1, y + h - t - 2, t, c, hc); break;
        default: vseg(px, x + w - t, y + gt + t + 1, y + h - t - 2, t, c, hc); break;
        }
    }
}
static int led_width(const led_t *g) { return 4 * g->w + 2 * g->gap + 2 * (g->gap + 1) + g->t; }
static void led_time(uint16_t *px, int x, int y, const led_t *g, const unsigned d[4], bool colon)
{
    led_digit(px, x, y, g, d[0]);
    x += g->w + g->gap;
    led_digit(px, x, y, g, d[1]);
    x += g->w + g->gap + 1;
    const uint16_t c = c565(colon ? LIT : DIM);
    const int dot = g->t;
    gfx_rect(px, x, y + g->h / 3 - dot / 2, dot, dot, c);
    gfx_rect(px, x, y + g->h * 2 / 3 - dot / 2, dot, dot, c);
    if (colon) {
        gfx_tint(px, x - 1, y + g->h / 3 - dot / 2 - 1, dot + 2, dot + 2, LIT, 40);
        gfx_tint(px, x - 1, y + g->h * 2 / 3 - dot / 2 - 1, dot + 2, dot + 2, LIT, 40);
    }
    x += dot + g->gap + 1;
    led_digit(px, x, y, g, d[2]);
    x += g->w + g->gap;
    led_digit(px, x, y, g, d[3]);
}
static void digits_of(const app_time_t *t, unsigned d[4])
{
    if (!t->valid) { d[0] = d[1] = d[2] = d[3] = DASH; return; }
    d[0] = (unsigned)t->hour / 10U; d[1] = (unsigned)t->hour % 10U;
    d[2] = (unsigned)t->minute / 10U; d[3] = (unsigned)t->minute % 10U;
}

static void draw_glow(uint16_t *px, int cy, int ry) /* a soft warm halo behind the digits: nested ellipses, no hard edge */
{
    for (int k = 0; k < 4; ++k) gfx_tint_ellipse(px, 80, cy, 76 - k * 9, ry - k * 3, 0xFF9A1A, 5);
}
/* ---- backdrop, the bell ---------------------------------------------------------------------------------------- */
static void draw_backdrop(uint16_t *px, unsigned flash)
{
    gfx_gradient(px, 0, APP_CANVAS_H, BG_TOP, BG_BOT);
    for (int y = 3; y < APP_CANVAS_H; y += 8) gfx_tint(px, 0, y, APP_CANVAS_W, 1, 0x6C8CFF, 8); /* faint scan lines */
    if (flash) gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0xFF7A1A, flash);
}
static void draw_bell_icon(uint16_t *px, int x, int y, bool on)
{ /* 9 x 9 */
    const uint16_t c = c565(on ? LIT : 0x4A4F5E);
    gfx_rect(px, x + 4, y, 1, 1, c);
    gfx_rect(px, x + 3, y + 1, 3, 1, c);
    gfx_rect(px, x + 2, y + 2, 5, 3, c);
    gfx_rect(px, x + 1, y + 5, 7, 1, c);
    gfx_rect(px, x, y + 6, 9, 1, c);
    gfx_rect(px, x + 4, y + 8, 1, 1, c);
    if (!on) gfx_line(px, x, y + 8, x + 8, y, c565(0xD94A4A));
}
static const uint8_t BELL[34] = {2, 2, 3, 3, 3, 4, 4, 6, 8, 9, 9, 10, 11, 11, 12, 12, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 15, 15, 16, 17, 17, 18, 18, 18};
/* The bell hangs from (cx, top) and swings by `swing` pixels at its lip (-256..256 is +-9 px). */
static void draw_bell(uint16_t *px, int cx, int top, int swing256, unsigned glow)
{
    const int swing = swing256 * 9 / 256;
    gfx_tint_ellipse(px, cx, top + 20, 26, 22, 0xFFA020, 10 + glow / 4U);
    gfx_line(px, cx, 0, cx + swing / 12, top + 1, c565(0x7A7F90)); /* the hook it hangs from */
    for (int r = 0; r < 34; ++r) {
        const int off = swing * r / 33, half = BELL[r], x0 = cx - half + off, x1 = cx + half + off, y = top + r;
        const uint32_t body = r >= 31 ? 0xE0A020 : 0xF2B233;
        gfx_hline(px, x0, x1, y, c565(body));
        if (half > 4) {
            gfx_tint(px, x0 + 1, y, 3, 1, 0xFFF0B0, 120);
            gfx_tint(px, x1 - 4, y, 4, 1, 0x5A3200, 110);
        }
        if (r == 30 || r == 33) gfx_hline(px, x0, x1, y, c565(r == 33 ? 0xA86E10 : 0xFFE08A));
    }
    const int clap = swing * 3 / 5; /* the clapper follows a little behind */
    gfx_disc(px, cx + clap, top + 37, 3, c565(0xC98A18));
    gfx_tint(px, cx + clap - 1, top + 36, 2, 1, 0xFFF0B0, 140);
    if (glow) gfx_tint_ellipse(px, cx, top + 17, 14, 14, 0xFFF0B0, glow / 5U);
}
static void draw_waves(uint16_t *px, int cx, int cy, unsigned now, unsigned strength)
{
    for (unsigned k = 0; k < 3U; ++k) {
        const int r = 22 + (int)((now / 36U + k * 8U) % 24U);
        const unsigned alpha = (unsigned)(190 - (r - 22) * 7) * strength / 256U;
        for (int dy = -r * 3 / 5; dy <= r * 3 / 5; ++dy) {
            const int dx = (int)gfx_isqrt((uint32_t)(r * r - dy * dy));
            gfx_tint_plot(px, cx + dx, cy + dy, 0xFFC040, alpha);
            gfx_tint_plot(px, cx - dx, cy + dy, 0xFFC040, alpha);
            gfx_tint_plot(px, cx + dx + 1, cy + dy, 0xFFC040, alpha / 2U);
            gfx_tint_plot(px, cx - dx - 1, cy + dy, 0xFFC040, alpha / 2U);
        }
    }
}

/* ---- views ----------------------------------------------------------------------------------------------------- */
static void draw_clock(uint16_t *px, const app_time_t *t, bool blink)
{
    draw_backdrop(px, 0);
    const led_t g = {22, 36, 4, 4};
    const int x = (APP_CANVAS_W - led_width(&g)) / 2, y = 15;
    draw_glow(px, y + g.h / 2, 28);
    unsigned d[4];
    digits_of(t, d);
    led_time(px, x, y, &g, d, t->valid ? t->second % 2 == 0 : blink);
    const int bar_x = x, bar_w = led_width(&g), bar_y = 56; /* the seconds */
    gfx_rect(px, bar_x, bar_y, bar_w, 2, c565(DIM));
    if (t->valid) {
        const int fill = (t->second * bar_w + bar_w / 2) / 59;
        gfx_rect(px, bar_x, bar_y, fill < 1 ? 1 : fill, 2, c565(0xE08A10));
        gfx_tint(px, bar_x, bar_y, fill, 1, 0xFFFFFF, 60);
    }
    draw_bell_icon(px, 101, 3, s_cfg.alarm_on);
}
static void draw_menu(uint16_t *px, unsigned top, unsigned sel)
{
    (void)sel;
    draw_backdrop(px, 0);
    gfx_rect(px, 156, 2, 2, 60, c565(0x1A2440)); /* the scroll bar */
    const int thumb = (int)(60U * VISIBLE / ROWS);
    gfx_rect(px, 156, 2 + (int)(top * (60U - (unsigned)thumb) / (ROWS - VISIBLE)), 2, thumb, c565(0xE08A10));
}
static void draw_edit(uint16_t *px, const state_t *s, uint64_t now)
{
    draw_backdrop(px, 0);
    const led_t g = {22, 34, 4, 4};
    const int x = (APP_CANVAS_W - led_width(&g)) / 2, y = 18;
    draw_glow(px, y + g.h / 2, 26);
    const bool on = ((now / 330U) & 1U) == 0U;
    unsigned d[4] = {s->hour / 10U, s->hour % 10U, s->m10, s->m1};
    if (!on) { if (s->field == 0) d[0] = d[1] = BLANK; else d[1 + s->field] = BLANK; }
    led_time(px, x, y, &g, d, true);
    const int left = s->field == 0 ? x : x + g.w * 2 + g.gap + 2 * (g.gap + 1) + g.t + (s->field == 2 ? g.w + g.gap : 0);
    const int len = s->field == 0 ? g.w * 2 + g.gap : g.w;
    gfx_rect(px, left, y + g.h + 3, len, 2, c565(0xE08A10)); /* underline of the field being set */
}
static void draw_ring(uint16_t *px, const state_t *s, const app_time_t *t, uint64_t now)
{
    const unsigned since = (unsigned)(now - s->ring_ms);
    unsigned flash = 0U, glow = 0U;
    int swing;
    if (s->kind == AL_ALARM) {
        swing = gfx_sin256(since * 5U / 7U);
        flash = (since / 350U) % 2U ? 22U : 0U;
        glow = 120U + (unsigned)(gfx_sin256(since * 5U / 7U + 64U) + 256) / 4U;
    } else {
        const unsigned age = s->beat ? (unsigned)(now - s->beat_ms) : 9999U;
        const unsigned amp = age < 650U ? 256U - age * 256U / 650U : 0U;
        swing = gfx_sin256(age * 10U / 11U) * (int)amp / 256;
        glow = age < 220U ? 220U - age : 0U;
    }
    draw_backdrop(px, flash);
    draw_waves(px, 34, 30, since, s->kind == AL_ALARM ? 256U : (glow ? 200U : 0U));
    draw_bell(px, 34, 8, swing, glow);
    const led_t g = {14, 24, 3, 3};
    const int x = 62 + (96 - led_width(&g)) / 2, y = 20;
    unsigned d[4];
    digits_of(t, d);
    led_time(px, x, y, &g, d, t->valid ? t->second % 2 == 0 : true);
    if (s->kind == AL_CHIME) {
        const unsigned beats = al_beats((unsigned)t->hour);
        for (unsigned i = 0; i < beats; ++i) { /* one dot per stroke, the ones already struck lit */
            const int dx = 62 + (96 - (int)beats * 7) / 2 + (int)i * 7, dy = 52;
            gfx_rect(px, dx, dy, 5, 5, c565(i < s->beat ? LIT : DIM));
            if (i + 1U == s->beat) gfx_tint(px, dx - 1, dy - 1, 7, 7, 0xFFE08A, 90);
        }
    } else {
        const unsigned phase = (since / 120U) % 8U;
        for (unsigned i = 0; i < 8U; ++i) gfx_rect(px, 66 + (int)i * 11, 54, 8, 2, c565(i == phase ? 0xFFE08A : DIM)); /* a chaser */
    }
}

/* ---- app ------------------------------------------------------------------------------------------------------- */
static void build(state_t *s, lv_obj_t *root)
{
    s->plot = app_ui_canvas(root, 0, 0, APP_CANVAS_W, APP_CANVAS_H);
    s->sel = app_ui_rect(root, 3, 2, 150, ROW_H, 0x24345E, 4);
    for (unsigned i = 0; i < VISIBLE; ++i) {
        s->name[i] = app_ui_label(root, &aihook_font_12, 9, 2 + (int)i * ROW_H + 1, 78, 14, LV_LABEL_LONG_MODE_CLIP);
        s->value[i] = app_ui_label(root, &aihook_font_12, 84, 2 + (int)i * ROW_H + 1, 66, 14, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(s->value[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_color(s->name[i], app_ui_color(0xDCE6FF), LV_PART_MAIN);
        lv_obj_set_style_text_color(s->value[i], app_ui_color(0xFFC860), LV_PART_MAIN);
    }
    s->date = app_ui_label(root, &aihook_font_12, 4, 0, 96, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->date, app_ui_color(0xA8BCEB), LV_PART_MAIN);
    s->atext = app_ui_label(root, &aihook_font_12, 113, 0, 46, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->atext, app_ui_color(0xFFC860), LV_PART_MAIN);
    s->big = app_ui_label(root, &aihook_font_16, 62, 1, 96, 18, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->big);
    lv_obj_set_style_text_color(s->big, app_ui_color(0xFFD060), LV_PART_MAIN);
    s->sub = app_ui_label(root, &aihook_font_12, 0, 2, APP_WIDTH, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->sub);
    lv_obj_set_style_text_color(s->sub, app_ui_color(0xA8BCEB), LV_PART_MAIN);
    s->band = app_ui_band(root, 0x070B18, 0x3A4A7A);
    s->hint = app_ui_label(s->band, &aihook_font_12, 0, 2, APP_WIDTH, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->hint);
    lv_obj_set_style_text_color(s->hint, app_ui_color(0xC8D4F2), LV_PART_MAIN);
    s->hold = app_ui_hold_bar(root, BAND_Y, APP_COLOR_ACCENT);
    app_ui_hide(s->hold, true);
    s->drawn_key = s->text_key = 0xFFFFFFFFU;
}
static void publish(const state_t *s, bool active, bool frame, int fired, int chimes)
{
    app_critical_enter();
    s_status.active = active; s_status.view = s->view; s_status.kind = s->view == V_RING ? s->kind : AL_NONE;
    s_status.fired += (uint32_t)fired; s_status.chimes += (uint32_t)chimes;
    if (frame) ++s_status.frames;
    app_critical_exit();
}
static void toast(state_t *s, uint64_t now, const char *text)
{
    snprintf(s->toast, sizeof s->toast, "%s", text);
    s->toast_until = now + TOAST_MS;
    s->text_key = 0xFFFFFFFFU;
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    haptic_init(&s->haptic);
    build(s, ctx->root);
    s->view = V_CLOCK;
    app_critical_enter(); s_status.active = true; app_critical_exit(); /* the counters live on across entries */
    publish(s, true, false, 0, 0);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->plot) app_ui_canvas_refresh(s->plot); /* the shared canvas goes to the next app */
    app_critical_enter(); s_status.active = false; s_status.view = V_CLOCK; s_status.kind = AL_NONE; app_critical_exit();
}
/* APP_EVENT_ALARM: the entry `id` is ringing and this app is on screen. */
static void start_ring(app_context_t *ctx, state_t *s, unsigned id)
{
    app_alarm_info_t info;
    if (!app_alarm_get(id, &info) || info.state != APP_ALARM_RINGING) return;
    const al_kind_t own = al_kind_of(&info);
    if (own == AL_ALARM && !info.enabled && s_cfg.alarm_on) { s_cfg.alarm_on = 0; save(); } /* a once-alarm: the framework switched it off */
    if (own == AL_ALARM && info.minute == 0U && s_chime_id) (void)app_alarm_skip(s_chime_id); /* the hour is the alarm's: no chime on top */
    s->ring_id = id;
    s->kind = own == AL_CHIME ? AL_CHIME : AL_ALARM;
    s->snoozable = info.max_snoozes > 0U;
    snprintf(s->label, sizeof s->label, "%s", info.label[0] ? info.label : "闹钟");
    s->view = V_RING; s->ring_ms = ctx->now_ms; s->beat = 0; s->beat_ms = 0;
    s->next_ms = ctx->now_ms + (own == AL_CHIME ? 350U : 0U);
    s->drawn_key = s->text_key = 0xFFFFFFFFU;
    app_set_exit_hold(ctx, 0);
    haptic_init(&s->haptic);
    /* The player switched the servo reminder on in this app's settings: that is the confirmation to arm it, for this
     * app's own entries. Another app's alarm does not get to move the servo through this one. */
    s->armed = own != AL_NONE && s_cfg.servo_on && haptic_state() == HAPTIC_READY && haptic_arm(ctx, &s->haptic);
    publish(s, true, false, s->kind == AL_ALARM, s->kind == AL_CHIME);
}
static void finish_ring(app_context_t *ctx, bool snooze)
{
    state_t *s = ctx->state;
    if (!(snooze && s->snoozable && app_alarm_snooze(s->ring_id))) app_alarm_done(s->ring_id); /* out of snoozes: over */
    s->view = V_CLOCK;
    app_idle(ctx);
}
static unsigned chime_ms(unsigned beats) { const unsigned ms = 2400U + beats * AL_BEAT_MS; return ms < 4000U ? 4000U : ms; }
static void tick_ring(app_context_t *ctx, state_t *s, const app_time_t *t)
{
    const uint64_t now = ctx->now_ms;
    app_servo_t sv;
    app_servo_read(&sv);
    if (s->kind == AL_ALARM) {
        if (now - s->ring_ms >= AL_RING_MS) { finish_ring(ctx, true); return; } /* nobody answered: try again in five minutes */
        if (now >= s->next_ms) {
            if (s->armed) haptic_shake(&s->haptic, now, 10, 150, 1800, sv.speed_dps); /* a slow rocking the servo can follow */
            s->next_ms = now + 2200U;
        }
    } else {
        const unsigned beats = al_beats((unsigned)t->hour);
        while (s->beat < beats && now >= s->next_ms) {
            ++s->beat; s->beat_ms = now;
            if (s->armed) haptic_shake(&s->haptic, now, 6, 110, 600, sv.speed_dps); /* one nod per stroke */
            s->next_ms += AL_BEAT_MS;
        }
        if (now - s->ring_ms >= chime_ms(beats)) { finish_ring(ctx, false); return; }
    }
    haptic_pump(ctx, &s->haptic);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->view != V_RING) return;
    app_time_t t;
    app_time_read(&t);
    tick_ring(ctx, s, &t);
}
static void begin_edit(state_t *s)
{
    s->view = V_EDIT; s->field = 0;
    s->hour = s_cfg.hour; s->m10 = s_cfg.minute / 10U; s->m1 = s_cfg.minute % 10U;
    s->drawn_key = s->text_key = 0xFFFFFFFFU;
}
static void menu_action(state_t *s, uint64_t now)
{
    char text[28];
    switch (s->sel_row) {
    case ROW_ALARM:
        s_cfg.alarm_on = !s_cfg.alarm_on;
        save();
        break;
    case ROW_TIME: begin_edit(s); return;
    case ROW_REPEAT: s_cfg.repeat = (uint8_t)((s_cfg.repeat + 1U) % AL_REPEATS); save(); break;
    case ROW_CHIME: s_cfg.chime_on = !s_cfg.chime_on; save(); break;
    case ROW_RANGE: s_cfg.range = (uint8_t)((s_cfg.range + 1U) % AL_RANGES); save(); break;
    case ROW_SERVO:
        if (haptic_state() == HAPTIC_NONE) { toast(s, now, "没有检测到舵机"); return; }
        s_cfg.servo_on = !s_cfg.servo_on;
        save();
        if (s_cfg.servo_on) toast(s, now, "到点舵机会摇晃提醒");
        break;
    default:
        s->view = V_CLOCK;
        break;
    }
    if (s->sel_row == ROW_ALARM) {
        app_time_t t;
        app_time_read(&t);
        snprintf(text, sizeof text, "%s", !s_cfg.alarm_on ? "闹钟已关闭" : !s_alarm_id ? "闹钟登记失败" : t.valid ? "闹钟已开启" : "已开启 但还没校时");
        toast(s, now, text);
    }
    s->drawn_key = s->text_key = 0xFFFFFFFFU;
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->kind == APP_EVENT_ALARM) { start_ring(ctx, s, e->key); return; } /* key is the registry id here, not a key number */
    if (e->key != 0 || (e->kind != APP_EVENT_SHORT && e->kind != APP_EVENT_CONFIRM)) return;
    const uint64_t now = ctx->now_ms;
    const bool confirm = e->kind == APP_EVENT_CONFIRM;
    s->drawn_key = s->text_key = 0xFFFFFFFFU;
    switch (s->view) {
    case V_RING:
        if (s->kind == AL_ALARM) finish_ring(ctx, !confirm); /* a short press snoozes, holding switches it off */
        else finish_ring(ctx, false);
        break;
    case V_CLOCK:
        if (confirm) {
            s_cfg.alarm_on = !s_cfg.alarm_on;
            save();
            char text[28];
            if (s_cfg.alarm_on && !s_alarm_id) snprintf(text, sizeof text, "闹钟登记失败");
            else if (s_cfg.alarm_on) snprintf(text, sizeof text, "闹钟已开启 %02u:%02u", s_cfg.hour, s_cfg.minute);
            else snprintf(text, sizeof text, "闹钟已关闭");
            toast(s, now, text);
        } else { s->view = V_MENU; s->sel_row = 0; s->top_row = 0; }
        break;
    case V_MENU:
        if (confirm) menu_action(s, now);
        else {
            s->sel_row = (uint8_t)((s->sel_row + 1U) % ROWS);
            if (s->sel_row < s->top_row) s->top_row = s->sel_row;
            if (s->sel_row >= s->top_row + VISIBLE) s->top_row = (uint8_t)(s->sel_row + 1U - VISIBLE);
        }
        break;
    default: /* V_EDIT */
        if (!confirm) {
            if (s->field == 0) s->hour = (uint8_t)((s->hour + 1U) % 24U);
            else if (s->field == 1) s->m10 = (uint8_t)((s->m10 + 1U) % 6U);
            else s->m1 = (uint8_t)((s->m1 + 1U) % 10U);
        } else if (s->field < 2U) ++s->field;
        else {
            s_cfg.hour = s->hour; s_cfg.minute = (uint8_t)(s->m10 * 10U + s->m1); s_cfg.alarm_on = 1;
            save();
            char text[28];
            snprintf(text, sizeof text, "闹钟 %02u:%02u 已开启", s_cfg.hour, s_cfg.minute);
            toast(s, now, text);
            s->view = V_MENU;
        }
        break;
    }
    publish(s, true, false, 0, 0);
}

static uint32_t mix(uint32_t k, uint32_t v) { return gfx_hash(k * 31U + v + 1U); }
static void sync_clock_text(state_t *s, const app_time_t *t, uint64_t now)
{
    const unsigned slot = (unsigned)((now / HINT_SLOT_MS) % 3U);
    const bool toasting = now < s->toast_until;
    const int32_t secs = s_alarm_id ? app_alarm_seconds_until(s_alarm_id, t, now) : -1;
    const int until = secs > 0 ? (int)((secs + 59) / 60) : -1; /* minutes to the next ring (a snooze counts), -1 when off */
    uint32_t key = mix(mix(mix(mix((uint32_t)(t->valid ? t->day + t->month * 40 : 0), slot), (uint32_t)toasting), (uint32_t)(until + 1)),
                       (uint32_t)t->valid);
    key = mix(mix(mix(mix(key, s_cfg.alarm_on), s_cfg.chime_on), (uint32_t)s_cfg.repeat << 8 | s_cfg.range), (uint32_t)s_cfg.hour << 8 | s_cfg.minute);
    if (key == s->text_key) return;
    s->text_key = key;
    char text[48];
    if (t->valid) {
        static const char *const week = "日一二三四五六";
        snprintf(text, sizeof text, "%d月%d日 周%.3s", t->month, t->day, week + (size_t)t->weekday * 3U);
    } else snprintf(text, sizeof text, "未校时");
    app_ui_text(s->date, text);
    snprintf(text, sizeof text, "%02u:%02u", s_cfg.hour, s_cfg.minute);
    app_ui_text(s->atext, s_cfg.alarm_on ? text : "");
    app_ui_hide(s->atext, !s_cfg.alarm_on);
    lv_obj_set_style_text_color(s->date, app_ui_color(t->valid ? 0xA8BCEB : 0xFF8A70), LV_PART_MAIN);
    if (toasting) { app_ui_text(s->hint, s->toast); return; }
    if (!t->valid) { app_ui_text(s->hint, slot == 1U ? "或运行 run.py time 校时" : "联网后自动校时"); return; }
    if (slot == 0U) app_ui_text(s->hint, "短按设置 长按开关闹钟");
    else if (slot == 1U) {
        if (until < 0) app_ui_text(s->hint, "闹钟已关闭");
        else if (until >= 60) { snprintf(text, sizeof text, "%s 还有%d时%d分", al_repeat_name(s_cfg.repeat), until / 60, until % 60); app_ui_text(s->hint, text); }
        else { snprintf(text, sizeof text, "%s 还有%d分", al_repeat_name(s_cfg.repeat), until); app_ui_text(s->hint, text); }
    } else {
        snprintf(text, sizeof text, "整点报时 %s", s_cfg.chime_on ? al_range_name(s_cfg.range) : "已关闭");
        app_ui_text(s->hint, text);
    }
}
static void sync_menu_text(state_t *s, uint64_t now)
{
    const bool toasting = now < s->toast_until;
    uint32_t key = mix(mix(mix((uint32_t)s->sel_row << 8 | s->top_row, (uint32_t)toasting), (uint32_t)s_cfg.alarm_on | (uint32_t)s_cfg.chime_on << 1 |
                       (uint32_t)s_cfg.servo_on << 2 | (uint32_t)s_cfg.repeat << 4 | (uint32_t)s_cfg.range << 8), (uint32_t)s_cfg.hour << 8 | s_cfg.minute);
    key = mix(key, (uint32_t)haptic_state());
    if (key == s->text_key) return;
    s->text_key = key;
    for (unsigned i = 0; i < VISIBLE; ++i) {
        const unsigned row = s->top_row + i;
        char value[24] = "";
        switch (row) {
        case ROW_ALARM: snprintf(value, sizeof value, "%s", s_cfg.alarm_on ? "开" : "关"); break;
        case ROW_TIME: snprintf(value, sizeof value, "%02u:%02u", s_cfg.hour, s_cfg.minute); break;
        case ROW_REPEAT: snprintf(value, sizeof value, "%s", al_repeat_name(s_cfg.repeat)); break;
        case ROW_CHIME: snprintf(value, sizeof value, "%s", s_cfg.chime_on ? "开" : "关"); break;
        case ROW_RANGE: snprintf(value, sizeof value, "%s", al_range_name(s_cfg.range)); break;
        case ROW_SERVO: snprintf(value, sizeof value, "%s", haptic_state() == HAPTIC_NONE ? "无舵机" : s_cfg.servo_on ? "开" : "关"); break;
        default: break;
        }
        app_ui_text(s->name[i], ROW_NAMES[row]);
        app_ui_text(s->value[i], value);
    }
    lv_obj_set_y(s->sel, 2 + (int)(s->sel_row - s->top_row) * ROW_H);
    app_ui_text(s->hint, toasting ? s->toast : s->sel_row == ROW_BACK ? "长按回到时钟" : "短按下一项 长按修改");
}
static void sync_views(state_t *s, const app_time_t *t, uint64_t now)
{
    const bool clock = s->view == V_CLOCK, menu = s->view == V_MENU, edit = s->view == V_EDIT, ring = s->view == V_RING;
    app_ui_hide(s->date, !clock); app_ui_hide(s->atext, !clock);
    app_ui_hide(s->sel, !menu);
    for (unsigned i = 0; i < VISIBLE; ++i) { app_ui_hide(s->name[i], !menu); app_ui_hide(s->value[i], !menu); }
    app_ui_hide(s->sub, !edit); app_ui_hide(s->big, !ring);
    char text[48];
    if (clock) sync_clock_text(s, t, now);
    else if (menu) sync_menu_text(s, now);
    else if (edit) {
        static const char *const fields[3] = {"设置闹钟 小时", "设置闹钟 分钟十位", "设置闹钟 分钟个位"};
        app_ui_text(s->sub, fields[s->field]);
        app_ui_text(s->hint, s->field < 2U ? "短按加一 长按下一步" : "短按加一 长按保存");
    } else {
        if (s->text_key != 0x77U) {
            s->text_key = 0x77U;
            app_ui_text(s->big, s->label);
            if (s->kind == AL_ALARM) app_ui_text(s->hint, s->snoozable ? "短按稍后提醒 长按关闭" : "按键关闭");
            else { snprintf(text, sizeof text, "%d点整 按键关闭", t->hour); app_ui_text(s->hint, text); }
        }
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t now = ctx->now_ms;
    app_time_t t;
    app_time_read(&t);
    sync_views(s, &t, now);
    uint32_t key;
    switch (s->view) {
    case V_CLOCK: key = (uint32_t)t.second | (uint32_t)t.minute << 6 | (uint32_t)t.valid << 12 | (uint32_t)s_cfg.alarm_on << 13 | (uint32_t)(t.valid ? 0U : now / 400U & 1U) << 14; break;
    case V_MENU: key = (uint32_t)s->sel_row << 8 | s->top_row | 0x10000U | (uint32_t)(now < s->toast_until) << 20; break;
    case V_EDIT: key = (uint32_t)(now / 330U) | 0x20000000U | (uint32_t)s->field << 24 | (uint32_t)s->hour << 16 | (uint32_t)s->m10 << 12 | (uint32_t)s->m1 << 8; break;
    default: key = 0xFFFFFFFFU - (uint32_t)(now / 20U); break; /* the ring moves on every frame */
    }
    if (key != s->drawn_key) {
        s->drawn_key = key;
        uint16_t *px = app_ui_pixels();
        if (s->view == V_CLOCK) draw_clock(px, &t, (now / 500U) % 2U == 0U);
        else if (s->view == V_MENU) draw_menu(px, s->top_row, s->sel_row);
        else if (s->view == V_EDIT) draw_edit(px, s, now);
        else draw_ring(px, s, &t, now);
        app_ui_canvas_refresh(s->plot);
    }
    app_ui_hold_line(s->hold, app_hold_ms(ctx), true, 0);
    publish(s, true, true, 0, 0);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot alarm status")) return false;
    status_t v;
    app_time_t t;
    app_shared_copy(v, s_status);
    app_time_read(&t);
    (void)app_command_replyf("active=%u view=%u ring=%u valid=%u alarm=%u time=%02u:%02u repeat=%u chime=%u range=%u servo=%u fired=%lu chimes=%lu frames=%lu aid=%u cid=%u",
            v.active ? 1U : 0U, v.view, v.kind, t.valid ? 1U : 0U, s_cfg.alarm_on, s_cfg.hour, s_cfg.minute, s_cfg.repeat,
            s_cfg.chime_on, s_cfg.range, s_cfg.servo_on, (unsigned long)v.fired, (unsigned long)v.chimes, (unsigned long)v.frames,
            s_alarm_id, s_chime_id);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    const uint32_t c = selected ? 0xFFB020 : 0xC8923A;
    app_ui_rect(parent, x + 3, y + 1, 9, 7, c, 4);                 /* the two bells on top */
    app_ui_rect(parent, x + 16, y + 1, 9, 7, c, 4);
    app_ui_rect(parent, x + 2, y + 4, 24, 22, c, 11);              /* the case */
    app_ui_rect(parent, x + 5, y + 7, 18, 16, 0x0C1630, 8);        /* the face */
    app_ui_rect(parent, x + 13, y + 10, 2, 7, 0xFFE08A, 0);        /* hands */
    app_ui_rect(parent, x + 13, y + 15, 6, 2, 0xFFE08A, 0);
    app_ui_rect(parent, x + 5, y + 25, 4, 3, c, 1);                /* feet */
    app_ui_rect(parent, x + 19, y + 25, 4, 3, c, 1);
}
const app_descriptor_t esphook_app_alarm = {.api_version = APP_API_VERSION, .id = "alarm", .title = "闹钟",
    .flags = APP_STANDBY, .refresh_ms = 50, .state_size = sizeof(state_t), .init = init, .on_enter = enter, .on_leave = leave,
    .on_event = event, .on_tick = tick, .render = render, .icon = icon, .command = command};
