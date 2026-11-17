#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include "muyu_core.h"
#include "freertos/FreeRTOS.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 电子木鱼: knock on the button (key down) and the mallet falls, the fish squashes, a ripple runs out and
 * "功德+1" floats up while the counter grows. A long press switches auto knocking on or off. The scene is the shared
 * 160x64 canvas, the bottom 16 px a hint strip; the HUD and the floating text are small LVGL labels. */
#define BAND_Y 64
#define FISH_CX 72
#define FISH_BOTTOM 55
#define FISH_RX 29
#define FISH_RY 17
#define STRIKE_X 95
#define STRIKE_Y 24
#define REST_X 123
#define REST_Y 27
#define FLOAT_MS 700U
#define FLOAT_RISE 14

typedef struct {
    muyu_t core;
    lv_obj_t *plot, *band, *line, *hold, *hint, *hud, *count, *tag, *tag_text, *floating[2];
    lv_image_dsc_t image;
    unsigned hud_width;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "muyu state too large");
typedef struct { bool active, auto_on; uint32_t count, rounds, frames; } status_t;
static status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static const uint32_t FISH_LIGHT = 0xF4B561, FISH_MID = 0xC9733A, FISH_DARK = 0x6A2B10, FISH_RIM = 0x32130A;
static const uint32_t GOLD = 0xFFD54A;

static int sin256(unsigned phase)
{
    static int16_t table[256];
    static bool ready; /* only the UI task draws */
    if (!ready) {
        for (int i = 0; i < 256; ++i) table[i] = (int16_t)lroundf(256.0f * sinf((float)i * 6.2831853f / 256.0f));
        ready = true;
    }
    return table[phase & 255U];
}
static int lerp(int a, int b, int t256) { return a + (b - a) * t256 / 256; }

