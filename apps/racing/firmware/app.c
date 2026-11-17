#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_haptic.h"
#include "esphook/app_services.h"
#include "esphook/app_steer.h"
#include "esphook/app_ui.h"
#include "esphook/app_command.h"
#include "esphook/app_sync.h"
#include "racing_core.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 极速赛车: a first-person drive on the shared 160x64 canvas, the bottom 16 px a message strip. Hold the key to
 * accelerate, double-tap it for nitro when the tank is full, steer with the radar (or K2/K3). The servo follows the
 * steering wheel once it has been enabled (hold a key for 0.5 s on the title or game over screen; it is never enabled
 * on its own) and gives a short shake in a crash. Game rules live in racing_core.c. */
#define BAND_Y 64
#define EXIT_HOLD_MS 2000U
#define HINT_SLOT_MS 2600U
#define CX 80
#define HY 22            /* the horizon */
#define KY 261000        /* distance in mm of a road row = KY / (row - HY) */
#define DASH_Y 52

typedef struct { int16_t cx[APP_CANVAS_H], hw[APP_CANVAS_H]; int32_t z[APP_CANVAS_H]; } rows_t;
typedef struct {
    rc_t core;
    steer_t steer;
    haptic_t haptic;
    lv_obj_t *plot, *band, *hold, *msg, *score, *tag, *tag_text, *title, *sub;
    uint64_t last_ms, last_up_ms;
    int bg16;
    uint32_t shown_key, shown_score, shown_card;
    uint8_t down_in_race;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "racing state too large");
_Static_assert(offsetof(state_t, core) == 0, "tests pose the scene through the core at the start of the state");
typedef struct {
    bool active;
    uint8_t phase, servo, nitro_ready, src;
    uint32_t speed, dist, score, best, cps, crashes, frames;
    int px, steer;
    unsigned time_s, nitro;
} status_t;
static status_t s_status;

static uint16_t c565(uint32_t rgb) { return gfx_rgb565(rgb); }

/* ---- a tiny 3x5 font for the numbers on the dashboard ------------------------------------------------------------ */
static void glyph(uint16_t *px, int x, int y, int scale, char ch, uint16_t color)
{
    static const char *const font[][5] = {
        {"###", "# #", "# #", "# #", "###"}, {" # ", "## ", " # ", " # ", "###"}, {"###", "  #", "###", "#  ", "###"},
        {"###", "  #", "###", "  #", "###"}, {"# #", "# #", "###", "  #", "  #"}, {"###", "#  ", "###", "  #", "###"},
        {"###", "#  ", "###", "# #", "###"}, {"###", "  #", " # ", " # ", " # "}, {"###", "# #", "###", "# #", "###"},
        {"###", "# #", "###", "  #", "###"}, {"   ", " # ", "###", " # ", "   "}, {"###", "#  ", "# #", "# #", "###"},
    };
    const int index = ch >= '0' && ch <= '9' ? ch - '0' : ch == '+' ? 10 : ch == 'G' ? 11 : ch == 'O' ? 0 : -1;
    if (index < 0) return;
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 3; ++col)
            if (font[index][row][col] == '#') gfx_rect(px, x + col * scale, y + row * scale, scale, scale, color);
}
static int text_width(const char *text, int scale) { return (int)strlen(text) * 4 * scale - scale; }
static void write_text(uint16_t *px, int x, int y, int scale, const char *text, uint32_t color, bool shadow)
{
    for (; *text; ++text, x += 4 * scale) {
        if (shadow) glyph(px, x + 1, y + 1, scale, *text, c565(0x04060E));
        glyph(px, x, y, scale, *text, c565(color));
    }
}

