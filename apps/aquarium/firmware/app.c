#include "esphook/app.h"
#include "esphook/app_fish.h"
#include "esphook/app_gfx.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include "aquarium_core.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

/* 水族箱: a reef tank on the shared 160x64 canvas, the bottom 16 px the wooden stand with the counters. A hand
 * sweeps back and forth above the water; a press drops a pinch of food where it is. The simulation lives in
 * aquarium_core.c; this file only draws it and turns key presses into feeding. */
#define BAND_Y 64

typedef struct {
    aq_t tank;
    lv_obj_t *plot, *band, *line, *fed, *eaten, *hint;
    lv_image_dsc_t image;
    uint64_t last_ms;
    uint32_t shown_fed, shown_eaten;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "aquarium state too large");
typedef struct { bool active; uint32_t thrown, eaten, frames; unsigned food; } status_t;
static status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static int ang(unsigned now, unsigned div, unsigned phase) { return gfx_sin256(now / div + phase); }

/* ---- the water ------------------------------------------------------------------------------------------------ */
static void draw_water(uint16_t *px, unsigned now)
{
    gfx_gradient(px, 0, AQ_SURFACE - 1, 0x0D1622, 0x1B2B3D); /* the room above the glass */
    gfx_gradient(px, AQ_SURFACE - 1, 30, 0x4CCBE6, 0x1D88BA);
    gfx_gradient(px, 30, APP_CANVAS_H, 0x1D88BA, 0x06224A);
    for (int i = 0; i < 4; ++i) { /* slanting beams of light that drift */
        const int x0 = 16 + i * 42 + ang(now, 22, (unsigned)i * 70U) * 6 / 256;
        const int alpha_base = 20 + ang(now, 30, (unsigned)i * 53U) * 6 / 256;
        for (int y = AQ_SURFACE + 1; y < AQ_FLOOR; ++y) {
            const int w = 5 + (y - AQ_SURFACE) / 4, a = alpha_base - (y - AQ_SURFACE) / 3;
            if (a > 0) gfx_tint(px, x0 + (y - AQ_SURFACE) / 3 - w / 2, y, w, 1, 0xDFF8FF, (unsigned)a);
        }
    }
    for (int x = 0; x < APP_CANVAS_W; ++x) { /* the water line */
        const int s = ang(now, 7, (unsigned)x * 5U);
        gfx_plot(px, x, AQ_SURFACE - 1, gfx_rgb565(s > 150 ? 0xEAFCFF : 0x8FDFF2));
        gfx_tint_plot(px, x, AQ_SURFACE, 0xFFFFFF, s > 100 ? 70U : 34U);
        gfx_tint_plot(px, x, AQ_SURFACE + 1, 0xDFF8FF, 16);
    }
    gfx_hline(px, 0, APP_CANVAS_W - 1, AQ_SURFACE - 2, gfx_rgb565(0x2A3F57)); /* the glass rim */
    gfx_rect(px, 34, 0, 92, 1, gfx_rgb565(0xFFF2CE));                         /* the tank lamp and its glow */
    gfx_tint(px, 30, 1, 100, 3, 0xFFF2CE, 22);
    gfx_tint(px, 44, 4, 72, 3, 0xFFF2CE, 10);
}
static void draw_floor(uint16_t *px, unsigned now)
{
    gfx_ridge(px, 0, 31, AQ_FLOOR + 1, 7, 19, 0x4A1, 0x0F5282, 0x0F5282);  /* far rocks */
    gfx_ridge(px, 13, 19, AQ_FLOOR + 1, 3, 9, 0x77B, 0x0A3C69, 0x0A3C69);  /* nearer rocks, darker */
    gfx_gradient(px, AQ_FLOOR, APP_CANVAS_H, 0xEAD39F, 0xA9885A);
    gfx_hline(px, 0, APP_CANVAS_W - 1, AQ_FLOOR, gfx_rgb565(0xFFF0C0));
    for (unsigned i = 0; i < 26U; ++i) { /* pebbles and grains */
        const uint32_t h = gfx_hash(i * 31U + 7U);
        const int x = (int)(h % 158U), y = AQ_FLOOR + 2 + (int)((h >> 9) % 5U);
        const uint32_t c = (h >> 17) % 3U == 0U ? 0x8B6B44 : (h >> 17) % 3U == 1U ? 0xF6E4B8 : 0xC7A56E;
        gfx_rect(px, x, y, 1 + (int)((h >> 20) % 2U), 1, gfx_rgb565(c));
    }
    for (int x = 0; x < APP_CANVAS_W; x += 2) { /* caustics: light wobbling on the sand */
        const int y = AQ_FLOOR + 1 + (ang(now, 12, (unsigned)x * 7U) + 256) * 3 / 512;
        gfx_tint_plot(px, x, y, 0xFFF6D0, 70);
    }
}
static void draw_kelp(uint16_t *px, int base, int height, unsigned now, unsigned phase)
{
    for (int y = AQ_FLOOR + 1; y > AQ_FLOOR + 1 - height; --y) {
        const int rise = AQ_FLOOR + 1 - y;
        const int x = base + ang(now, 13, (unsigned)(rise * 6) + phase) * (rise / 6 + 1) / 256;
        const bool light = ((rise / 4) & 1) != 0;
        gfx_plot(px, x, y, gfx_rgb565(light ? 0x58C46A : 0x2A9248));
        gfx_plot(px, x + 1, y, gfx_rgb565(light ? 0x2A9248 : 0x1F7A3A));
        if (rise % 9 == 7) gfx_plot(px, x + 2 + (rise & 1), y - 1, gfx_rgb565(0x58C46A)); /* a leaf */
    }
}
static void draw_chest(uint16_t *px, unsigned now)
{
    const int x = AQ_CHEST_X - 9, y = AQ_FLOOR - 10;
    gfx_tint_ellipse(px, AQ_CHEST_X, y + 4, 15, 8, 0xFFD870, (unsigned)(18 + ang(now, 26, 0) / 40)); /* the gold glows */
    gfx_rect(px, x, y + 4, 19, 7, gfx_rgb565(0x7A4A22));
    gfx_rect(px, x, y + 9, 19, 2, gfx_rgb565(0x4E2D12));
    for (int k = 3; k < 19; k += 4) gfx_vline(px, x + k, y + 4, y + 10, gfx_rgb565(0x5E3818));
    gfx_rect(px, x + 3, y + 4, 2, 7, gfx_rgb565(0xE8B838));
    gfx_rect(px, x + 14, y + 4, 2, 7, gfx_rgb565(0xE8B838));
    gfx_rect(px, x + 8, y + 6, 3, 3, gfx_rgb565(0xFFE070));
    gfx_plot(px, x + 9, y + 7, gfx_rgb565(0x5E3818));
    gfx_rect(px, x + 1, y + 2, 17, 2, gfx_rgb565(0xFFE070)); /* coins spilling under the lid */
    gfx_rect(px, x + 2, y, 15, 3, gfx_rgb565(0x8E5A2A));     /* the lid, ajar */
    gfx_hline(px, x + 2, x + 16, y, gfx_rgb565(0xB57A3C));
    gfx_rect(px, x + 3, y, 2, 3, gfx_rgb565(0xE8B838));
    gfx_rect(px, x + 14, y, 2, 3, gfx_rgb565(0xE8B838));
    if (((now / 220U) & 3U) == 0U) gfx_tint(px, x + 5 + (int)((now / 220U) % 9U), y + 1, 1, 1, 0xFFFFFF, 220); /* a glint */
}
static void draw_coral(uint16_t *px, unsigned now)
{
    static const int8_t branch[][4] = {{72, 57, 72, 48}, {72, 53, 66, 45}, {72, 49, 77, 41}, {72, 49, 69, 42}, {66, 45, 63, 39},
                                       {66, 45, 68, 39}, {77, 41, 80, 36}, {77, 41, 75, 35}, {72, 57, 80, 51}, {80, 51, 83, 46},
                                       {72, 57, 64, 52}, {64, 52, 60, 48}, {83, 46, 86, 44}, {60, 48, 57, 47}};
    for (unsigned i = 0; i < sizeof branch / sizeof branch[0]; ++i) {
        gfx_line2(px, branch[i][0], branch[i][1], branch[i][2], branch[i][3], gfx_rgb565(0xD9552D));
        gfx_line(px, branch[i][0], branch[i][1], branch[i][2], branch[i][3], gfx_rgb565(0xF2793F));
        gfx_plot(px, branch[i][2], branch[i][3], gfx_rgb565(i % 3U ? 0xFFC59A : 0xFFE6B8));
    }
    gfx_ellipse(px, 97, AQ_FLOOR - 1, 9, 6, gfx_rgb565(0x3F9F66)); /* a brain coral */
    gfx_ellipse(px, 97, AQ_FLOOR - 2, 8, 4, gfx_rgb565(0x58B97C));
    for (int i = 0; i < 4; ++i) gfx_line(px, 91 + i * 4, AQ_FLOOR - 6, 90 + i * 4 + (i & 1) * 2, AQ_FLOOR - 1, gfx_rgb565(0x2D7A4B));
    gfx_hline(px, 93, 99, AQ_FLOOR - 7, gfx_rgb565(0x90DDA8));
    const int sx = 52, sy = AQ_FLOOR + 3; /* a starfish on the sand */
    static const int8_t arm[5][2] = {{0, -4}, {4, -1}, {2, 3}, {-2, 3}, {-4, -1}};
    for (int i = 0; i < 5; ++i) gfx_line(px, sx, sy, sx + arm[i][0], sy + arm[i][1], gfx_rgb565(0xF28C3A));
    gfx_plot(px, sx, sy, gfx_rgb565(0xFFC070));
    gfx_plot(px, sx, sy - 4 + ((now / 900U) & 1U), gfx_rgb565(0xFFC070));
}
static void draw_anemone(uint16_t *px, unsigned now)
{
    gfx_ellipse(px, AQ_ANEMONE_X, AQ_FLOOR - 1, 8, 3, gfx_rgb565(0x6E2A66));
    gfx_hline(px, AQ_ANEMONE_X - 6, AQ_ANEMONE_X + 6, AQ_FLOOR - 3, gfx_rgb565(0x9A3E8C));
    for (int i = 0; i < 15; ++i) {
        const int bx = AQ_ANEMONE_X - 7 + i, sway = ang(now, 9, (unsigned)i * 23U) * 2 / 256;
        const int h = 9 + (i * 5) % 5, tx = bx + (i - 7) / 2 + sway;
        gfx_line(px, bx, AQ_FLOOR - 3, tx, AQ_FLOOR - 3 - h, gfx_rgb565(i & 1 ? 0xE0488C : 0xC93A7C));
        gfx_plot(px, tx, AQ_FLOOR - 3 - h, gfx_rgb565(0xFFC8E8));
    }
}
static void draw_snow(uint16_t *px, unsigned now)
{
    for (unsigned i = 0; i < 18U; ++i) {
        const uint32_t h = gfx_hash(i * 13U + 99U);
        const int x = (int)(h % 160U) + ang(now, 40, i * 17U) / 90;
        const int y = AQ_SURFACE + 3 + (int)(((h >> 8) % 44U + now * (6U + (h >> 20) % 5U) / 1000U) % 44U);
        gfx_tint_plot(px, x, y, 0xE0F4FF, 56);
    }
}

