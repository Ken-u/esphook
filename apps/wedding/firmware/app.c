#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include "wedding_glyphs.h"
#include "cJSON.h"
#include "esphook/app_command.h"
#include "esphook/app_sync.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 新婚快乐 / Happy Wedding: the default standby screen. The whole scene is painted into the shared canvas from the
 * time since entry alone (no per-object state): ribbons hang from the top, confetti rains down, and every time the
 * banner changes a party popper on alternating sides throws streamers and confetti. The bottom 16 px are a scrolling
 * blessing line (an LVGL label), which a PC message can replace. */
#define BAND_Y 64
#define BAND_H 16
#define PAGE_MS 4600U   /* one banner; the Chinese and English ones alternate */
#define FADE_MS 360U
#define POP_DELAY_MS 140
#define POP_LIFE_MS 6400
#define STREAMERS 8
#define CONFETTI 18
#define RAIN 22
#define WISH_MAX 128
#define DEFAULT_WISH "百年好合 ★ 永结同心 ★ 幸福美满 ★ 白头偕老 ★ Wishing you a lifetime of love and happiness ★ "

typedef struct {
    lv_obj_t *plot, *band, *wish;
    uint64_t entered_ms;
    char text[WISH_MAX];
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "wedding state too large");
_Static_assert(sizeof DEFAULT_WISH <= WISH_MAX, "the default blessing must fit its buffer untruncated");
typedef struct { bool active; unsigned page; uint32_t frames; } status_t;
static status_t s_status;

static const uint32_t PALETTE[8] = {0xFFD54A, 0xFFFFFF, 0xFF8FB1, 0x58D6E0, 0xB38CFF, 0x6FE0A0, 0xFF7A5C, 0xFFE9A8};
static const uint32_t BG_TOP = 0x3A0617, BG_MID = 0x8E1235, BG_LOW = 0xDD4670;
static const uint32_t GOLD_TOP = 0xFFF3B8, GOLD_BOTTOM = 0xE79A12, INK = 0x4B0A22, SHADOW = 0x25030F;

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

/* ---- text -------------------------------------------------------------------------------------------------- */
typedef struct { int fill_top, fill_bottom, shine; } look_t;
static unsigned coverage(const wedding_glyph_t *g, int col, int row)
{
    unsigned i = (unsigned)row * g->w + (unsigned)col;
    return (WEDDING_GLYPH_DATA[g->offset + (i >> 2)] >> (6U - 2U * (i & 3U))) & 3U;
}
/* kind 0: drop shadow, 1: ink outline, 2: golden fill with a travelling shine. `alpha` is 0..256. */
static void paint_glyph(uint16_t *px, const wedding_glyph_t *g, int x, int y, int kind, unsigned alpha, const look_t *look)
{
    for (int row = 0; row < g->h; ++row) {
        for (int col = 0; col < g->w; ++col) {
            unsigned cov = coverage(g, col, row);
            if (!cov) continue;
            unsigned a = (cov == 3U ? 256U : cov * 85U) * alpha / 256U;
            int sx = x + col, sy = y + row;
            if (kind == 0) { gfx_tint_plot(px, sx + 1, sy + 2, SHADOW, a * 3U / 4U); continue; }
            if (kind == 1) {
                gfx_tint_plot(px, sx - 1, sy, INK, a); gfx_tint_plot(px, sx + 1, sy, INK, a);
                gfx_tint_plot(px, sx, sy - 1, INK, a); gfx_tint_plot(px, sx, sy + 1, INK, a);
                continue;
            }
            int span = look->fill_bottom - look->fill_top;
            int t = span > 0 ? (sy - look->fill_top) * 256 / span : 0;
            uint32_t rgb = gfx_mix(GOLD_TOP, GOLD_BOTTOM, (unsigned)(t < 0 ? 0 : t > 256 ? 256 : t));
            int d = sx + sy - look->shine;
            if (d < 0) d = -d;
            if (d < 9) rgb = gfx_mix(rgb, 0xFFFFFF, (unsigned)(9 - d) * 22U);
            gfx_tint_plot(px, sx, sy, rgb, a);
        }
    }
}
static const wedding_glyph_t *find_glyph(const wedding_glyph_t *set, unsigned count, uint32_t code)
{
    for (unsigned i = 0; i < count; ++i) if (set[i].codepoint == code) return &set[i];
    return NULL;
}
/* Draws a centred run of glyphs from one set with its baseline on `baseline`; `bounce` makes letters hop in turn. */
static void draw_run(uint16_t *px, const wedding_glyph_t *set, unsigned count, const uint32_t *codes, unsigned length,
                     int baseline, unsigned alpha, unsigned now_ms, bool bounce, int fill_top, int fill_bottom)
{
    const wedding_glyph_t *glyphs[12];
    int width = -1;
    if (length > 12U) length = 12U;
    for (unsigned i = 0; i < length; ++i) {
        glyphs[i] = find_glyph(set, count, codes[i]);
        if (glyphs[i]) width += glyphs[i]->advance;
    }
    const look_t look = {fill_top, fill_bottom, (int)((now_ms / 6U) % 560U) - 140};
    for (int kind = 0; kind < 3; ++kind) {
        int pen = (APP_CANVAS_W - width) / 2;
        for (unsigned i = 0; i < length; ++i) {
            if (!glyphs[i]) continue;
            int hop = bounce ? sin256(now_ms / 7U + i * 48U) * 2 / 256 : 0;
            paint_glyph(px, glyphs[i], pen + glyphs[i]->left, baseline + glyphs[i]->top + hop, kind, alpha, &look);
            pen += glyphs[i]->advance;
        }
    }
}

