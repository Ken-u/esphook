#pragma once
/* A small fish drawn into the shared canvas: shaded body, outline, dorsal fin, tail that wags, gill and eye.
 * One routine and a handful of styles cover the aquarium, the fishing game and any other sea-life scene, so
 * every fish looks like part of the same world. Facing: dir > 0 swims to the right. */
#include "app_gfx.h"

typedef enum { FISH_SLIM, FISH_OVAL, FISH_TALL } fish_shape_t;
typedef enum { FISH_PLAIN, FISH_BANDS, FISH_STRIPE, FISH_PATCHES } fish_pattern_t;
typedef struct {
    uint32_t top, belly, fin, mark, edge; /* 0xRRGGBB; `mark` colours the bands / stripe / patches */
    uint8_t shape, pattern;
    uint8_t dorsal, tail; /* fin sizes in 1/16 of the body length */
} fish_style_t;

/* The tail-to-nose half height of each body shape, 16 samples, 16 = the full height. */
static const uint8_t fish_profile[3][16] = {
    {3, 5, 7, 9, 11, 12, 13, 14, 14, 14, 14, 13, 12, 11, 8, 4},  /* slim */
    {3, 5, 8, 10, 12, 14, 15, 16, 16, 16, 15, 14, 13, 11, 8, 4}, /* oval */
    {2, 4, 7, 10, 13, 15, 16, 16, 16, 16, 15, 14, 12, 10, 7, 3}, /* tall */
};
static const fish_style_t fish_clown = {0xFF7A18, 0xFFA040, 0xFF6A10, 0xFFF6EA, 0x401600, FISH_OVAL, FISH_BANDS, 3, 4};
static const fish_style_t fish_tang = {0x1F63E8, 0x4C92FF, 0x16409E, 0x0C1C48, 0x06143C, FISH_TALL, FISH_STRIPE, 5, 5};
static const fish_style_t fish_angel = {0xF4E070, 0xFFF4B8, 0xE8B838, 0x1E3250, 0x4A3A08, FISH_TALL, FISH_BANDS, 7, 6};
static const fish_style_t fish_tetra = {0x6C98B8, 0xE4EEF6, 0xC8D8E4, 0x26E4FF, 0x2C4458, FISH_OVAL, FISH_STRIPE, 3, 4};
static const fish_style_t fish_koi = {0xFFF2E8, 0xFFFFFF, 0xFFCCB4, 0xE8381C, 0x8A5A48, FISH_OVAL, FISH_PATCHES, 3, 5};
static const fish_style_t fish_crucian = {0x8E9E6E, 0xE2E6CC, 0x76865A, 0x6C7C50, 0x36401E, FISH_OVAL, FISH_PLAIN, 3, 4};
static const fish_style_t fish_bass = {0x587C3A, 0xDCE4B4, 0x44602C, 0x28401A, 0x1A2A10, FISH_SLIM, FISH_STRIPE, 4, 4};
static const fish_style_t fish_carp = {0xE8A020, 0xFFD878, 0xE07414, 0xFFF0B0, 0x6A3C06, FISH_OVAL, FISH_PLAIN, 3, 5};
static const fish_style_t fish_tuna = {0x1E4272, 0xCCDCEC, 0x2A5488, 0xFFD42E, 0x0C1E38, FISH_SLIM, FISH_STRIPE, 4, 6};
static const fish_style_t fish_arowana = {0xF8C440, 0xFFF0A0, 0xE8601C, 0xFF4A22, 0x6A3000, FISH_SLIM, FISH_PATCHES, 5, 5};