/* ---- the animals ---------------------------------------------------------------------------------------------- */
static void draw_jelly(uint16_t *px, const aq_fish_t *f)
{
    const int cx = f->x / 256, cy = f->y / 256, p = (int)(f->tail % 220U);
    const int squeeze = p < 50 ? (50 - p) * 256 / 50 : 0;
    const int rx = 6 - squeeze * 2 / 256, ry = 5 + squeeze / 256;
    gfx_tint_ellipse(px, cx, cy, rx + 4, ry + 4, 0xE8B0FF, 26);
    for (int i = 0; i < 6; ++i) { /* tentacles trail behind the bell */
        const int sx = cx - rx + 1 + i * (2 * rx - 2) / 5, len = 9 + (i % 3) * 3;
        for (int k = 0; k < len; ++k)
            gfx_tint_plot(px, sx + gfx_sin256((unsigned)(k * 22 + (int)f->tail * 3 + i * 41)) * (k / 4 + 1) / 512, cy + 2 + k,
                          0xE1A8F6, (unsigned)(190 - k * 11));
    }
    for (int dy = -ry; dy <= 1; ++dy) {
        const int half = rx * (int)gfx_isqrt((uint32_t)(ry * ry - (dy < 0 ? dy * dy : 0))) / ry;
        gfx_tint(px, cx - half, cy + dy, half * 2 + 1, 1, dy < -ry / 2 ? 0xF4D2FF : 0xD596F4, dy == 1 ? 120U : 175U);
    }
    gfx_tint(px, cx - rx, cy + 1, rx * 2 + 1, 1, 0xFFE8FF, 150); /* the bright rim */
    gfx_tint(px, cx - 2, cy - ry / 2, 5, 2, 0xFFFFFF, 70);       /* the organs showing through */
    gfx_tint_plot(px, cx - rx / 2, cy - ry + 1, 0xFFFFFF, 190);
    gfx_tint_plot(px, cx - rx / 2 + 1, cy - ry + 1, 0xFFFFFF, 120);
}
static void draw_crab(uint16_t *px, const aq_fish_t *f)
{
    const int cx = f->x / 256, cy = f->y / 256, d = f->dir, t = (int)f->tail;
    const uint16_t body = gfx_rgb565(0xE04A28), dark = gfx_rgb565(0xA02A14), light = gfx_rgb565(0xFF8860);
    for (int i = 0; i < 3; ++i) { /* legs, walking */
        const int sway = ((t / 5 + i) & 1) ? 1 : 0;
        for (int side = -1; side <= 1; side += 2) {
            gfx_line(px, cx + side * 3, cy + 1, cx + side * (6 + i), cy + 3 - sway + (i == 2), dark);
            gfx_plot(px, cx + side * (6 + i), cy + 3 - sway + (i == 2) + 1, dark);
        }
    }
    gfx_ellipse(px, cx, cy, 5, 3, body);
    gfx_hline(px, cx - 3, cx + 3, cy + 2, dark);
    gfx_hline(px, cx - 3, cx + 2, cy - 2, light);
    for (int side = -1; side <= 1; side += 2) { /* claws */
        const int wave = gfx_sin256((unsigned)(t * 3 + (side > 0 ? 40 : 0))) / 128;
        const int hx = cx + side * 8, hy = cy - 5 + wave;
        gfx_line(px, cx + side * 4, cy - 1, hx, hy + 1, dark);
        gfx_disc(px, hx, hy, 2, body);
        gfx_plot(px, hx + side, hy - 1, dark);
        gfx_plot(px, hx, hy - 1, light);
    }
    gfx_plot(px, cx - 2, cy - 4, 0xFFFF); gfx_plot(px, cx + 2, cy - 4, 0xFFFF); /* eyes on stalks */
    gfx_plot(px, cx - 2, cy - 3, dark); gfx_plot(px, cx + 2, cy - 3, dark);
    gfx_plot(px, cx - 2 + d, cy - 5, gfx_rgb565(0x101820)); gfx_plot(px, cx + 2 + d, cy - 5, gfx_rgb565(0x101820));
}
static const fish_style_t *style_of(unsigned kind)
{
    switch (kind) {
    case AQ_CLOWN: return &fish_clown;
    case AQ_TANG: return &fish_tang;
    case AQ_ANGEL: return &fish_angel;
    default: return &fish_tetra;
    }
}
static void draw_animals(uint16_t *px, const aq_t *a)
{
    for (unsigned pass = 0; pass < 2U; ++pass) /* small fish and the crab behind, the big ones and the jelly in front */
        for (unsigned i = 0; i < AQ_FISH; ++i) {
            const aq_fish_t *f = &a->fish[i];
            const bool big = f->kind == AQ_TANG || f->kind == AQ_ANGEL || f->kind == AQ_JELLY;
            if (big != (pass == 1U)) continue;
            if (f->kind == AQ_CRAB) draw_crab(px, f);
            else if (f->kind == AQ_JELLY) draw_jelly(px, f);
            else {
                const int len = aq_fish_len(f->kind), wag = gfx_sin256(f->tail * 3U) * (f->kind == AQ_TETRA ? 1 : 2) / 256;
                const int y = f->y / 256 + gfx_sin256(f->tail) / 192; /* a little bob in the swim */
                fish_draw(px, f->x / 256, y, len, f->dir, wag, style_of(f->kind));
            }
        }
}
static void draw_food(uint16_t *px, const aq_t *a)
{
    for (unsigned i = 0; i < AQ_FOOD; ++i) {
        const aq_food_t *p = &a->food[i];
        if (!p->alive) continue;
        const int x = aq_food_x(p), y = p->y / 256;
        if (p->landed) {
            const bool fading = p->landed > (AQ_LANDED_MS / AQ_STEP_MS) * 3U / 4U;
            if (fading && ((p->landed / 12U) & 1U)) continue;
            gfx_rect(px, x, y, 2, 1, gfx_rgb565(0xA67C45));
            gfx_plot(px, x, y - 1, gfx_rgb565(0xD4A464));
        } else {
            gfx_tint(px, x - 1, y - 1, 4, 4, 0xFFE0A0, 46); /* a faint glow so it catches the eye */
            gfx_rect(px, x, y, 2, 2, gfx_rgb565(0xE09A48));
            gfx_plot(px, x, y, gfx_rgb565(0xFFE8B0));
            gfx_plot(px, x + 1, y + 1, gfx_rgb565(0xA8662A));
        }
    }
}
static void draw_bubbles(uint16_t *px, const aq_t *a)
{
    for (unsigned i = 0; i < AQ_BUBBLES; ++i) {
        const aq_bubble_t *b = &a->bubble[i];
        if (!b->alive) continue;
        const int y = b->y / 256, x = b->x + gfx_sin256((unsigned)y * 11U + i * 37U) / 200;
        if (b->size == 0U) gfx_tint_plot(px, x, y, 0xE4FAFF, 210);
        else {
            gfx_ring(px, x, y, b->size, 0xE4FAFF, 190);
            gfx_tint_plot(px, x - 1, y - 1, 0xFFFFFF, 240);
        }
    }
}
static void draw_splash(uint16_t *px, const aq_t *a)
{
    const unsigned age = a->since_feed_ms;
    if (age >= 700U) return;
    const int x = a->drop_x;
    for (unsigned k = 0; k < 2U; ++k) { /* two ripples flatten out along the surface */
        if (age < k * 130U) continue;
        const unsigned t = age - k * 130U;
        const int r = 2 + (int)(t / 36U);
        const unsigned alpha = t < 560U ? 190U - t * 190U / 560U : 0U;
        for (int s = -1; s <= 1; s += 2) {
            gfx_tint_plot(px, x + s * r, AQ_SURFACE, 0xFFFFFF, alpha);
            gfx_tint_plot(px, x + s * (r - 1), AQ_SURFACE + 1, 0xFFFFFF, alpha);
            gfx_tint_plot(px, x + s * (r + 1), AQ_SURFACE, 0xFFFFFF, alpha / 2U);
            gfx_tint_plot(px, x + s * (r - 2), AQ_SURFACE + 1, 0xFFFFFF, alpha / 2U);
        }
    }
    if (age < 200U) /* a few droplets */
        for (int s = -1; s <= 1; ++s)
            gfx_tint(px, x + s * (2 + (int)age / 40), AQ_SURFACE - 2 - (int)(age < 100U ? age / 25U : (200U - age) / 25U), 1, 2, 0xFFFFFF, 220U - age);
}
static void draw_hand(uint16_t *px, const aq_t *a)
{
    const int hx = aq_hand_x(a);
    const unsigned since = a->since_feed_ms;
    const bool open = since < 150U, empty = since < 520U;
    const uint16_t skin = gfx_rgb565(0xF4C8A0), shade = gfx_rgb565(0xD4A07A);
    gfx_rect(px, hx - 5, 0, 11, 3, gfx_rgb565(0xDDE6F0));            /* sleeve */
    gfx_hline(px, hx - 5, hx + 5, 3, gfx_rgb565(0x8EA4BC));
    gfx_ellipse(px, hx, 5, 4, 3, skin);                              /* the hand */
    gfx_hline(px, hx - 3, hx + 3, 7, shade);
    gfx_plot(px, hx + 3, 4, shade);
    if (open) { /* fingers open and the pinch is dropped */
        gfx_rect(px, hx - 5, 6, 2, 2, skin);
        gfx_rect(px, hx + 4, 6, 2, 2, skin);
    } else {
        gfx_rect(px, hx - 2, 7, 2, 2, skin);
        gfx_rect(px, hx + 1, 7, 2, 2, skin);
        if (!empty) { /* the next pinch between the fingertips */
            gfx_plot(px, hx - 1, 8, gfx_rgb565(0xC8884A));
            gfx_plot(px, hx, 8, gfx_rgb565(0xD9A060));
            gfx_plot(px, hx + 1, 8, gfx_rgb565(0xC8884A));
        }
    }
}
static void draw_yum(uint16_t *px, const aq_t *a)
{
    const unsigned age = a->since_eat_ms;
    if (age >= 650U) return;
    static const char *heart[4] = {".X.X.", "XXXXX", ".XXX.", "..X.."};
    const int x = a->eat_x - 2, y = a->eat_y - 8 - (int)(age / 90U);
    const unsigned alpha = 255U - age * 255U / 650U;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 5; ++col)
            if (heart[row][col] == 'X') gfx_tint_plot(px, x + col, y + row, row == 0 && col == 1 ? 0xFFC0D4 : 0xFF5E92, alpha);
}
static void draw_scene(uint16_t *px, const aq_t *a, unsigned now)
{
    draw_water(px, now);
    draw_floor(px, now);
    draw_chest(px, now);
    draw_coral(px, now);
    draw_anemone(px, now);
    draw_kelp(px, 7, 30, now, 0);
    draw_kelp(px, 13, 21, now, 90);
    draw_kelp(px, 147, 26, now, 40);
    draw_kelp(px, 153, 33, now, 150);
    draw_snow(px, now);
    draw_animals(px, a);
    draw_bubbles(px, a);
    draw_food(px, a);
    draw_splash(px, a);
    draw_yum(px, a);
    draw_hand(px, a);
}