/* ---- the road, row by row ------------------------------------------------------------------------------------ */
static void build_rows(rows_t *rw, const rc_t *r)
{
    int64_t slope = 0; /* in 1e-5 */
    int32_t x = 0, prev = 0;
    for (int y = APP_CANVAS_H - 1; y > HY; --y) {
        const int32_t z = KY / (y - HY), dz = prev ? z - prev : 0;
        if (dz) {
            const int64_t old = slope;
            slope += (int64_t)rc_curve_at(r, r->pos_mm + (prev + z) / 2) * dz / 1000;
            x += (int32_t)((old + slope) / 2 * dz / 100000);
        }
        prev = z;
        rw->z[y] = z;
        rw->hw[y] = (int16_t)((y - HY) * 8 / 3);
        rw->cx[y] = (int16_t)(CX + x * (y - HY) / 1500 - rc_px(r) * ((y - HY) * 8 / 3) / 1000);
    }
}
static uint32_t haze(uint32_t color, int y)
{
    const int far = (APP_CANVAS_H - 1 - y) * 256 / (APP_CANVAS_H - 1 - HY); /* 0 near .. 256 at the horizon */
    return gfx_mix(color, 0xE8B890, (unsigned)(far * far / 256 * 150 / 256));
}
static void draw_road(uint16_t *px, const rows_t *rw, const rc_t *r, unsigned now)
{
    for (int y = APP_CANVAS_H - 1; y > HY; --y) {
        const int32_t world = r->pos_mm + rw->z[y];
        const bool band = (world / 8000) & 1, dash = (world / 4000) & 1;
        const int cx = rw->cx[y], hw = rw->hw[y], fade = y - HY < 5 ? 2 : 0;
        const bool kerb = ((world / 3000) & 1) && !fade;
        gfx_rect(px, 0, y, APP_CANVAS_W, 1, c565(haze(band && !fade ? 0x2F8D3B : 0x3A9E46, y)));
        const int rumble = hw * 112 / 100 + 1;
        gfx_hline(px, cx - rumble, cx + rumble, y, c565(haze(kerb ? 0xD83A2E : 0xF0F0F0, y)));
        gfx_hline(px, cx - hw, cx + hw, y, c565(haze(band && !fade ? 0x585C66 : 0x626672, y)));
        const int e = hw * 94 / 100;
        gfx_plot(px, cx - e, y, c565(haze(0xF4F4F4, y)));
        gfx_plot(px, cx + e, y, c565(haze(0xF4F4F4, y)));
        if (dash && y - HY > 3) { /* the lane lines */
            const int lane = hw * 320 / 1000, w = hw > 28 ? 1 : 0;
            gfx_rect(px, cx - lane - w, y, 1 + w, 1, c565(haze(0xE8E8E0, y)));
            gfx_rect(px, cx + lane, y, 1 + w, 1, c565(haze(0xE8E8E0, y)));
        }
    }
    (void)now;
}

/* ---- the sky and what is on the horizon ---------------------------------------------------------------------- */
static void draw_sky(uint16_t *px, const state_t *s, unsigned now)
{
    gfx_gradient(px, 0, HY + 1, 0x2442A0, 0xF4A468);
    const int bg = s->bg16 / 16;
    gfx_sun(px, 112 + bg / 6, HY - 5, 0xFFE9A8, 0xF6B070, 0, false);
    gfx_cloud(px, (int)((now / 300U + 40U) % 220U) - 40 + bg / 8, 4, 26, c565(0xF8D8C0), c565(0xD8908C));
    gfx_cloud(px, (int)((now / 520U + 150U) % 220U) - 40 + bg / 8, 10, 18, c565(0xF0C0B0), c565(0xC8808C));
    gfx_ridge(px, -bg / 5, 34, HY, 3, 9, 0xA71, 0x6A5C98, 0x9C88B8);
    gfx_ridge(px, -bg / 3, 22, HY, 1, 5, 0x5E3, 0x3F6A5A, 0x3F6A5A);
}