static inline void fish_draw(uint16_t *px, int cx, int cy, int len, int dir, int wag, const fish_style_t *st)
{
    if (len < 6) len = 6;
    const int d = dir < 0 ? -1 : 1;
    int hmax = st->shape == FISH_SLIM ? len / 6 + 1 : st->shape == FISH_OVAL ? len * 2 / 7 + 1 : len * 5 / 14 + 1;
    if (hmax < 2) hmax = 2;
    const uint8_t *prof = fish_profile[st->shape];
    const uint16_t edge565 = gfx_rgb565(st->edge);
    const unsigned outline = len < 14 ? 80U : 160U; /* tiny fish keep a soft outline or they turn into wire */
    const int tail_len = len * st->tail / 24 + 2;
    /* tail first (behind the body): a fan that wags, forked on the fast slim swimmers */
    for (int j = 1; j <= tail_len; ++j) {
        const int half = 1 + (hmax * 7 / 8) * j / tail_len, off = wag * j / tail_len;
        const int x = cx + d * (-len / 2 - j + 1);
        for (int dy = -half; dy <= half; ++dy) {
            if (st->shape == FISH_SLIM && j * 10 > tail_len * 6 && dy > -half / 3 && dy < half / 3) continue;
            uint32_t c = gfx_mix(st->fin, st->belly, (unsigned)((dy + half) * 90 / (2 * half + 1)));
            if (dy == -half || dy == half || j == tail_len) c = gfx_mix(c, st->edge, outline - 10U);
            else if ((j + dy) % 3 == 0) c = gfx_mix(c, st->edge, 40); /* fin rays */
            gfx_plot(px, x, cy + off + dy, gfx_rgb565(c));
        }
    }
    /* dorsal fin */
    const int dfin = len * st->dorsal / 16;
    for (int i = len * 3 / 10; i < len * 3 / 10 + len * 4 / 10 && dfin > 0; ++i) {
        const int t16 = i * 16 / len, half = prof[t16 > 15 ? 15 : t16] * hmax / 16;
        const int span = len * 4 / 10, k = i - len * 3 / 10;
        const int up = (k < span / 3 ? k * 3 : (span - k) * 3 / 2) * dfin / span + 1;
        for (int u = 1; u <= up && u <= dfin; ++u) {
            uint32_t c = gfx_mix(st->fin, st->edge, u == up ? 150 : 30);
            gfx_plot(px, cx + d * (i - len / 2), cy - half - u + 1, gfx_rgb565(c));
        }
    }
    /* body */
    for (int i = 0; i < len; ++i) {
        const int t16 = i * 16 / len;
        int half = prof[t16 > 15 ? 15 : t16] * hmax / 16;
        if (half < 1 && i > 0 && i < len - 1) half = 1;
        const int x = cx + d * (i - len / 2), u100 = i * 100 / len;
        for (int dy = -half; dy <= half; ++dy) {
            uint32_t c = gfx_mix(st->top, st->belly, (unsigned)((dy + half) * 256 / (2 * half + 1)));
            switch (st->pattern) {
            case FISH_BANDS:
                if ((u100 >= 56 && u100 < 68) || (u100 >= 26 && u100 < 36) || (st->tail >= 6 && u100 >= 72 && u100 < 78))
                    c = st->mark;
                break;
            case FISH_STRIPE:
                if (st->shape == FISH_TALL ? (dy >= -half / 2 && dy <= -half / 2 + 1 && u100 > 12 && u100 < 80)
                                           : (dy == -half / 3 && u100 > 14 && u100 < 90))
                    c = st->mark;
                break;
            case FISH_PATCHES:
                if (gfx_hash((uint32_t)(i / 3 * 7 + (dy + 12) / 3 * 131)) % 3U == 0U && dy < half && u100 > 8 && u100 < 70)
                    c = st->mark;
                break;
            default: break;
            }
            if (dy == -half + 1 && half > 1) c = gfx_mix(c, 0xFFFFFF, 55); /* light along the back */
            if (dy == -half || dy == half || i == 0 || i == len - 1) c = gfx_mix(c, st->edge, outline);
            gfx_plot(px, x, cy + dy, gfx_rgb565(c));
        }
    }
    /* pectoral fin, gill and eye */
    const int fx = cx + d * (len * 5 / 12 - len / 2) * 1, fy = cy + hmax / 3;
    gfx_plot(px, fx, fy, gfx_rgb565(gfx_mix(st->fin, 0xFFFFFF, 40)));
    gfx_plot(px, fx - d, fy + 1 + (wag > 0), gfx_rgb565(st->fin));
    if (len >= 12) gfx_plot(px, fx - 2 * d, fy + 1 + (wag > 0), gfx_rgb565(st->fin));
    const int gx = cx + d * (len * 11 / 16 - len / 2);
    for (int dy = -hmax / 2; dy <= hmax / 2; ++dy) gfx_tint_plot(px, gx, cy + dy, st->edge, 90);
    const int ex = cx + d * (len * 13 / 16 - len / 2), ey = cy - hmax / 3;
    if (len >= 16) {
        gfx_plot(px, ex, ey, 0xFFFF);
        gfx_plot(px, ex + d, ey, 0xFFFF);
        gfx_plot(px, ex + d, ey + 0, gfx_rgb565(0x101820));
        gfx_plot(px, ex, ey - 1, gfx_rgb565(0xE8F4FF));
    } else {
        gfx_plot(px, ex, ey, gfx_rgb565(0x0A1018));
        gfx_plot(px, ex - d, ey - 1, 0xFFFF);
    }
    gfx_plot(px, cx + d * (len / 2 - 1), cy + 1 + hmax / 5, edge565);
}