/* ---- small pieces ------------------------------------------------------------------------------------------ */
static void star(uint16_t *px, int x, int y, int arm, uint32_t rgb, unsigned alpha)
{
    for (int k = 1; k <= arm; ++k) {
        unsigned a = alpha * (unsigned)(arm + 1 - k) / (unsigned)(arm + 1);
        gfx_tint_plot(px, x - k, y, rgb, a); gfx_tint_plot(px, x + k, y, rgb, a);
        gfx_tint_plot(px, x, y - k, rgb, a); gfx_tint_plot(px, x, y + k, rgb, a);
    }
    gfx_tint_plot(px, x, y, 0xFFFFFF, alpha);
}
static void heart(uint16_t *px, int x, int y)
{
    static const char *const rows[9] = {".####.####.", "##h#######d", "#h#########", "###########", ".#########.",
                                        "..#######..", "...#####...", "....###....", ".....#....."};
    static const uint32_t rgb[3] = {0xFF3D6E, 0xFFB3C6, 0xC21F4F};
    uint16_t colors[3];
    for (int i = 0; i < 3; ++i) colors[i] = gfx_rgb565(rgb[i]);
    gfx_blit(px, 11, 9, rows, x, y, "#hd", colors, false);
}
static void rings(uint16_t *px, int x, int y)
{
    for (int i = 0; i < 2; ++i) {
        gfx_ring(px, x - 4 + i * 8, y, 7, i ? 0xFFE9A8 : 0xFFC93C, 256);
        gfx_ring(px, x - 4 + i * 8, y, 6, i ? 0xFFE9A8 : 0xFFC93C, 150);
    }
    star(px, x, y - 8, 2, 0xFFFFFF, 220);
}
static void confetti_piece(uint16_t *px, int x, int y, unsigned shape, uint32_t rgb)
{
    uint16_t c = gfx_rgb565(rgb), dark = gfx_rgb565(gfx_mix(rgb, 0x000000, 70));
    switch (shape & 3U) {
    case 0: gfx_rect(px, x, y, 2, 2, c); break;
    case 1: gfx_rect(px, x - 1, y, 3, 1, c); break;
    case 2: gfx_rect(px, x, y, 2, 2, dark); break;
    default: gfx_rect(px, x, y, 1, 3, c); break;
    }
}