/* ---- things beside and on the road --------------------------------------------------------------------------- */
static void draw_tree(uint16_t *px, int x, int y, int u, uint32_t salt)
{
    const uint32_t h = gfx_hash(salt);
    if (u < 2) { gfx_rect(px, x, y - 3, 2, 3, c565(0x2E6A34)); return; }
    const int trunk_h = u * 9 / 10, r = u;
    const uint16_t green = c565(haze((h & 4U) ? 0x2C7A3A : 0x3A8A44, y)), dark = c565(haze(0x1C5A2C, y)), light = c565(haze(0x66B45A, y));
    gfx_rect(px, x - u / 6, y - trunk_h, u / 3 + 1, trunk_h, c565(haze(0x5A3A22, y)));
    gfx_tint_ellipse(px, x + u / 2, y, u, u / 4 + 1, 0x000000, 70); /* a long shadow across the verge */
    if (h & 1U) { /* a round tree */
        gfx_disc(px, x, y - trunk_h - r + 2, r, green);
        gfx_disc(px, x - r / 3, y - trunk_h - r + 2 - r / 3, r * 2 / 3, light);
        gfx_tint_ellipse(px, x + r / 3, y - trunk_h - r / 2, r * 2 / 3, r / 2, 0x04200C, 90);
    } else { /* a pine */
        for (int k = 0; k < 3; ++k) {
            const int w = r * (4 - k) / 3 + 1, top = y - trunk_h - r * (k + 1) * 3 / 4 - r / 2;
            gfx_tri(px, x, top, x - w, top + r * 3 / 4 + 1, x + w, top + r * 3 / 4 + 1, k & 1 ? dark : green);
        }
    }
}
static void draw_post(uint16_t *px, int x, int y, int u)
{
    if (u < 2) { gfx_vline(px, x, y - 4, y, c565(0x8890A0)); return; }
    gfx_rect(px, x, y - u * 3, u / 4 + 1, u * 3, c565(haze(0x9098A8, y)));
    gfx_rect(px, x - u / 2, y - u * 3, u / 2 + u / 4 + 1, u / 4 + 1, c565(haze(0x9098A8, y)));
    gfx_tint_ellipse(px, x - u / 2, y - u * 3 + 1, u / 2 + 1, u / 2 + 1, 0xFFE8A0, 120);
}
static void draw_gantry(uint16_t *px, int cx, int hw, int y)
{
    const int u = (y - HY) * 2 / 3, x0 = cx - hw * 118 / 100, x1 = cx + hw * 118 / 100, top = y - u * 5;
    gfx_rect(px, x0 - 1, top, 3, y - top, c565(0xD8D8E0));
    gfx_rect(px, x1 - 1, top, 3, y - top, c565(0xD8D8E0));
    for (int x = x0 - 1; x <= x1 + 1; ++x) {
        const bool black = ((x - x0) / 2 + 0) & 1;
        gfx_rect(px, x, top - u / 2, 1, u / 2 + 2, c565(black ? 0x181820 : 0xF4F4F4));
    }
    gfx_rect(px, x0 - 1, top - u / 2 - 1, x1 - x0 + 3, 1, c565(0xE03A2E));
    gfx_rect(px, x0 - 1, top + 1, x1 - x0 + 3, 1, c565(0xE03A2E));
}
static void draw_car(uint16_t *px, int x, int y, int w, const rc_car_t *car, unsigned now)
{
    static const uint32_t body[6] = {0xD8302A, 0x2E6FD8, 0xE8B82A, 0x38A85A, 0xB0B8C8, 0x9A4AC8};
    if (w < 3) {
        gfx_rect(px, x - w, y - w * 3 / 4 - 1, w * 2 + 1, w * 3 / 4 + 1, c565(body[car->color % 6U]));
        gfx_plot(px, x - w, y - 1, c565(0xFF3A3A));
        return;
    }
    const uint32_t b = body[car->color % 6U], dark = gfx_mix(b, 0x000000, 120), glass = 0x16243C;
    const int hw = w, h = w * 3 / 5 + 1, cab = w * 3 / 5;
    gfx_tint_ellipse(px, x, y, hw + 1, h / 5 + 1, 0x000000, 110);                          /* shadow */
    gfx_rect(px, x - hw, y - h * 2 / 3, hw * 2 + 1, h * 2 / 3, c565(haze(b, y)));           /* body */
    gfx_rect(px, x - cab, y - h - h / 4, cab * 2 + 1, h / 3 + h / 4, c565(haze(gfx_mix(b, 0x000000, 40), y))); /* cabin */
    gfx_rect(px, x - cab + 1, y - h - h / 4 + 1, cab * 2 - 1, h / 3 + h / 4 - 1, c565(haze(glass, y)));      /* rear window */
    gfx_hline(px, x - cab + 1, x + cab - 1, y - h - h / 4 + 1, c565(haze(0x4A6890, y)));
    gfx_rect(px, x - hw, y - h * 2 / 3, hw * 2 + 1, 1, c565(haze(gfx_mix(b, 0xFFFFFF, 70), y)));             /* roof line of the boot */
    gfx_rect(px, x - hw, y - h / 3, hw * 2 + 1, h / 3, c565(haze(dark, y)));                                 /* bumper */
    const int lw = w / 3 + 1;
    const bool braking = car->flash || (now / 160U) % 9U < 1U;
    for (int side = -1; side <= 1; side += 2) {
        gfx_rect(px, x + side * (hw - lw) - (side > 0 ? 0 : lw - 1), y - h * 2 / 3 + 1, lw, h / 4 + 1, c565(braking ? 0xFF6A5A : 0xC0201C)); /* tail lights */
        gfx_rect(px, x + side * (hw - w / 6) - w / 8, y - h / 5, w / 4 + 1, h / 5 + 1, c565(0x0C0C10));                            /* wheels */
    }
    gfx_rect(px, x - w / 6, y - h / 3, w / 3 + 1, h / 6 + 1, c565(0xE8E8E0)); /* number plate */
    if (car->flash) gfx_tint(px, x - hw, y - h - h / 4, hw * 2 + 1, h + h / 4, 0xFFFFFF, 120);
}
static void draw_objects(uint16_t *px, const rows_t *rw, const rc_t *r, unsigned now)
{
    /* roadside trees and posts every 18 m, far to near */
    const int32_t slot_mm = 18000;
    const int32_t first = r->pos_mm / slot_mm + 1;
    for (int n = 14; n >= 0; --n) {
        const int32_t index = first + n, dz = index * slot_mm - r->pos_mm;
        const int y = dz > 0 ? HY + KY / dz : 0;
        if (y <= HY + 1 || y >= APP_CANVAS_H) continue;
        const int u = (y - HY) * 2 / 3;
        for (int side = -1; side <= 1; side += 2) {
            const uint32_t salt = (uint32_t)index * 2U + (side > 0 ? 1U : 0U);
            if ((gfx_hash(salt * 7U) & 7U) == 0U) continue; /* a gap in the trees */
            const int off = rw->hw[y] * (1500 + (int)(gfx_hash(salt + 99U) % 700U)) / 1000;
            if (index % 4 == 0) draw_post(px, rw->cx[y] + side * rw->hw[y] * 118 / 100, y, u);
            else draw_tree(px, rw->cx[y] + side * off, y, u * 4 / 3 + 1, salt);
        }
    }
    const int32_t cp = r->next_cp_mm - r->pos_mm; /* the checkpoint gantry */
    if (r->phase == RC_RACE && cp > 6000 && cp < 250000) {
        const int y = HY + KY / cp;
        if (y > HY + 1 && y < APP_CANVAS_H) draw_gantry(px, rw->cx[y], rw->hw[y], y);
    }
    uint8_t order[RC_CARS];
    unsigned count = 0;
    for (unsigned i = 0; i < RC_CARS; ++i)
        if (r->car[i].alive && r->car[i].pos_mm > r->pos_mm + 6500) order[count++] = (uint8_t)i;
    for (unsigned i = 1; i < count; ++i) { /* far to near */
        const uint8_t k = order[i];
        unsigned j = i;
        for (; j > 0 && r->car[order[j - 1]].pos_mm < r->car[k].pos_mm; --j) order[j] = order[j - 1];
        order[j] = k;
    }
    for (unsigned i = 0; i < count; ++i) {
        const rc_car_t *c = &r->car[order[i]];
        const int32_t dz = c->pos_mm - r->pos_mm;
        const int y = HY + KY / dz;
        if (y <= HY + 1 || y >= APP_CANVAS_H) continue;
        const int hw = rw->hw[y];
        draw_car(px, rw->cx[y] + c->px * hw / 1000, y, hw * 18 / 40 + 1, c, now);
    }
}

