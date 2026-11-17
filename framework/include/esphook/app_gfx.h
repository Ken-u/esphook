#pragma once
/* Small software-drawing kit for apps that paint into the shared RGB565 canvas (app_ui_pixels()).
 * Everything is header-only, clipped to APP_CANVAS_W x APP_CANVAS_H and allocation free, so a game can
 * redraw a whole scene every frame without touching the LVGL heap. Colors are 0xRRGGBB unless a function
 * says RGB565. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "app_ui.h"

static inline uint16_t gfx_rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 19) & 0x1F) << 11 | ((rgb >> 10) & 0x3F) << 5 | ((rgb >> 3) & 0x1F));
}
/* Linear blend of two 0xRRGGBB colors; t is 0 (all a) .. 256 (all b). */
static inline uint32_t gfx_mix(uint32_t a, uint32_t b, unsigned t)
{
    if (t > 256U) t = 256U;
    uint32_t r = (((a >> 16) & 0xFF) * (256U - t) + ((b >> 16) & 0xFF) * t) >> 8;
    uint32_t g = (((a >> 8) & 0xFF) * (256U - t) + ((b >> 8) & 0xFF) * t) >> 8;
    uint32_t bl = ((a & 0xFF) * (256U - t) + (b & 0xFF) * t) >> 8;
    return r << 16 | g << 8 | bl;
}
static inline uint32_t gfx_hash(uint32_t z)
{
    z += 0x9e3779b9U; z ^= z >> 16; z *= 0x85ebca6bU; z ^= z >> 13; z *= 0xc2b2ae35U;
    return z ^ (z >> 16);
}
static inline void gfx_plot(uint16_t *px, int x, int y, uint16_t c)
{
    if ((unsigned)x < (unsigned)APP_CANVAS_W && (unsigned)y < (unsigned)APP_CANVAS_H) px[y * APP_CANVAS_W + x] = c;
}
/* Blends the 0xRRGGBB color `rgb` over an RGB565 pixel; alpha is 0 (untouched) .. 256 (replaced). */
static inline uint16_t gfx_blend565(uint16_t dst, uint32_t rgb, unsigned alpha)
{
    if (alpha > 256U) alpha = 256U;
    unsigned dr = (dst >> 11) & 0x1FU, dg = (dst >> 5) & 0x3FU, db = dst & 0x1FU;
    unsigned sr = (rgb >> 19) & 0x1FU, sg = (rgb >> 10) & 0x3FU, sb = (rgb >> 3) & 0x1FU;
    dr = (dr * (256U - alpha) + sr * alpha) >> 8;
    dg = (dg * (256U - alpha) + sg * alpha) >> 8;
    db = (db * (256U - alpha) + sb * alpha) >> 8;
    return (uint16_t)(dr << 11 | dg << 5 | db);
}
/* Translucent paint: shadows, glows, light beams and highlights. */
static inline void gfx_tint_plot(uint16_t *px, int x, int y, uint32_t rgb, unsigned alpha)
{
    if ((unsigned)x < (unsigned)APP_CANVAS_W && (unsigned)y < (unsigned)APP_CANVAS_H)
        px[y * APP_CANVAS_W + x] = gfx_blend565(px[y * APP_CANVAS_W + x], rgb, alpha);
}
static inline void gfx_tint(uint16_t *px, int x, int y, int w, int h, uint32_t rgb, unsigned alpha)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) gfx_tint_plot(px, xx, yy, rgb, alpha);
}
static inline void gfx_fill(uint16_t *px, uint16_t c)
{
    for (int i = 0; i < APP_CANVAS_W * APP_CANVAS_H; ++i) px[i] = c;
}
static inline void gfx_rect(uint16_t *px, int x, int y, int w, int h, uint16_t c)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > APP_CANVAS_W) x1 = APP_CANVAS_W;
    if (y1 > APP_CANVAS_H) y1 = APP_CANVAS_H;
    for (int yy = y; yy < y1; ++yy)
        for (int xx = x; xx < x1; ++xx) px[yy * APP_CANVAS_W + xx] = c;
}
static inline void gfx_hline(uint16_t *px, int x0, int x1, int y, uint16_t c)
{
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    gfx_rect(px, x0, y, x1 - x0 + 1, 1, c);
}
static inline void gfx_vline(uint16_t *px, int x, int y0, int y1, uint16_t c)
{
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    gfx_rect(px, x, y0, 1, y1 - y0 + 1, c);
}
static inline void gfx_line(uint16_t *px, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
        gfx_plot(px, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e = err * 2;
        if (e > -dy) { err -= dy; x0 += sx; }
        if (e < dx) { err += dx; y0 += sy; }
    }
}
/* A line two pixels thick (the second pixel is offset perpendicular to the dominant axis). */
static inline void gfx_line2(uint16_t *px, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    gfx_line(px, x0, y0, x1, y1, c);
    if (dx >= dy) gfx_line(px, x0, y0 + 1, x1, y1 + 1, c);
    else gfx_line(px, x0 + 1, y0, x1 + 1, y1, c);
}
static inline void gfx_disc(uint16_t *px, int cx, int cy, int r, uint16_t c)
{
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r + r / 2) gfx_plot(px, cx + x, cy + y, c);
}
/* Filled triangle pointing up: apex at (cx, top), base `base_w` wide on row top + h - 1. */
static inline void gfx_peak(uint16_t *px, int cx, int top, int base_w, int h, uint16_t c)
{
    for (int row = 0; row < h; ++row) {
        int half = h > 1 ? (base_w * row) / (2 * (h - 1)) : 0;
        gfx_hline(px, cx - half, cx + half, top + row, c);
    }
}
/* Vertical gradient from `top` to `bottom` (0xRRGGBB) over rows y0..y1-1 with 4x4 ordered dithering, so the
 * 5/6-bit RGB565 steps do not show up as hard bands. */