/* ---- scene ------------------------------------------------------------------------------------------------- */
static void draw_background(uint16_t *px)
{
    gfx_gradient(px, 0, 34, BG_TOP, BG_MID);
    gfx_gradient(px, 34, BAND_Y, BG_MID, BG_LOW);
    static const int radius[3][2] = {{74, 31}, {54, 23}, {34, 15}}; /* a soft golden glow behind the banner */
    for (int e = 0; e < 3; ++e) gfx_tint_ellipse(px, 80, 31, radius[e][0], radius[e][1], 0xFFC857, 14);
}
static void draw_sparkles(uint16_t *px, unsigned now)
{
    for (unsigned i = 0; i < 12U; ++i) {
        uint32_t h = gfx_hash(i * 193U + 7U);
        unsigned period = 1300U + (h & 511U), phase = (now + (h >> 9) % period) % period;
        if (phase > 520U) continue;
        unsigned a = phase < 260U ? phase * 256U / 260U : (520U - phase) * 256U / 260U;
        star(px, 4 + (int)((h >> 12) % 152U), 3 + (int)((h >> 20) % 58U), 2 + (int)(h % 3U) / 2, 0xFFF0B0, a);
    }
}
static void draw_hanging_ribbons(uint16_t *px, unsigned now)
{
    static const struct { int x, length; uint8_t color; } ribbon[8] = {{3, 26, 0}, {10, 36, 2}, {17, 22, 3}, {24, 30, 4},
                                                                      {133, 30, 5}, {141, 22, 6}, {148, 36, 0}, {155, 26, 2}};
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < ribbon[i].length; ++k) {
            int sway = sin256(now / 11U + (unsigned)(i * 41 + k * 6)) * (1 + k / 7) / 256;
            uint32_t rgb = ((k / 3) & 1) ? gfx_mix(PALETTE[ribbon[i].color], 0xFFFFFF, 90) : PALETTE[ribbon[i].color];
            uint16_t c = gfx_rgb565(rgb);
            gfx_plot(px, ribbon[i].x + sway, k, c); gfx_plot(px, ribbon[i].x + sway + 1, k, c);
        }
    }
}
static void draw_rain(uint16_t *px, unsigned now)
{
    for (unsigned i = 0; i < RAIN; ++i) {
        uint32_t h = gfx_hash(i * 53U + 1U);
        unsigned speed = 11U + (h >> 8) % 22U, travel = (unsigned)(((uint64_t)now * speed / 1000U) + (h >> 14) % 72U);
        int y = (int)(travel % 72U) - 4, x = (int)(h % 160U) + sin256(now / 13U + i * 37U) * 4 / 256;
        confetti_piece(px, x, y, now / 110U + i, PALETTE[(h >> 20) & 7U]);
    }
}
/* A popper's pieces slow down quickly (drag), then drift down: s(t) = v*t / (1 + t/tau) + fall*t. */
#define DRAG_MS 250
static int travel(int v, int t) { return (int)((int64_t)v * t * DRAG_MS / (1000 * (DRAG_MS + t))); }
static void popper(uint16_t *px, unsigned id, int age)
{
    const bool right = id & 1U;
    const int x0 = right ? 156 : 3, y0 = BAND_Y - 2, dir = right ? -1 : 1;
    if (age < 0 || age > POP_LIFE_MS) return;
    for (unsigned j = 0; j < CONFETTI; ++j) {
        uint32_t h = gfx_hash(id * 131U + j * 17U + 9U);
        int vx = dir * (40 + (int)((h >> 4) % 280U)), vy = 120 + (int)((h >> 12) % 180U), fall = 13 + (int)(h % 13U);
        int t = age;
        int x = x0 + travel(vx, t) + (t > 400 ? sin256((unsigned)t / 7U + j * 31U) * 5 / 256 : 0);
        int y = y0 - travel(vy, t) + fall * t / 1000;
        confetti_piece(px, x, y, (unsigned)t / 90U + j, PALETTE[(h >> 20) & 7U]);
    }
    for (unsigned i = 0; i < STREAMERS; ++i) {
        uint32_t h = gfx_hash(id * 977U + i * 31U + 5U);
        int vx = dir * (80 + (int)((h >> 4) % 240U)), vy = 200 + (int)((h >> 12) % 110U), fall = 15 + (int)(h % 7U);
        uint32_t rgb = PALETTE[(h >> 20) & 7U];
        unsigned phase = (h >> 24) & 255U;
        int hang = age - 250 < 0 ? 0 : (age - 250) * 256 / 600, hx = 0, hy = 0, px_prev = 0, py_prev = 0;
        if (hang > 256) hang = 256;
        for (int k = 0; k < 14; ++k) {
            int t = age - k * 46;
            if (t < 0) break;
            int x = x0 + travel(vx, t), y = y0 - travel(vy, t) + fall * t / 1000;
            if (k == 0) { hx = x; hy = y; }
            int tx = x + sin256((unsigned)t / 5U + phase + (unsigned)k * 23U) * (k * 3 / 4) / 256;
            int hang_x = hx + sin256((unsigned)age / 9U + phase + (unsigned)k * 29U) * (k * 3 / 5) / 256, hang_y = hy + k * 3;
            int cx = tx + (hang_x - tx) * hang / 256, cy = y + (hang_y - y) * hang / 256;
            if (k) {
                uint16_t c = gfx_rgb565((k / 2) & 1 ? gfx_mix(rgb, 0xFFFFFF, 100) : rgb);
                if (k < 6) gfx_line2(px, px_prev, py_prev, cx, cy, c);
                else gfx_line(px, px_prev, py_prev, cx, cy, c);
            }
            px_prev = cx; py_prev = cy;
        }
        if (age < 170) { /* the pop: a short flash at the mouth */
            int len = 4 + age / 12;
            gfx_line(px, x0, y0, x0 + dir * len * (3 + (int)i % 3) / 4, y0 - len, gfx_rgb565(0xFFF3B8));
        }
    }
}
static unsigned fade_alpha(unsigned page_ms)
{
    unsigned in = page_ms < FADE_MS ? page_ms * 256U / FADE_MS : 256U;
    unsigned out = page_ms > PAGE_MS - FADE_MS ? (PAGE_MS - page_ms) * 256U / FADE_MS : 256U;
    return in < out ? in : out;
}
static void draw_banner(uint16_t *px, unsigned page, unsigned page_ms, unsigned now)
{
    unsigned alpha = fade_alpha(page_ms);
    int drop = (int)(256U - alpha) / 40;
    if (page == 0) {
        static const uint32_t code[4] = {0x65B0, 0x5A5A, 0x5FEB, 0x4E50};
        draw_run(px, WEDDING_CJK_GLYPHS, WEDDING_CJK_COUNT, code, 4, 41 + drop, alpha, now, true, 14, 41);
        uint16_t gold = gfx_rgb565(0xFFD54A);
        if (alpha > 200U) {
            gfx_hline(px, 34, 72, 49, gold); gfx_hline(px, 88, 126, 49, gold);
            heart(px, 75, 44 + sin256(now / 9U) / 128);
            star(px, 28, 49, 3, 0xFFF0B0, 230); star(px, 132, 49, 3, 0xFFF0B0, 230);
        }
    } else {
        static const uint32_t happy[5] = {'H', 'a', 'p', 'p', 'y'}, wedding[7] = {'W', 'e', 'd', 'd', 'i', 'n', 'g'};
        draw_run(px, WEDDING_LATIN_GLYPHS, WEDDING_LATIN_COUNT, happy, 5, 26 + drop, alpha, now, true, 8, 26);
        draw_run(px, WEDDING_LATIN_GLYPHS, WEDDING_LATIN_COUNT, wedding, 7, 53 + drop, alpha, now, true, 35, 53);
        if (alpha > 200U) {
            rings(px, 21, 21);
            heart(px, 134, 14 + sin256(now / 8U) / 100);
            heart(px, 8, 42 + sin256(now / 8U + 90U) / 100);
            heart(px, 142, 43 + sin256(now / 8U + 40U) / 100);
        }
    }
}
static void draw_scene(uint16_t *px, uint32_t rel)
{
    unsigned page_index = rel / PAGE_MS, page_ms = rel % PAGE_MS;
    draw_background(px);
    draw_sparkles(px, rel);
    draw_hanging_ribbons(px, rel);
    draw_rain(px, rel);
    popper(px, page_index > 0U ? page_index - 1U : 0U, page_index > 0U ? (int)(rel - (page_index - 1U) * PAGE_MS) - POP_DELAY_MS : -1);
    popper(px, page_index, (int)page_ms - POP_DELAY_MS);
    draw_banner(px, page_index & 1U, page_ms, rel);
}