/* ---- scene ------------------------------------------------------------------------------------------------- */
static void draw_backdrop(uint16_t *px, unsigned now, unsigned since_knock)
{
    gfx_gradient(px, 0, BAND_Y, 0x120F26, 0x2B1626);
    unsigned glow = since_knock < 420U ? (420U - since_knock) * 22U / 420U : 0U; /* every knock lights the halo */
    gfx_tint_ellipse(px, FISH_CX, 36, 66, 36, 0xFFC857, 8 + glow / 3U);
    gfx_tint_ellipse(px, FISH_CX, 36, 48, 28, 0xFFC857, 10 + glow / 2U);
    gfx_tint_ellipse(px, FISH_CX, 36, 32, 20, 0xFFC857, 10 + glow);
    for (unsigned i = 0; i < 12U; ++i) { /* golden motes drift upwards */
        uint32_t h = gfx_hash(i * 71U + 5U);
        unsigned speed = 5U + (h >> 8) % 9U, rise = (unsigned)(((uint64_t)now * speed / 1000U) + (h >> 14) % 70U);
        int y = BAND_Y - 1 - (int)(rise % 70U), x = (int)(h % 160U) + sin256(now / 40U + i * 21U) * 3 / 256;
        unsigned a = 70U + (unsigned)(sin256(now / 25U + i * 40U) + 256) / 6U;
        gfx_tint(px, x, y, 2, 2, 0xFFE08A, a);
    }
}
static void draw_incense(uint16_t *px, unsigned now)
{
    const int x = 19, base = 61;
    gfx_rect(px, x - 7, base - 2, 15, 4, gfx_rgb565(0x6B4A1C));
    gfx_hline(px, x - 7, x + 7, base - 2, gfx_rgb565(0xC9973A));
    gfx_rect(px, x, base - 17, 1, 15, gfx_rgb565(0x9A4A30));
    gfx_disc(px, x, base - 18, 2, gfx_rgb565(0xFF9A3C));
    gfx_tint_plot(px, x, base - 18, 0xFFFFFF, 180);
    for (int k = 0; k < 40; ++k) { /* a thin curl of smoke */
        int sx = x + sin256(now / 16U + (unsigned)k * 11U) * (2 + k / 5) / 256, sy = base - 20 - k * 3 / 2;
        unsigned alpha = k < 30 ? 110U - (unsigned)k * 3U : 20U;
        gfx_tint_plot(px, sx, sy, 0xD8D2F0, alpha);
        if (k < 24) gfx_tint_plot(px, sx + 1, sy, 0xD8D2F0, alpha / 2U);
    }
}
static void draw_cushion(uint16_t *px)
{
    const int cx = FISH_CX, cy = 62, rx = 52, ry = 13;
    for (int dy = -ry; dy <= ry; ++dy) {
        int y = cy + dy, half = rx * (int)gfx_isqrt((uint32_t)(ry * ry - dy * dy)) / ry;
        if (y < 0 || y >= BAND_Y) continue;
        uint32_t body = gfx_mix(0xC8343E, 0x7A1822, (unsigned)((dy + ry) * 256 / (2 * ry)));
        gfx_hline(px, cx - half, cx + half, y, gfx_rgb565(body));
        uint16_t trim = gfx_rgb565(dy < -ry + 2 ? 0xF5CB5C : 0xE9B949);
        gfx_plot(px, cx - half, y, trim); gfx_plot(px, cx + half, y, trim);
        if (dy < -ry + 3) gfx_hline(px, cx - half, cx + half, y, trim);
    }
}
static void draw_fish(uint16_t *px, unsigned squash)
{
    const int ry = FISH_RY - (int)(FISH_RY * squash * 18U / 25600U), rx = FISH_RX + (int)(FISH_RX * squash * 10U / 25600U);
    const int cy = FISH_BOTTOM - ry;
    gfx_tint_ellipse(px, FISH_CX, FISH_BOTTOM - 1, rx + 4, 5, 0x000000, 90); /* shadow on the cushion */
    gfx_disc(px, FISH_CX + 24, cy - ry * 3 / 4, 4, gfx_rgb565(FISH_RIM)); /* the little handle */
    gfx_disc(px, FISH_CX + 24, cy - ry * 3 / 4, 3, gfx_rgb565(FISH_MID));
    for (int dy = -ry; dy <= ry; ++dy) {
        int half = rx * (int)gfx_isqrt((uint32_t)(ry * ry - dy * dy)) / ry;
        for (int x = -half; x <= half; ++x) {
            uint32_t rgb;
            if (x <= -half + 1 || x >= half - 1 || dy <= -ry || dy >= ry) rgb = FISH_RIM;
            else {
                int u = (x + 10) * 256 / rx, v = (dy + 8) * 256 / ry;
                unsigned f = (unsigned)(u * u + v * v) / 700U;
                if (f > 256U) f = 256U;
                rgb = f < 128U ? gfx_mix(FISH_LIGHT, FISH_MID, f * 2U) : gfx_mix(FISH_MID, FISH_DARK, (f - 128U) * 2U);
            }
            gfx_plot(px, FISH_CX + x, cy + dy, gfx_rgb565(rgb));
        }
    }
    for (int x = -26; x <= 6; ++x) { /* the mouth: a dark slit with a lit lip under it */
        int y = cy + 5 + (x + 10) * (x + 10) / 80, half = rx * (int)gfx_isqrt((uint32_t)(ry * ry - (y - cy) * (y - cy))) / ry;
        if (x < -half + 2 || x > half - 2) continue;
        gfx_rect(px, FISH_CX + x, y, 1, 3, gfx_rgb565(0x2A0E05));
        gfx_tint_plot(px, FISH_CX + x, y + 3, 0xF0B070, 130);
    }
    static const int gloss[4][3] = {{-11, -20, -8}, {-10, -22, -6}, {-9, -22, -8}, {-8, -18, -10}}; /* dy, x0, x1 */
    static const unsigned gloss_alpha[4] = {40, 65, 50, 30};
    for (int i = 0; i < 4; ++i) gfx_tint(px, FISH_CX + gloss[i][1], cy + gloss[i][0] * ry / FISH_RY, gloss[i][2] - gloss[i][1] + 1, 1, 0xFFFFFF, gloss_alpha[i]);
    for (int i = 0; i < 3; ++i) { /* a few carved scales */
        gfx_hline(px, FISH_CX + 10 + i * 5, FISH_CX + 13 + i * 5, cy - 7 + i * 2, gfx_rgb565(FISH_DARK));
        gfx_plot(px, FISH_CX + 9 + i * 5, cy - 6 + i * 2, gfx_rgb565(FISH_DARK));
    }
}
static void draw_mallet(uint16_t *px, unsigned age, unsigned now)
{
    int hx, hy;
    if (age < 60U) { hx = lerp(REST_X + 4, STRIKE_X, (int)(age * 256U / 60U)); hy = lerp(REST_Y - 12, STRIKE_Y, (int)(age * 256U / 60U)); }
    else if (age < 260U) {
        int t = (int)((age - 60U) * 256U / 200U);
        t = 256 - (256 - t) * (256 - t) / 256; /* eases out */
        hx = lerp(STRIKE_X, REST_X, t); hy = lerp(STRIKE_Y, REST_Y, t);
    } else { hx = REST_X; hy = REST_Y + sin256(now / 17U) / 128; }
    gfx_line2(px, hx + 1, hy, hx + 25, hy - 32, gfx_rgb565(0x7A4A22));
    gfx_line2(px, hx, hy, hx + 24, hy - 32, gfx_rgb565(0xE0B27A));
    gfx_disc(px, hx, hy, 5, gfx_rgb565(0x6E3A18));
    gfx_disc(px, hx, hy, 4, gfx_rgb565(0xB06A33));
    gfx_disc(px, hx - 1, hy - 1, 2, gfx_rgb565(0xD99256));
}
static void draw_impact(uint16_t *px, unsigned age)
{
    static const int dirs[8][2] = {{1, -2}, {2, -1}, {2, 1}, {1, 2}, {-1, 2}, {-2, 1}, {-2, -1}, {-1, -2}};
    for (unsigned i = 0; i < 2U; ++i) { /* two ripples running out from the fish */
        if (age < i * 90U || age - i * 90U > 480U) continue;
        unsigned a = age - i * 90U;
        gfx_ring(px, FISH_CX, 38, 22 + (int)(a * 52U / 480U), 0xFFD54A, 150U * (480U - a) / 480U);
    }
    if (age < 400U)
        for (int i = 0; i < 8; ++i) { /* sparks fly from where the mallet landed */
            int d = 3 + (int)age / 13;
            unsigned alpha = 240U * (400U - age) / 400U;
            gfx_tint(px, STRIKE_X + dirs[i][0] * d / 2 - 1, STRIKE_Y + 3 + dirs[i][1] * d / 2, 2, 2, 0xFFE08A, alpha);
        }
}
static void draw_scene(uint16_t *px, const muyu_view_t *v, unsigned now)
{
    unsigned age = v->since_knock_ms;
    unsigned squash = age < 40U ? age * 256U / 40U : age < 240U ? (240U - age) * 256U / 200U : 0U;
    draw_backdrop(px, now, age);
    draw_incense(px, now);
    draw_cushion(px);
    draw_fish(px, squash);
    draw_impact(px, age);
    draw_mallet(px, age, now);
}