/* ---- speed, nitro and impact --------------------------------------------------------------------------------- */
static void draw_streaks(uint16_t *px, const rc_t *r, unsigned now)
{
    const int kmh = (int)rc_speed_kmh(r);
    if (kmh < 120 && r->nitro_ms <= 0) return;
    const bool nitro = r->nitro_ms > 0;
    for (unsigned i = 0; i < 16U; ++i) {
        const uint32_t h = gfx_hash(i * 977U + 5U);
        const int side = (h & 1U) ? 1 : -1, start = 24 + (int)((now * (nitro ? 5U : 3U) + (h >> 8)) % 80U);
        const int dx = side * (start + (int)(h >> 4) % 6), len = 4 + (kmh - 100) / 18 + (nitro ? 6 : 0);
        const int dy = (int)((h >> 12) % 30U) - 8;
        const int x0 = CX + dx, y0 = HY + 2 + dy * start / 60, x1 = CX + dx + side * len, y1 = HY + 2 + dy * (start + len) / 60;
        if (y0 < 0 || y0 > DASH_Y - 2) continue;
        gfx_line(px, x0, y0, x1, y1, c565(nitro ? 0xBFE8FF : 0xD8D0D0));
    }
}
static void draw_fx(uint16_t *px, const rc_t *r, unsigned now)
{
    if (r->nitro_ms > 0) { /* blue flames at the edges of the view */
        const unsigned a = 70U + (unsigned)(gfx_sin256(now / 5U) + 256) / 8U;
        gfx_tint(px, 0, 0, 7, DASH_Y, 0x40A0FF, a);
        gfx_tint(px, APP_CANVAS_W - 7, 0, 7, DASH_Y, 0x40A0FF, a);
        gfx_tint(px, 0, 0, APP_CANVAS_W, 5, 0x60C0FF, a / 2U);
    }
    if (r->crash_age_ms < 450U) { /* a red flush and sparks when the car is hit */
        const unsigned a = (450U - r->crash_age_ms) * 160U / 450U;
        gfx_tint(px, 0, 0, APP_CANVAS_W, DASH_Y, 0xFF2A1A, a / 3U);
        gfx_tint(px, 0, 0, 5, DASH_Y, 0xFF2A1A, a);
        gfx_tint(px, APP_CANVAS_W - 5, 0, 5, DASH_Y, 0xFF2A1A, a);
        for (unsigned k = 0; k < 10U; ++k) {
            const uint32_t h = gfx_hash(k * 31U + 7U);
            const int t = (int)r->crash_age_ms / 30, x = CX + (int)(h % 120U) - 60 + ((int)(h >> 8) % 9 - 4) * t / 3, y = DASH_Y - 4 - t * ((int)(h >> 16) % 4);
            gfx_plot(px, x, y, c565(t < 6 ? 0xFFF0A0 : 0xFF9030));
        }
    }
}

