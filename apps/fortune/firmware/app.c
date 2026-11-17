#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include "esphook/app_command.h"
#include "esphook/app_sync.h"
#include "fortune_core.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 今日幸运签: a red lacquer tube of bamboo sticks on a dark red lattice. Hold the key and the tube shakes, let go and
 * one stick jumps out, then the slip unfolds: a gold seal with the grade (大吉 / 中吉 / 小吉), the area of life, a
 * blessing, and a lucky number and colour in the strip below. Everything is a lucky slip; the game is only which. */
#define BAND_Y 64
#define TUBE_X 80
#define RIM_Y 40
#define FOOT_Y 61
#define CARD_L 6
#define CARD_T 2
#define CARD_W 148
#define CARD_H 60
#define SEAL_X 34
#define SEAL_Y 32
#define SEAL_R 20
#define HINT_SLOT_MS 2400U
#define GOLD 0xFDB927

typedef struct {
    ft_t core;
    lv_obj_t *plot, *band, *title, *hint, *num, *swatch, *colour_text, *category, *grade, *blessing;
    uint64_t last_ms;
    uint32_t shown_key, shown_draws;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "fortune state too large");
_Static_assert(offsetof(state_t, core) == 0, "tests pose the draw through the core at the start of the state");
typedef struct { bool active; uint8_t phase, category, level, text, number, colour; uint32_t draws, frames; } status_t;
static status_t s_status;

static uint16_t c565(uint32_t rgb) { return gfx_rgb565(rgb); }
static unsigned ease_out(unsigned t, unsigned total) /* 0..256 */
{
    if (t >= total) return 256U;
    const unsigned u = 256U - t * 256U / total;
    return 256U - u * u / 256U;
}

/* ---- backdrop -------------------------------------------------------------------------------------------------- */
static void draw_backdrop(uint16_t *px, unsigned now)
{
    gfx_gradient(px, 0, APP_CANVAS_H, 0x3A0C14, 0x12040A);
    for (int x = 4; x < APP_CANVAS_W; x += 16) gfx_tint(px, x, 0, 1, APP_CANVAS_H, GOLD, 14); /* lattice */
    for (int y = 6; y < APP_CANVAS_H; y += 16) gfx_tint(px, 0, y, APP_CANVAS_W, 1, GOLD, 14);
    for (int i = 0; i < 6; ++i) { /* drifting motes of light */
        const uint32_t h = gfx_hash((uint32_t)i * 977U + 5U);
        const int x = (int)(h % 150U) + 5, y = (int)(((h >> 9) % 40U) + (now / (45U + (h >> 20) % 30U)) % 24U) % 60;
        gfx_tint_plot(px, x, y, 0xFFD27A, 40U + (unsigned)((now / 90U + (unsigned)i * 5U) % 5U) * 20U);
    }
}