/* ---- app --------------------------------------------------------------------------------------------------- */
static void build(state_t *s, lv_obj_t *root)
{
    s->plot = app_ui_canvas(root, 0, 0, APP_CANVAS_W, APP_CANVAS_H);
    s->band = app_ui_band(root, 0x3A0614, 0xFFC93C);
    s->wish = app_ui_label(s->band, &aihook_font_12, 4, 1, 152, 14, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(s->wish, app_ui_color(0xFFE9A8), LV_PART_MAIN);
    app_ui_text(s->wish, s->text);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    s->entered_ms = ctx->now_ms;
    snprintf(s->text, sizeof s->text, "%s", DEFAULT_WISH);
    app_critical_enter(); s_status = (status_t){true, 0, 0}; app_critical_exit();
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
    if (e->kind == APP_EVENT_SHORT) app_menu(ctx);
    if (e->kind == APP_EVENT_MESSAGE && e->text && s->wish) { /* {"text":"..."}: a personal blessing; empty restores ours */
        cJSON *json = cJSON_Parse(e->text);
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(json, "text");
        if (cJSON_IsString(text) && strlen(text->valuestring) < sizeof s->text && robot_ui_valid_utf8(text->valuestring)) {
            snprintf(s->text, sizeof s->text, "%s", text->valuestring[0] ? text->valuestring : DEFAULT_WISH);
            app_ui_text(s->wish, s->text);
        }
        cJSON_Delete(json);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    uint32_t rel = (uint32_t)(ctx->now_ms - s->entered_ms);
    draw_scene(app_ui_pixels(), rel);
    app_ui_canvas_refresh(s->plot);
    app_critical_enter(); s_status.page = (rel / PAGE_MS) & 1U; ++s_status.frames; app_critical_exit();
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot wedding status")) return false;
    status_t v;
    app_shared_copy(v, s_status);
    (void)app_command_replyf("active=%u page=%u frames=%lu", v.active ? 1U : 0U, v.page, (unsigned long)v.frames);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    const uint32_t box = selected ? 0xFF5C86 : 0xD83A62, band = selected ? 0xFFE27A : 0xFFC93C;
    app_ui_rect(parent, x + 2, y + 10, 24, 16, box, 3);       /* gift box */
    app_ui_rect(parent, x + 12, y + 10, 4, 16, band, 0);      /* ribbon down the middle */
    app_ui_rect(parent, x + 2, y + 16, 24, 3, band, 0);
    app_ui_rect(parent, x + 7, y + 4, 8, 6, band, 3);         /* bow */
    app_ui_rect(parent, x + 13, y + 4, 8, 6, band, 3);
    app_ui_rect(parent, x + 12, y + 6, 4, 4, 0xFF9AB5, 2);    /* knot */
    app_ui_rect(parent, x + 1, y + 2, 2, 2, 0x58D6E0, 0);     /* confetti */
    app_ui_rect(parent, x + 24, y + 3, 2, 2, 0xB38CFF, 0);
    app_ui_rect(parent, x + 25, y + 9, 2, 2, 0x6FE0A0, 0);
}
const app_descriptor_t esphook_app_wedding = {.api_version = APP_API_VERSION, .id = "wedding", .title = "新婚快乐",
    .flags = APP_STANDBY | APP_DEFAULT_STANDBY, .refresh_ms = 40, .state_size = sizeof(state_t), .on_enter = enter,
    .on_leave = leave, .on_event = event, .render = render, .icon = icon, .command = command};