/* ---- app --------------------------------------------------------------------------------------------------- */
static lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb, unsigned opa)
{
    lv_obj_t *p = app_ui_rect(parent, x, y, w, h, rgb, 5);
    lv_obj_set_style_bg_opa(p, (lv_opa_t)opa, LV_PART_MAIN);
    return p;
}
static void build(state_t *s, lv_obj_t *root)
{
    s->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->image.header.cf = LV_COLOR_FORMAT_RGB565;
    s->image.header.w = APP_CANVAS_W; s->image.header.h = APP_CANVAS_H; s->image.header.stride = APP_CANVAS_W * 2;
    s->image.data_size = APP_CANVAS_W * APP_CANVAS_H * 2; s->image.data = (const uint8_t *)app_ui_pixels();
    s->plot = lv_image_create(root);
    lv_obj_set_pos(s->plot, 0, 0);
    lv_image_set_src(s->plot, &s->image);
    s->band = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, APP_HEIGHT - BAND_Y, 0x1B0F18, 0);
    s->line = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, 1, 0x7A5A1E, 0);
    s->hold = app_ui_rect(root, 0, BAND_Y, 0, 2, GOLD, 0);
    s->hint = app_ui_label(s->band, &aihook_font_12, 0, 2, APP_WIDTH, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->hint);
    lv_obj_set_style_text_color(s->hint, app_ui_color(0xC9B79C), LV_PART_MAIN);
    s->hud_width = 60;
    s->hud = pill(root, 3, 3, (int)s->hud_width, 16, 0x0B1630, 150);
    s->count = app_ui_label(s->hud, &aihook_font_12, 5, 1, 90, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->count, app_ui_color(GOLD), LV_PART_MAIN);
    s->tag = pill(root, 123, 3, 34, 16, 0x2E8B57, 230);
    s->tag_text = app_ui_label(s->tag, &aihook_font_12, 0, 1, 34, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->tag_text);
    app_ui_text(s->tag_text, "自动");
    for (unsigned i = 0; i < 2U; ++i) {
        s->floating[i] = app_ui_label(root, &aihook_font_16, 58, 6, 80, 20, LV_LABEL_LONG_MODE_CLIP);
        app_ui_center(s->floating[i]);
        lv_obj_set_style_text_color(s->floating[i], app_ui_color(GOLD), LV_PART_MAIN);
        app_ui_hide(s->floating[i], true);
    }
}
static void publish(const state_t *s, bool active, bool frame)
{
    muyu_view_t v;
    muyu_snapshot(&s->core, &v);
    portENTER_CRITICAL(&s_lock);
    s_status.active = active; s_status.auto_on = v.auto_on; s_status.count = v.count; s_status.rounds = v.rounds;
    if (frame) ++s_status.frames;
    portEXIT_CRITICAL(&s_lock);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    muyu_start(&s->core, ctx->now_ms);
    portENTER_CRITICAL(&s_lock); s_status = (status_t){0}; portEXIT_CRITICAL(&s_lock);
    publish(s, true, false);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    uint32_t before = s->core.count;
    muyu_tick(&s->core, ctx->now_ms);
    if (s->core.count != before) publish(s, true, false);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->plot) app_ui_plot_refresh(s->plot, &s->image); /* The shared canvas goes to the next app. */
    portENTER_CRITICAL(&s_lock); s_status.active = false; portEXIT_CRITICAL(&s_lock);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->kind == APP_EVENT_DOWN) { (void)muyu_knock(&s->core, e->ms); publish(s, true, false); } /* knock the moment it is pressed */
    else if (e->kind == APP_EVENT_CONFIRM) { muyu_toggle_auto(&s->core, e->ms); publish(s, true, false); }
}
static void sync_hud(state_t *s, const muyu_view_t *v, unsigned hold_ms)
{
    char text[40];
    snprintf(text, sizeof text, "功德 %lu", (unsigned long)v->count);
    app_ui_text(s->count, text);
    unsigned digits = 1;
    for (uint32_t n = v->count; n >= 10U; n /= 10U) ++digits;
    unsigned width = 36 + digits * 7;
    if (width != s->hud_width) { lv_obj_set_width(s->hud, (int)width); s->hud_width = width; }
    app_ui_hide(s->tag, !v->auto_on);
    app_ui_text(s->hint, v->auto_on ? "自动中 长按停止 三击退出" : "按键敲 长按自动 三击退出");
    int bar = hold_ms > 120U ? (int)(hold_ms > APP_CONFIRM_MS ? APP_CONFIRM_MS : hold_ms) * APP_WIDTH / (int)APP_CONFIRM_MS : 0;
    app_ui_hide(s->hold, bar == 0);
    if (bar) lv_obj_set_width(s->hold, bar);
    for (unsigned i = 0; i < 2U; ++i) { /* the floating 功德+1: rises, then fades; two slots may overlap */
        bool show = v->shown[i] && v->age_ms[i] < FLOAT_MS;
        app_ui_hide(s->floating[i], !show);
        if (!show) continue;
        unsigned age = v->age_ms[i];
        app_ui_text(s->floating[i], v->round[i] ? "功德圆满" : "功德+1");
        lv_obj_set_y(s->floating[i], 6 - (int)(age * FLOAT_RISE / FLOAT_MS));
        unsigned opa = age < 350U ? 255U : 255U * (FLOAT_MS - age) / 350U;
        if (v->shown[i ^ 1U] && v->age_ms[i ^ 1U] < age && v->age_ms[i ^ 1U] < 220U) /* a newer knock pushes this one out of the way */
            opa = opa * (220U - v->age_ms[i ^ 1U]) / 220U;
        lv_obj_set_style_text_opa(s->floating[i], (lv_opa_t)opa, LV_PART_MAIN);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    muyu_view_t v;
    muyu_snapshot(&s->core, &v);
    sync_hud(s, &v, app_hold_ms(ctx));
    draw_scene(app_ui_pixels(), &v, (unsigned)ctx->now_ms);
    app_ui_plot_refresh(s->plot, &s->image);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (strcmp(line, "robot muyu status")) return false;
    status_t v;
    portENTER_CRITICAL(&s_lock); v = s_status; portEXIT_CRITICAL(&s_lock);
    char reply[96];
    snprintf(reply, sizeof reply, "active=%u count=%lu auto=%u rounds=%lu frames=%lu", v.active ? 1U : 0U, (unsigned long)v.count,
             v.auto_on ? 1U : 0U, (unsigned long)v.rounds, (unsigned long)v.frames);
    (void)app_transport_write(reply);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    const uint32_t body = selected ? 0xF0A24E : 0xC9733A;
    app_ui_rect(parent, x + 1, y + 20, 26, 7, 0xA3222B, 3);          /* cushion */
    app_ui_rect(parent, x + 1, y + 20, 26, 2, 0xE9B949, 1);
    app_ui_rect(parent, x + 3, y + 8, 22, 16, body, 8);              /* the fish */
    app_ui_rect(parent, x + 7, y + 10, 8, 3, 0xF8D08C, 1);           /* gloss */
    app_ui_rect(parent, x + 5, y + 17, 12, 2, 0x4A210F, 1);          /* mouth */
    app_ui_rect(parent, x + 20, y + 2, 3, 10, 0xE0B27A, 1);          /* mallet */
    app_ui_rect(parent, x + 17, y + 9, 8, 6, 0xB06A33, 3);
    app_ui_rect(parent, x + 1, y + 3, 2, 2, GOLD, 0);                /* sparkles */
    app_ui_rect(parent, x + 25, y + 18, 2, 2, GOLD, 0);
}
const app_descriptor_t esphook_app_muyu = {.api_version = APP_API_VERSION, .id = "muyu", .title = "电子木鱼",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