/* ---- the tube -------------------------------------------------------------------------------------------------- */
static void draw_stick(uint16_t *px, int x0, int y0, int x1, int y1)
{
    gfx_line(px, x0, y0, x1, y1, c565(0xE6CE94));
    gfx_line(px, x0 + 1, y0, x1 + 1, y1, c565(0xB99A5E));
    const int dx = x1 - x0, dy = y1 - y0;
    gfx_line(px, x1 - dx / 7, y1 - dy / 7, x1, y1, c565(0xD7263D)); /* the red tip */
    gfx_line(px, x1 + 1 - dx / 7, y1 - dy / 7, x1 + 1, y1, c565(0xA81A2E));
}
static void draw_tube(uint16_t *px, const ft_t *f, unsigned now)
{
    const unsigned t = f->phase_ms;
    int shake = 0, tilt = 0;
    unsigned rise = 0;
    if (f->phase == FT_SHAKE) {
        const unsigned ramp = t < 140U ? t * 256U / 140U : 256U;
        shake = gfx_sin256(t * 9U / 5U) * 5 * (int)ramp / 65536;
        tilt = gfx_sin256(t * 9U / 5U + 64U) * 5 * (int)ramp / 65536;
    } else if (f->phase == FT_POP) {
        rise = ease_out(t, 420U);
        const int settle = t < 200U ? (int)(200U - t) : 0;
        shake = gfx_sin256(t * 3U) * settle / 2200;
    } else if (f->phase >= FT_UNFOLD) rise = 256U;
    const int cx = TUBE_X + shake, rimy = RIM_Y;
    gfx_tint_ellipse(px, cx, FOOT_Y + 1, 21, 4, 0x000000, 130); /* its shadow on the table */
    gfx_tint_ellipse(px, cx, 34, 34, 22, 0xFFC864, 20); /* warm glow behind it */
    static const int base[9] = {-10, -7, -4, -2, 0, 2, 4, 7, 10};
    static const int lift[9] = {7, 13, 9, 16, 12, 15, 8, 12, 10};
    for (int i = 0; i < 9; ++i) { /* the sticks behind the front lip of the tube */
        const bool chosen = i == 4 && f->phase >= FT_POP;
        int top = lift[i] + 4, sway = 0;
        if (f->phase == FT_SHAKE) {
            const unsigned ph = (unsigned)i * 41U + t * 2U;
            top += gfx_sin256(ph) * 5 / 256;
            sway = gfx_sin256(ph + 90U) * 3 / 256 + tilt * 2;
        }
        int x1 = cx + base[i] + sway + base[i] / 3, y1 = rimy + 2 - top;
        if (chosen) {
            y1 = rimy + 2 - (top + (int)((26U + 4U) * rise / 256U));
            x1 = cx + base[i] / 2;
            gfx_tint_ellipse(px, x1, y1 + 4, 7, 16, 0xFFD870, 40U + rise / 4U); /* a glow around the stick that came out */
        }
        draw_stick(px, cx + base[i] + base[i] / 3 + tilt, rimy + 3, x1, y1);
    }
    for (int y = rimy; y <= FOOT_Y; ++y) { /* the lacquer barrel, a little wider at the foot, sheared by the tilt */
        const int off = tilt * (FOOT_Y - y) / 22, half = 12 + (y - rimy) / 9;
        const int x0 = cx - half + off, x1 = cx + half + off;
        gfx_hline(px, x0, x1, y, c565(0xA52B25));
        gfx_tint(px, x0, y, 5, 1, 0xFF9A7A, 70);                 /* highlight on the left */
        gfx_tint(px, x1 - 5, y, 6, 1, 0x1A0408, 120);            /* shade on the right */
        gfx_tint_plot(px, x0, y, 0xFFE0A0, 80);
    }
    for (unsigned band = 0; band < 2U; ++band) { /* gold hoops */
        const int y = band ? FOOT_Y - 3 : rimy + 4, off = tilt * (FOOT_Y - y) / 22, half = 12 + (y - rimy) / 9;
        gfx_rect(px, cx - half + off, y, half * 2 + 1, 2, c565(0xF2B63C));
        gfx_hline(px, cx - half + off, cx + half + off, y, c565(0xFFE08A));
    }
    gfx_ellipse(px, cx + tilt * 2 / 4, rimy, 13, 3, c565(0x3A0C10)); /* the mouth: dark inside, the lip a gold line */
    gfx_tint_ellipse(px, cx + tilt * 2 / 4, rimy, 13, 3, 0xF2B63C, 90);
    gfx_ellipse(px, cx + tilt * 2 / 4, rimy + 1, 11, 2, c565(0x2A0609));
    for (int i = 0; i < 9; ++i) /* the stick bottoms, hidden by the lip of the barrel */
        gfx_rect(px, cx + base[i] + base[i] / 3 + tilt - 1, rimy + 2, 3, 2, c565(0xB99A5E));
    gfx_rect(px, cx - 3 + tilt / 2, rimy + 12, 7, 2, c565(0xF2B63C)); /* the emblem: a gold diamond */
    gfx_tri(px, cx + tilt / 2, rimy + 9, cx - 4 + tilt / 2, rimy + 13, cx + 4 + tilt / 2, rimy + 13, c565(0xFFD65A));
    gfx_tri(px, cx + tilt / 2, rimy + 18, cx - 4 + tilt / 2, rimy + 14, cx + 4 + tilt / 2, rimy + 14, c565(0xF2B63C));
    (void)now;
}