/* ---- app ------------------------------------------------------------------------------------------------------ */
static void build(state_t *s, lv_obj_t *root)
{
    s->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->image.header.cf = LV_COLOR_FORMAT_RGB565;
    s->image.header.w = APP_CANVAS_W; s->image.header.h = APP_CANVAS_H; s->image.header.stride = APP_CANVAS_W * 2;
    s->image.data_size = APP_CANVAS_W * APP_CANVAS_H * 2; s->image.data = (const uint8_t *)app_ui_pixels();
    s->plot = lv_image_create(root);
    lv_obj_set_pos(s->plot, 0, 0);
    lv_image_set_src(s->plot, &s->image);
    s->band = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, APP_HEIGHT - BAND_Y, 0x2A1B10, 0); /* the wooden stand */
    s->line = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, 1, 0x8A6034, 0);
    app_ui_rect(root, 0, BAND_Y + 1, APP_WIDTH, 1, 0x4A3018, 0);
    s->fed = app_ui_label(s->band, &aihook_font_12, 4, 2, 52, 14, LV_LABEL_LONG_MODE_CLIP);
    s->eaten = app_ui_label(s->band, &aihook_font_12, 58, 2, 54, 14, LV_LABEL_LONG_MODE_CLIP);
    s->hint = app_ui_label(s->band, &aihook_font_12, 109, 2, 51, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->fed, app_ui_color(0xF4D9A4), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->eaten, app_ui_color(0xFFB4C6), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->hint, app_ui_color(0xB5A488), LV_PART_MAIN);
    app_ui_text(s->hint, "按键投食");
}
static void publish(const state_t *s, bool active, bool frame)
{
    portENTER_CRITICAL(&s_lock);
    s_status.active = active; s_status.thrown = s->tank.thrown; s_status.eaten = s->tank.eaten;
    s_status.food = aq_food_live(&s->tank);
    if (frame) ++s_status.frames;
    portEXIT_CRITICAL(&s_lock);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    aq_init(&s->tank, app_random() ^ (uint32_t)ctx->now_ms);
    s->last_ms = ctx->now_ms;
    s->shown_fed = s->shown_eaten = 0xFFFFFFFFU;
    portENTER_CRITICAL(&s_lock); s_status = (status_t){0}; portEXIT_CRITICAL(&s_lock);
    publish(s, true, false);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t dt = ctx->now_ms > s->last_ms ? ctx->now_ms - s->last_ms : 0U;
    s->last_ms = ctx->now_ms;
    aq_advance(&s->tank, dt > AQ_MAX_ADVANCE_MS ? AQ_MAX_ADVANCE_MS : (uint32_t)dt);
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
    if (e->kind == APP_EVENT_DOWN && aq_feed(&s->tank)) publish(s, true, false); /* food falls the moment it is pressed */
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    if (s->shown_fed != s->tank.thrown) {
        char text[24];
        snprintf(text, sizeof text, "投食 %lu", (unsigned long)s->tank.thrown);
        app_ui_text(s->fed, text);
        s->shown_fed = s->tank.thrown;
    }
    if (s->shown_eaten != s->tank.eaten) {
        char text[24];
        snprintf(text, sizeof text, "进食 %lu", (unsigned long)s->tank.eaten);
        app_ui_text(s->eaten, text);
        s->shown_eaten = s->tank.eaten;
    }
    draw_scene(app_ui_pixels(), &s->tank, (unsigned)ctx->now_ms);
    app_ui_plot_refresh(s->plot, &s->image);
    publish(s, true, true);
}
static bool command(const char *line)
{
    if (strcmp(line, "robot aquarium status")) return false;
    status_t v;
    portENTER_CRITICAL(&s_lock); v = s_status; portEXIT_CRITICAL(&s_lock);
    char reply[128];
    snprintf(reply, sizeof reply, "active=%u thrown=%lu eaten=%lu food=%u frames=%lu", v.active ? 1U : 0U, (unsigned long)v.thrown,
             (unsigned long)v.eaten, v.food, (unsigned long)v.frames);
    (void)app_transport_write(reply);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x + 1, y + 3, 26, 22, selected ? 0x3CB6DE : 0x2A8CB8, 4);   /* the tank */
    app_ui_rect(parent, x + 1, y + 3, 26, 3, 0x9FE6F6, 2);                          /* water line */
    app_ui_rect(parent, x + 1, y + 21, 26, 4, 0xE0C68E, 2);                         /* sand */
    app_ui_rect(parent, x + 5, y + 10, 12, 8, 0xFF7A18, 4);                         /* the fish */
    app_ui_rect(parent, x + 15, y + 11, 6, 6, 0xFF9A3C, 2);                         /* its tail */
    app_ui_rect(parent, x + 8, y + 10, 2, 8, 0xFFF4E6, 0);                          /* a white band */
    app_ui_rect(parent, x + 7, y + 12, 2, 2, 0x101820, 1);                          /* eye */
    app_ui_rect(parent, x + 20, y + 8, 3, 3, 0xE4FAFF, 2);                          /* bubbles */
    app_ui_rect(parent, x + 23, y + 12, 2, 2, 0xE4FAFF, 1);
    app_ui_rect(parent, x + 3, y + 14, 2, 8, 0x2E9A4C, 1);                          /* weed */
    app_ui_rect(parent, x + 22, y + 17, 2, 6, 0x2E9A4C, 1);
}
const app_descriptor_t esphook_app_aquarium = {.api_version = APP_API_VERSION, .id = "aquarium", .title = "水族箱",
    .refresh_ms = 33, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