/* ---- cockpit ------------------------------------------------------------------------------------------------- */
static void draw_wheel(uint16_t *px, int steer)
{
    const int cx = CX, cy = 69, r = 17;
    const unsigned turn = (unsigned)((steer * 64 / 256 * 70 / 90 + 256) & 255); /* up to +-70 degrees */
    for (int a = 0; a < 256; a += 2) { /* the rim */
        const int x = cx + gfx_sin256((unsigned)a + 64U) * r / 256, y = cy + gfx_sin256((unsigned)a) * r / 256;
        gfx_disc(px, x, y, 1, c565(a > 64 && a < 192 ? 0x1C202C : 0x30384C));
    }
    for (int k = 0; k < 3; ++k) { /* spokes */
        const unsigned phase = turn + (unsigned)(k == 0 ? 64 : k == 1 ? 192 : 128);
        const int x = cx + gfx_sin256(phase) * (r - 1) / 256, y = cy - gfx_sin256(phase + 64U) * (r - 1) / 256;
        gfx_line2(px, cx, cy, x, y, c565(0x242836));
    }
    gfx_disc(px, cx, cy, 4, c565(0x3C4660));
    gfx_disc(px, cx, cy, 2, c565(0x7A88B0));
    const int mx = cx + gfx_sin256(turn) * (r + 1) / 256, my = cy - gfx_sin256(turn + 64U) * (r + 1) / 256; /* the 12 o'clock mark */
    gfx_rect(px, mx - 1, my - 1, 3, 3, c565(0xFFD84A));
}
static void panel(uint16_t *px, int x, int y, int w, int h)
{
    gfx_rect(px, x, y, w, h, c565(0x3A425E));
    gfx_rect(px, x + 1, y + 1, w - 2, h - 2, c565(0x04060C));
    gfx_plot(px, x, y, c565(0x12141E));
    gfx_plot(px, x + w - 1, y, c565(0x12141E));
}
static void draw_cockpit(uint16_t *px, const state_t *s, unsigned now)
{
    const rc_t *r = &s->core;
    gfx_rect(px, 0, 0, APP_CANVAS_W, 2, c565(0x12141E));
    gfx_hline(px, 0, APP_CANVAS_W - 1, 2, c565(0x30364C));
    for (int side = 0; side < 2; ++side) {
        const int x = side ? APP_CANVAS_W - 1 : 0, dir = side ? -1 : 1;
        gfx_tri(px, x, 0, x + dir * 5, 0, x + dir * 13, DASH_Y, c565(0x0C0E16));
        gfx_tri(px, x, 0, x + dir * 13, DASH_Y, x, DASH_Y, c565(0x0C0E16));
        gfx_line(px, x + dir * 5, 0, x + dir * 13, DASH_Y, c565(0x424A66));
    }
    gfx_ellipse(px, CX, 60, 78, 9, c565(0x3A425A)); /* the dashboard: a lit edge over a dark body */
    gfx_ellipse(px, CX, 61, 77, 9, c565(0x12141E));
    gfx_rect(px, 0, 61, APP_CANVAS_W, 3, c565(0x0C0E16));
    /* rear-view mirror: the road behind, the nearest car as a dot */
    gfx_rect(px, CX - 11, 2, 23, 8, c565(0x20243A));
    gfx_rect(px, CX - 10, 3, 21, 6, c565(0x5A7098));
    gfx_rect(px, CX - 10, 6, 21, 3, c565(0x3C4A60));
    gfx_tint(px, CX - 10, 3, 21, 2, 0xFFFFFF, 60);
    /* speed and nitro on the dash */
    char text[16];
    snprintf(text, sizeof text, "%u", rc_speed_kmh(r));
    const int speed_w = text_width(text, 2);
    panel(px, 11, 52, 36, 12);
    write_text(px, 12 + 34 - 3 - speed_w, 54, 2, text, r->nitro_ms > 0 ? 0x80D8FF : 0xFFC040, false);
    panel(px, 111, 53, 40, 9);
    const int fill = r->nitro * 36 / RC_NITRO_FULL;
    const bool ready = r->nitro >= RC_NITRO_FULL && r->phase == RC_RACE, burning = r->nitro_ms > 0;
    gfx_rect(px, 113, 55, fill, 5, c565(burning ? 0xFFA030 : ready ? (((now / 110U) & 1U) ? 0xFFFFFF : 0x40D8FF) : 0x2090D0));
    gfx_hline(px, 113, 148, 56, c565(burning ? 0xFFE080 : 0x80E0FF));
    for (int i = 1; i < 4; ++i) gfx_vline(px, 113 + i * 9, 55, 59, c565(0x04060C));
    gfx_plot(px, 110, 57, c565(ready || burning ? 0x40D8FF : 0x205060)); /* a flame icon */
    gfx_rect(px, 109, 58, 3, 2, c565(ready || burning ? 0x40A0FF : 0x103040));
    draw_wheel(px, s->steer.steer);
    /* time left, top left; red and blinking at the end */
    snprintf(text, sizeof text, "%u", rc_time_s(r));
    const bool low = r->phase == RC_RACE && r->time_ms < 10000U;
    if (!low || ((now / 250U) & 1U)) {
        gfx_rect(px, 5, 4, text_width(text, 2) + 6, 13, c565(0x04060C));
        write_text(px, 8, 6, 2, text, low ? 0xFF5040 : 0xFFFFFF, false);
    }
    if (r->cp_age_ms < 1500U && r->checkpoints) { /* extra time */
        write_text(px, CX - 12, 14, 2, "+24", (now / 100U) & 1U ? 0xFFE040 : 0xFFFFFF, true);
    }
}
static void draw_scene(uint16_t *px, state_t *s, unsigned now)
{
    const rc_t *r = &s->core;
    rows_t rows;
    build_rows(&rows, r);
    int ox = 0, oy = 0;
    if (r->crash_age_ms < 350U) { /* the car shudders */
        const int a = 1 + (int)(350U - r->crash_age_ms) * 3 / 350;
        const uint32_t h = gfx_hash(now / 33U);
        ox = (int)(h % (uint32_t)(a * 2 + 1)) - a;
        oy = (int)((h >> 8) % (uint32_t)(a * 2 + 1)) - a;
    } else if (r->offroad && r->speed16 > 16 * 20 && r->phase == RC_RACE) oy = (int)(now / 40U & 1U);
    draw_sky(px, s, now);
    draw_road(px, &rows, r, now);
    draw_objects(px, &rows, r, now);
    draw_streaks(px, r, now);
    draw_fx(px, r, now);
    if (ox || oy) { /* shake the view: shift the scene, the edge that opens up repeats its neighbour */
        static uint16_t line[APP_CANVAS_W];
        for (int i = 0; i < DASH_Y; ++i) {
            const int y = oy > 0 ? DASH_Y - 1 - i : i; /* never read a row that has already been moved */
            const int sy = y - oy < 0 ? 0 : y - oy >= DASH_Y ? DASH_Y - 1 : y - oy;
            for (int x = 0; x < APP_CANVAS_W; ++x) {
                const int sx = x - ox < 0 ? 0 : x - ox >= APP_CANVAS_W ? APP_CANVAS_W - 1 : x - ox;
                line[x] = px[sy * APP_CANVAS_W + sx];
            }
            memcpy(&px[y * APP_CANVAS_W], line, sizeof line);
        }
    }
    draw_cockpit(px, s, now);
    if (r->phase == RC_COUNT) {
        const unsigned digit = 3U - (r->count_ms * 3U / RC_COUNT_MS > 2U ? 2U : r->count_ms * 3U / RC_COUNT_MS);
        const unsigned into = r->count_ms % (RC_COUNT_MS / 3U); /* each number lands big, then settles */
        const int scale = into < 150U ? 8 : 6;
        char text[2] = {(char)('0' + digit), 0};
        gfx_tint_ellipse(px, CX, 24, 22, 17, 0x000010, 110);
        write_text(px, CX - text_width(text, scale) / 2, 24 - 5 * scale / 2, scale, text, into < 120U ? 0xFFFFFF : 0xFFD84A, true);
    } else if (r->phase == RC_RACE && r->time_ms + 800U > RC_START_TIME_MS) {
        const int w = text_width("GO", 5);
        gfx_tint_ellipse(px, CX, 24, 30, 15, 0x001000, 100);
        write_text(px, CX - w / 2, 24 - 12, 5, "GO", 0x6CFF8A, true);
    }
    if (r->phase == RC_TITLE || r->phase == RC_OVER) {
        gfx_tint(px, 0, 0, APP_CANVAS_W, APP_CANVAS_H, 0x01030C, r->phase == RC_OVER ? 150U : 60U);
        gfx_tint(px, 14, 6, 132, 38, 0x061024, 170);
        gfx_hline(px, 14, 145, 6, c565(0xE0A040));
        gfx_hline(px, 14, 145, 43, c565(0xE0A040));
        gfx_vline(px, 14, 6, 43, c565(0x8A5A20));
        gfx_vline(px, 145, 6, 43, c565(0x8A5A20));
    }
}