/* ---- the slip -------------------------------------------------------------------------------------------------- */
static void sparkle(uint16_t *px, int x, int y, unsigned size, unsigned alpha)
{
    gfx_tint_plot(px, x, y, 0xFFF3C4, alpha);
    for (unsigned k = 1; k <= size; ++k) {
        const unsigned a = alpha * (size + 1U - k) / (size + 1U);
        gfx_tint_plot(px, x - (int)k, y, 0xFFE08A, a); gfx_tint_plot(px, x + (int)k, y, 0xFFE08A, a);
        gfx_tint_plot(px, x, y - (int)k, 0xFFE08A, a); gfx_tint_plot(px, x, y + (int)k, 0xFFE08A, a);
    }
}
static void draw_card(uint16_t *px, const ft_t *f, unsigned now)
{
    unsigned grow = 256U;
    if (f->phase == FT_UNFOLD) grow = ease_out(f->phase_ms, FT_UNFOLD_MS);
    const int w = (int)(CARD_W * grow / 256U) < 6 ? 6 : (int)(CARD_W * grow / 256U);
    const int h = (int)(CARD_H * (96U + grow * 160U / 256U) / 256U);
    const int l = TUBE_X - w / 2, t = 32 - h / 2;
    for (int y = t; y < t + h; ++y) { /* red paper, a little darker at the foot */
        const unsigned m = (unsigned)((y - t) * 256 / (h > 1 ? h : 1));
        gfx_hline(px, l, l + w - 1, y, c565(gfx_mix(0xC42B35, 0x7A121B, m)));
    }
    gfx_rect(px, l, t, w, 1, c565(0xF2B63C)); gfx_rect(px, l, t + h - 1, w, 1, c565(0xD08A26));
    gfx_rect(px, l, t, 1, h, c565(0xF2B63C)); gfx_rect(px, l + w - 1, t, 1, h, c565(0xD08A26));
    if (grow < 256U || w < 60) return;
    gfx_tint(px, l + 3, t + 3, w - 6, 1, 0xFFE08A, 90); gfx_tint(px, l + 3, t + h - 4, w - 6, 1, 0xFFE08A, 90); /* inner line */
    gfx_tint(px, l + 3, t + 3, 1, h - 6, 0xFFE08A, 90); gfx_tint(px, l + w - 4, t + 3, 1, h - 6, 0xFFE08A, 90);
    static const int cx[4] = {7, 148, 7, 148}, cy[4] = {5, 5, 58, 58};
    for (unsigned k = 0; k < 4U; ++k) { /* corner studs */
        gfx_rect(px, cx[k] - 1, cy[k] - 1, 3, 3, c565(0xFFD65A));
        gfx_plot(px, cx[k], cy[k], c565(0xC42B35));
    }
    const int sx = SEAL_X, sy = SEAL_Y;
    if (f->sign.level == FT_BIG) { /* rays for the best grade */
        for (unsigned a = 0; a < 24U; ++a) {
            const unsigned ph = (unsigned)a * 256U / 24U, flick = (now / 70U + a * 3U) % 7U;
            const int r0 = SEAL_R + 2, r1 = SEAL_R + 4 + (int)flick;
            const int x0 = sx + gfx_sin256(ph + 64U) * r0 / 256, y0 = sy + gfx_sin256(ph) * r0 / 256;
            const int x1 = sx + gfx_sin256(ph + 64U) * r1 / 256, y1 = sy + gfx_sin256(ph) * r1 / 256;
            gfx_line(px, x0, y0, x1, y1, c565(gfx_mix(0xC42B35, 0xFFD65A, 90U + flick * 20U)));
        }
    }
    gfx_disc(px, sx, sy, SEAL_R, c565(0xF2B63C));                   /* the seal */
    gfx_disc(px, sx, sy, SEAL_R - 2, c565(0x8E1822));
    gfx_ring(px, sx, sy, SEAL_R - 4, 0xFFD65A, 170);
    gfx_tint_ellipse(px, sx - 5, sy - 8, 8, 4, 0xFFFFFF, 26);
    gfx_vline(px, 60, 8, 55, c565(gfx_mix(0xC42B35, 0xFFD65A, 110))); /* a gold rule between seal and writing */
    gfx_hline(px, 66, 148, 25, c565(gfx_mix(0x9C1E28, 0xFFD65A, 130)));
    const unsigned twinkles = f->sign.level == FT_BIG ? 9U : f->sign.level == FT_MID ? 4U : 0U;
    for (unsigned i = 0; i < twinkles; ++i) {
        const uint32_t h2 = gfx_hash(i * 131U + f->draws * 7U + 3U);
        const unsigned life = (now / 60U + (h2 >> 8) % 24U) % 24U;
        if (life > 9U) continue;
        const int x = 10 + (int)(h2 % 140U), y = 6 + (int)((h2 >> 12) % 52U);
        sparkle(px, x, y, 1U + (life < 5U ? life : 9U - life) / 2U, 120U + (life < 5U ? life : 9U - life) * 28U);
    }
}