static inline void gfx_gradient(uint16_t *px, int y0, int y1, uint32_t top, uint32_t bottom)
{
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    int rows = y1 - y0;
    for (int y = y0; y < y1; ++y) {
        if ((unsigned)y >= (unsigned)APP_CANVAS_H) continue;
        uint32_t c = gfx_mix(top, bottom, rows > 1 ? (unsigned)((y - y0) * 256 / (rows - 1)) : 0U);
        int r = (int)((c >> 16) & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)(c & 0xFF);
        for (int x = 0; x < APP_CANVAS_W; ++x) {
            int bias = bayer[y & 3][x & 3];
            int rr = r + bias / 2 > 255 ? 255 : r + bias / 2, gg = g + bias / 4 > 255 ? 255 : g + bias / 4,
                bb = b + bias / 2 > 255 ? 255 : b + bias / 2;
            px[y * APP_CANVAS_W + x] = (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
        }
    }
}

/* Pixel-art sprites: `rows` has h strings of w characters; '.' is transparent and any other character selects a
 * colour through the `keys`/`colors` pair (the n-th key paints colors[n]). Unknown characters are skipped. */
static inline void gfx_blit(uint16_t *px, int w, int h, const char *const *rows, int x, int y, const char *keys,
                            const uint16_t *colors, bool mirror)
{
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            char ch = rows[row][mirror ? w - 1 - col : col];
            if (ch == '.') continue;
            const char *hit = strchr(keys, ch);
            if (hit) gfx_plot(px, x + col, y + row, colors[hit - keys]);
        }
    }
}
/* Mountain ridge: random heights at fixed `spacing` joined linearly and scrolled by `offset` pixels. The silhouette
 * is `min_h`..`min_h + span` rows tall above `base_row`; tall peaks get a two pixel snow cap. */
static inline void gfx_ridge(uint16_t *px, int offset, int spacing, int base_row, int min_h, int span, uint32_t salt,
                             uint32_t body, uint32_t cap)
{
    const uint16_t body565 = gfx_rgb565(body), cap565 = gfx_rgb565(cap);
    for (int c = 0; c < APP_CANVAS_W; ++c) {
        int pos = offset + c, node = pos / spacing, f = pos % spacing;
        if (pos < 0) { node = -((-pos + spacing - 1) / spacing); f = pos - node * spacing; }
        int a = (int)(gfx_hash((uint32_t)node + salt) % (uint32_t)span);
        int b = (int)(gfx_hash((uint32_t)(node + 1) + salt) % (uint32_t)span);
        int h = min_h + a + (b - a) * f / spacing, top = base_row - h;
        if (top < 0) top = 0;
        gfx_vline(px, c, top, base_row + 12, body565);
        gfx_plot(px, c, top, cap565);
        if (h > min_h + span / 2) gfx_plot(px, c, top + 1, cap565);
    }
}
/* Sun (or, with `moon`, a crescent moon) with a soft halo; `sky` is the sky colour around it. */
static inline void gfx_sun(uint16_t *px, int cx, int cy, uint32_t sun, uint32_t sky, uint32_t bite, bool moon)
{
    gfx_disc(px, cx, cy, 8, gfx_rgb565(gfx_mix(sky, sun, 70)));
    gfx_disc(px, cx, cy, 6, gfx_rgb565(gfx_mix(sky, sun, 140)));
    gfx_disc(px, cx, cy, 4, gfx_rgb565(sun));
    if (moon) gfx_disc(px, cx + 2, cy - 1, 3, gfx_rgb565(bite));
}
/* A flat-bottomed puff of cloud, `w` wide and 7 rows tall. */
static inline void gfx_cloud(uint16_t *px, int x, int y, int w, uint16_t body, uint16_t shade)
{
    gfx_rect(px, x + 1, y + 3, w - 2, 3, body);
    gfx_rect(px, x + w / 5, y + 1, w * 3 / 5, 3, body);
    gfx_rect(px, x + w / 3, y, w / 3, 2, body);
    gfx_hline(px, x + 1, x + w - 2, y + 6, shade);
}