/* ---- app ----------------------------------------------------------------------------------------------------- */
static void build(state_t *s, lv_obj_t *root)
{
    s->plot = app_ui_canvas(root, 0, 0, APP_CANVAS_W, APP_CANVAS_H);
    s->band = app_ui_band(root, 0x14100A, 0x9A6A2A);
    s->msg = app_ui_label(s->band, &aihook_font_12, 3, 2, 122, 14, LV_LABEL_LONG_MODE_CLIP);
    s->score = app_ui_label(s->band, &aihook_font_12, 124, 2, 36, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->msg, app_ui_color(0xF0E0C8), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->score, app_ui_color(0xFFD84A), LV_PART_MAIN);
    lv_obj_set_style_text_align(s->score, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s->hold = app_ui_hold_bar(root, BAND_Y, APP_COLOR_ACCENT); /* the hold towards the exit */
    app_ui_hide(s->hold, true);
    s->tag = app_ui_rect(root, 128, 5, 29, 14, 0x2E8B57, 5);
    s->tag_text = app_ui_label(s->tag, &aihook_font_12, 0, 1, 29, 12, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->tag_text);
    app_ui_text(s->tag_text, "舵机");
    s->title = app_ui_label(root, &aihook_font_16, 14, 9, 132, 20, LV_LABEL_LONG_MODE_CLIP);
    s->sub = app_ui_label(root, &aihook_font_12, 14, 28, 132, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(s->title);
    app_ui_center(s->sub);
    lv_obj_set_style_text_color(s->title, app_ui_color(0xFFE6B0), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->sub, app_ui_color(0x9CE8FF), LV_PART_MAIN);
    app_ui_hide(s->tag, true);
    app_ui_hide(s->title, true);
    app_ui_hide(s->sub, true);
    s->shown_key = s->shown_score = s->shown_card = 0xFFFFFFFFU;
}
static void publish(const state_t *s, bool active, bool frame)
{
    const rc_t *r = &s->core;
    const haptic_state_t hs = haptic_state();
    app_critical_enter();
    s_status.active = active; s_status.phase = r->phase; s_status.servo = (uint8_t)hs; s_status.src = s->steer.source;
    s_status.nitro_ready = r->nitro >= RC_NITRO_FULL; s_status.speed = rc_speed_kmh(r); s_status.dist = rc_distance_m(r);
    s_status.score = rc_score(r); s_status.best = r->best; s_status.cps = r->checkpoints; s_status.crashes = r->crashes;
    s_status.px = rc_px(r); s_status.steer = s->steer.steer; s_status.time_s = rc_time_s(r); s_status.nitro = (unsigned)r->nitro;
    if (frame) ++s_status.frames;
    app_critical_exit();
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    rc_init(&s->core, app_random() ^ (uint32_t)ctx->now_ms);
    steer_init(&s->steer);
    haptic_init(&s->haptic);
    s->last_ms = ctx->now_ms;
    app_set_triple_exit(ctx, false); /* the pedal is pumped for nitro: three quick presses are never an exit */
    app_critical_enter(); s_status = (status_t){0}; app_critical_exit();
    publish(s, true, false);
}
static bool idle_screen(const state_t *s)
{
    return s->core.phase == RC_TITLE || (s->core.phase == RC_OVER && s->core.over_age_ms > 800U);
}
/* The way out is a two second hold on the title or game over screen, and only for a press that began there: a pedal
 * held down through a long race must not leave the moment the time runs out. */
static void sync_exit(app_context_t *ctx, const state_t *s)
{
    app_set_exit_hold(ctx, idle_screen(s) ? EXIT_HOLD_MS : 0U);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t now = ctx->now_ms;
    const uint32_t dt = now > s->last_ms ? (uint32_t)(now - s->last_ms) : 0U;
    app_radar_t radar;
    app_radar_read(&radar);
    if (radar.available && radar.online) steer_radar(&s->steer, now, true, radar.frame.targets);
    const int steer = steer_tick(&s->steer, now);
    rc_steer(&s->core, steer);
    sync_exit(ctx, s);
    rc_advance(&s->core, dt);
    s->last_ms = now;
    s->bg16 -= (int)((int64_t)rc_curve_at(&s->core, s->core.pos_mm) * (int)rc_speed_kmh(&s->core) * (int)(dt > 200U ? 200U : dt) / 165000);
    app_servo_t sv;
    app_servo_read(&sv);
    /* the servo is the steering wheel while racing and a shaker in a crash */
    haptic_base(&s->haptic, s->core.phase == RC_RACE ? steer * 140 / 256 : 0);
    while (rc_take_crash(&s->core))
        if (sv.available && sv.armed) haptic_shake(&s->haptic, now, 5, 100, 450, sv.speed_dps);
    haptic_pump(ctx, &s->haptic);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    haptic_stop(&s->haptic); /* the framework disarms the servo it leased to this app */
    if (s->plot) app_ui_canvas_refresh(s->plot); /* The shared canvas goes to the next app. */
    app_critical_enter(); s_status.active = false; app_critical_exit();
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    rc_t *r = &s->core;
    const bool side = e->key == 1U || e->key == 2U; /* K2 steers left, K3 right */
    switch (e->kind) {
    case APP_EVENT_DOWN:
        if (side) steer_key(&s->steer, e->key, true);
        else {
            s->down_in_race = r->phase == RC_COUNT || r->phase == RC_RACE || r->phase == RC_FINISH;
            rc_throttle(r, true);
            /* Nitro is a quick re-press: lift the pedal and put it down again within a fraction of a second. */
            if (s->last_up_ms && e->ms - s->last_up_ms <= 280U) (void)rc_nitro(r);
        }
        break;
    case APP_EVENT_UP:
        if (side) steer_key(&s->steer, e->key, false);
        else {
            rc_throttle(r, false);
            s->last_up_ms = e->ms;
        }
        break;
    case APP_EVENT_SHORT:
        /* A press that began in the race is a pedal, not a "start". */
        if (!side && !s->down_in_race && idle_screen(s)) {
            rc_start(r);
            steer_recentre(&s->steer);
        }
        break;
    case APP_EVENT_CONFIRM:
        /* A deliberate half-second hold on the title or game over screen is the only thing that enables the servo. */
        if (!side && !s->down_in_race && (r->phase == RC_TITLE || r->phase == RC_OVER)) (void)haptic_arm(ctx, &s->haptic);
        break;
    case APP_EVENT_CANCEL:
        steer_release_keys(&s->steer);
        rc_throttle(r, false);
        break;
    default: break;
    }
    sync_exit(ctx, s);
    publish(s, true, false);
}
static void sync_hud(state_t *s, uint64_t now, unsigned hold_ms)
{
    const rc_t *r = &s->core;
    const haptic_state_t hs = haptic_state();
    const char *msg = "";
    const bool bonus = r->phase == RC_RACE && r->cp_age_ms < 1500U && r->checkpoints, ready = r->nitro >= RC_NITRO_FULL;
    switch (r->phase) {
    case RC_TITLE: msg = hs == HAPTIC_READY ? "按键出发 长按启用舵机" : "按键出发 雷达转向"; break;
    case RC_COUNT: msg = "准备…"; break;
    case RC_RACE:
        msg = bonus ? "检查点! 加时24秒" : ready && r->nitro_ms <= 0 ? "氮气就绪 双击按键" : hs == HAPTIC_ARMED ? "按住加速 舵机已启用" : "按住加速 雷达转向";
        break;
    case RC_FINISH: msg = "时间到!"; break;
    default: msg = r->over_age_ms > 800U ? "按键再来" : "时间到!"; break;
    }
    const bool idle = idle_screen(s), exit_slot = idle && (now / HINT_SLOT_MS) % 2U == 1U; /* the hint of the way out alternates */
    if (exit_slot) msg = "按住2秒退出";
    const uint32_t key = (uint32_t)r->phase << 8 | (uint32_t)hs << 4 | (bonus ? 4U : 0U) | (ready && r->nitro_ms <= 0 ? 2U : 0U) |
                         (idle ? 1U : 0U) | (exit_slot ? 8U : 0U);
    if (key != s->shown_key) { app_ui_text(s->msg, msg); s->shown_key = key; }
    const uint32_t score = rc_score(r);
    if (s->shown_score != score) {
        char text[16];
        snprintf(text, sizeof text, "%lu", (unsigned long)score);
        app_ui_text(s->score, text);
        s->shown_score = score;
    }
    app_ui_hold_line(s->hold, hold_ms, hs == HAPTIC_READY, EXIT_HOLD_MS);
    app_ui_hide(s->tag, hs != HAPTIC_ARMED);
    const bool card = r->phase == RC_TITLE || r->phase == RC_OVER;
    app_ui_hide(s->title, !card);
    app_ui_hide(s->sub, !card);
    const uint32_t card_key = r->best * 31U + score + r->phase;
    if (card && s->shown_card != card_key) {
        char text[48];
        app_ui_text(s->title, r->phase == RC_TITLE ? "极速赛车" : "时间到");
        if (r->phase == RC_TITLE) snprintf(text, sizeof text, "最高 %lu", (unsigned long)r->best);
        else snprintf(text, sizeof text, "里程 %lu米 最高 %lu", (unsigned long)rc_distance_m(r), (unsigned long)r->best);
        app_ui_text(s->sub, text);
        s->shown_card = card_key;
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    sync_hud(s, ctx->now_ms, app_exit_hold_progress(ctx));
    draw_scene(app_ui_pixels(), s, (unsigned)ctx->now_ms);
    app_ui_canvas_refresh(s->plot);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot racing status")) return false;
    status_t v;
    app_shared_copy(v, s_status);
    (void)app_command_replyf("active=%u phase=%u speed=%lu px=%d dist=%lu time=%u nitro=%u score=%lu best=%lu cps=%lu crashes=%lu steer=%d src=%u servo=%u frames=%lu",
            v.active ? 1U : 0U, v.phase, (unsigned long)v.speed, v.px, (unsigned long)v.dist, v.time_s, v.nitro, (unsigned long)v.score,
            (unsigned long)v.best, (unsigned long)v.cps, (unsigned long)v.crashes, v.steer, v.src, v.servo, (unsigned long)v.frames);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x + 1, y + 1, 26, 26, selected ? 0xF0A060 : 0xD88850, 5);          /* sky */
    app_ui_rect(parent, x + 1, y + 13, 26, 14, 0x3A9E46, 5);                                /* grass */
    app_ui_rect(parent, x + 1, y + 13, 26, 2, 0x3A9E46, 0);
    app_ui_rect(parent, x + 3, y + 9, 9, 4, 0x6A5C98, 2);                                   /* hills */
    app_ui_rect(parent, x + 17, y + 4, 6, 6, 0xFFE9A8, 3);                                  /* sun */
    app_ui_rect(parent, x + 11, y + 13, 6, 2, 0x626672, 0);                                 /* road */
    app_ui_rect(parent, x + 9, y + 17, 10, 3, 0x626672, 0);
    app_ui_rect(parent, x + 5, y + 21, 18, 6, 0x626672, 2);
    app_ui_rect(parent, x + 13, y + 14, 2, 2, 0xF0F0F0, 0);                                 /* centre line */
    app_ui_rect(parent, x + 13, y + 18, 2, 2, 0xF0F0F0, 0);
    app_ui_rect(parent, x + 13, y + 22, 2, 3, 0xF0F0F0, 0);
    app_ui_rect(parent, x + 15, y + 16, 5, 3, 0xD8302A, 1);                                 /* a car ahead */
    app_ui_rect(parent, x + 16, y + 15, 3, 2, 0x16243C, 0);
}
const app_descriptor_t esphook_app_racing = {.api_version = APP_API_VERSION, .id = "racing", .title = "极速赛车",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