static void draw_scene(uint16_t *px, const ft_t *f, unsigned now)
{
    draw_backdrop(px, now);
    if (f->phase == FT_SLIP) { draw_card(px, f, now); return; }
    draw_tube(px, f, now);
    if (f->phase == FT_UNFOLD) {
        draw_card(px, f, now);
        const unsigned flash = f->phase_ms < 120U ? 120U - f->phase_ms : 0U; /* the flash of the card opening */
        if (flash) gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0xFFF1C0, flash * 2U);
    }
}

/* ---- app ------------------------------------------------------------------------------------------------------- */
static void build(state_t *s, lv_obj_t *root)
{
    s->plot = app_ui_canvas(root, 0, 0, APP_CANVAS_W, APP_CANVAS_H);
    s->band = app_ui_band(root, 0x1A0409, 0x8A5A1A);
    s->hint = app_ui_label(s->band, &aihook_font_12, 0, 2, APP_WIDTH, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->hint);
    lv_obj_set_style_text_color(s->hint, app_ui_color(0xF0D9A0), LV_PART_MAIN);
    s->num = app_ui_label(s->band, &aihook_font_12, 6, 2, 62, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->num, app_ui_color(0xFFE08A), LV_PART_MAIN);
    s->swatch = app_ui_rect(s->band, 82, 4, 10, 10, GOLD, 2);
    s->colour_text = app_ui_label(s->band, &aihook_font_12, 96, 2, 62, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->colour_text, app_ui_color(0xFFE08A), LV_PART_MAIN);
    s->title = app_ui_label(root, &aihook_font_16, 0, 2, APP_WIDTH, 20, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->title);
    lv_obj_set_style_text_color(s->title, app_ui_color(0xFFD65A), LV_PART_MAIN);
    app_ui_text(s->title, "今日幸运签");
    s->grade = app_ui_label(root, &aihook_font_16, SEAL_X - 16, SEAL_Y - 10, 32, 20, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->grade);
    s->category = app_ui_label(root, &aihook_font_16, 66, 5, 84, 20, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->category, app_ui_color(0xFFD65A), LV_PART_MAIN);
    s->blessing = app_ui_label(root, &aihook_font_12, 66, 28, 86, 32, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(s->blessing, app_ui_color(0xFFEFD0), LV_PART_MAIN);
    app_ui_hide(s->grade, true); app_ui_hide(s->category, true); app_ui_hide(s->blessing, true);
    s->shown_key = 0xFFFFFFFFU;
}
static void publish(const state_t *s, bool active, bool frame)
{
    const ft_t *f = &s->core;
    app_critical_enter();
    s_status.active = active; s_status.phase = f->phase; s_status.draws = f->draws;
    s_status.category = f->sign.category; s_status.level = f->sign.level; s_status.text = f->sign.text;
    s_status.number = f->sign.number; s_status.colour = f->sign.colour;
    if (frame) ++s_status.frames;
    app_critical_exit();
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    app_time_t t;
    app_time_read(&t);
    ft_init(&s->core, app_random() ^ (uint32_t)ctx->now_ms ^ (t.valid ? t.epoch : 0U));
    s->last_ms = ctx->now_ms;
    app_critical_enter(); s_status = (status_t){0}; app_critical_exit();
    publish(s, true, false);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t now = ctx->now_ms;
    const uint32_t dt = now > s->last_ms ? (uint32_t)(now - s->last_ms) : 0U;
    s->last_ms = now;
    const unsigned before = s->core.phase;
    ft_advance(&s->core, dt);
    if (before != s->core.phase) publish(s, true, false);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->plot) app_ui_canvas_refresh(s->plot); /* The shared canvas goes to the next app. */
    app_critical_enter(); s_status.active = false; app_critical_exit();
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->kind == APP_EVENT_DOWN && e->key == 0) ft_press(&s->core);
    else if (e->kind == APP_EVENT_UP || e->kind == APP_EVENT_CANCEL) ft_release(&s->core);
    publish(s, true, false);
}
static void sync_hud(state_t *s, uint64_t now)
{
    const ft_t *f = &s->core;
    char text[40];
    const bool alt = (now / HINT_SLOT_MS) % 2U == 1U;
    app_time_t t;
    app_time_read(&t);
    const char *week = "日一二三四五六";
    const bool slip = f->phase == FT_SLIP, card = slip; /* the text waits until the slip is fully open */
    const uint32_t key = (uint32_t)f->phase << 24 | (uint32_t)alt << 20 | (uint32_t)(t.valid ? t.day + t.month * 40 : 0) << 4 | f->sign.number;
    app_ui_hide(s->title, f->phase >= FT_UNFOLD);
    app_ui_hide(s->hint, slip && !alt);
    app_ui_hide(s->num, !(slip && !alt)); app_ui_hide(s->swatch, !(slip && !alt)); app_ui_hide(s->colour_text, !(slip && !alt));
    app_ui_hide(s->grade, !card); app_ui_hide(s->category, !card); app_ui_hide(s->blessing, !card);
    if (key == s->shown_key && f->draws == s->shown_draws) return;
    s->shown_key = key; s->shown_draws = f->draws;
    switch (f->phase) {
    case FT_IDLE:
        if (t.valid && alt) {
            snprintf(text, sizeof text, "%d月%d日 周%.3s", t.month, t.day, week + (size_t)t.weekday * 3U);
            app_ui_text(s->hint, text);
        } else app_ui_text(s->hint, "按键摇签 按住摇得更久");
        break;
    case FT_SHAKE: app_ui_text(s->hint, "摇签中..."); break;
    case FT_POP: app_ui_text(s->hint, "有签出来了!"); break;
    case FT_UNFOLD: app_ui_text(s->hint, "展开签文"); break;
    default: app_ui_text(s->hint, "按键再抽一签"); break;
    }
    if (!card) return;
    snprintf(text, sizeof text, "%s运", ft_category_name(f->sign.category));
    app_ui_text(s->category, text);
    app_ui_text(s->grade, ft_level_name(f->sign.level));
    lv_obj_set_style_text_color(s->grade, app_ui_color(f->sign.level == FT_BIG ? 0xFFE066 : f->sign.level == FT_MID ? 0xFFEBC0 : 0xF2D3A8), LV_PART_MAIN);
    app_ui_text(s->blessing, ft_text(f->sign.category, f->sign.level, f->sign.text));
    snprintf(text, sizeof text, "幸运数 %u", (unsigned)f->sign.number);
    app_ui_text(s->num, text);
    snprintf(text, sizeof text, "幸运色 %s", ft_colour_name(f->sign.colour));
    app_ui_text(s->colour_text, text);
    lv_obj_set_style_bg_color(s->swatch, app_ui_color(ft_colour_rgb(f->sign.colour)), LV_PART_MAIN);
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    sync_hud(s, ctx->now_ms);
    draw_scene(app_ui_pixels(), &s->core, (unsigned)ctx->now_ms);
    app_ui_canvas_refresh(s->plot);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot fortune status")) return false;
    status_t v;
    app_shared_copy(v, s_status);
    (void)app_command_replyf("active=%u phase=%u draws=%lu category=%u level=%u text=%u number=%u colour=%u frames=%lu",
            v.active ? 1U : 0U, v.phase, (unsigned long)v.draws, v.category, v.level, v.text, v.number, v.colour, (unsigned long)v.frames);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x + 1, y + 1, 26, 26, selected ? 0x5A1420 : 0x3A0C14, 5);        /* lacquer red */
    for (int i = 0; i < 5; ++i) { /* sticks */
        app_ui_rect(parent, x + 6 + i * 4, y + 3 + (i % 2) * 2 + (i == 2 ? -2 : 0), 2, 12, 0xE6CE94, 0);
        app_ui_rect(parent, x + 6 + i * 4, y + 3 + (i % 2) * 2 + (i == 2 ? -2 : 0), 2, 3, 0xD7263D, 0);
    }
    app_ui_rect(parent, x + 4, y + 13, 20, 12, 0xB02A26, 3);                                /* the tube */
    app_ui_rect(parent, x + 4, y + 13, 20, 2, 0xF2B63C, 1);
    app_ui_rect(parent, x + 4, y + 22, 20, 2, 0xF2B63C, 1);
    app_ui_rect(parent, x + 12, y + 16, 4, 4, 0xFFD65A, 1);
}
const app_descriptor_t esphook_app_fortune = {.api_version = APP_API_VERSION, .id = "fortune", .title = "幸运签",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
