#include "remote_game.h"
#include <string.h>
/* Plain C on purpose: the host tests compile this file without LVGL or any framework header. */
#define W 160
#define H 48
#define RGB565(rgb) ((uint16_t)((((rgb) >> 19) & 0x1F) << 11 | (((rgb) >> 10) & 0x3F) << 5 | (((rgb) >> 3) & 0x1F)))
#define COLOR_UP RGB565(0xFF4D5A)     /* China: red is up */
#define COLOR_DOWN RGB565(0x2CE0A0)
#define COLOR_MA30 RGB565(0xFFFF00)
#define COLOR_MA60 RGB565(0x00FFFF)
#define COLOR_GRID RGB565(0x26335E)
#define COLOR_MARK RGB565(0x5A6BA0)
#define COLOR_TRACK RGB565(0x1B2444)
static void pixel(uint16_t *p, int x, int y, uint16_t c) { if (x >= 0 && x < W && y >= 0 && y < H) p[y * W + x] = c; }
static void line(uint16_t *p, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
        pixel(p, x0, y0, c); if (x0 == x1 && y0 == y1) break;
        int e = err * 2; if (e > -dy) { err -= dy; x0 += sx; } if (e < dx) { err += dx; y0 += sy; }
    }
}
static int py(int32_t price, int32_t lo, int32_t hi) { return 45 - (int)((int64_t)(price - lo) * 43 / (hi - lo)); }
/* Night-blue backdrop, a touch lighter towards the bottom. */
static void backdrop(uint16_t *p)
{
    for (int y = 0; y < H; ++y) {
        unsigned r = 0x0A + (unsigned)(0x16 - 0x0A) * (unsigned)y / (H - 1), g = 0x10 + (unsigned)(0x22 - 0x10) * (unsigned)y / (H - 1),
            b = 0x20 + (unsigned)(0x3F - 0x20) * (unsigned)y / (H - 1);
        uint16_t c = RGB565(r << 16 | g << 8 | b);
        for (int x = 0; x < W; ++x) p[y * W + x] = c;
    }
}
void stock_chart_draw(uint16_t *p, const remote_game_view_t *v)
{
    backdrop(p);
    if (!v->count || v->count > REMOTE_GAME_WINDOW) return;
    int32_t lo = v->bars[0].low, hi = v->bars[0].high;
    for (unsigned i = 0; i < v->count; ++i) {
        const chart_bar_t *b = &v->bars[i];
        int32_t prices[] = {b->low, b->high, b->ma30, b->ma60};
        for (unsigned k = 0; k < 4; ++k) { if (prices[k] < lo) lo = prices[k]; if (prices[k] > hi) hi = prices[k]; }
    }
    if (hi == lo) ++hi;
    for (int y = 2; y < H; y += 14) for (int x = 0; x < 154; x += 3) pixel(p, x, y, COLOR_GRID);
    unsigned origin = v->cursor + 1 - v->count;
    if (v->first >= origin && v->first < origin + v->count) { /* where this run started, with a small flag on top */
        int x = (int)((v->first - origin) * 154 / REMOTE_GAME_WINDOW + 1);
        for (int y = 0; y < H; y += 4) pixel(p, x, y, COLOR_MARK);
        pixel(p, x + 1, 0, COLOR_MARK); pixel(p, x + 2, 1, COLOR_MARK); pixel(p, x + 1, 2, COLOR_MARK); pixel(p, x, 1, COLOR_MARK);
    }
    for (unsigned i = 0; i < v->count; ++i) {
        const chart_bar_t *b = &v->bars[i];
        int x = (int)(i * 154 / REMOTE_GAME_WINDOW + 2);
        uint16_t c = b->close >= b->open ? COLOR_UP : COLOR_DOWN;
        line(p, x, py(b->high, lo, hi), x, py(b->low, lo, hi), c);
        int top = py(b->open > b->close ? b->open : b->close, lo, hi);
        int bottom = py(b->open < b->close ? b->open : b->close, lo, hi);
        for (int y = top; y <= bottom; ++y) { pixel(p, x - 1, y, c); pixel(p, x, y, c); }
    }
    for (unsigned i = 1; i < v->count; ++i) { /* moving averages on top of the candles; the 30 line over the 60 line */
        int x = (int)(i * 154 / REMOTE_GAME_WINDOW + 2), before = (int)((i - 1) * 154 / REMOTE_GAME_WINDOW + 2);
        line(p, before, py(v->bars[i - 1].ma60, lo, hi), x, py(v->bars[i].ma60, lo, hi), COLOR_MA60);
    }
    for (unsigned i = 1; i < v->count; ++i) {
        int x = (int)(i * 154 / REMOTE_GAME_WINDOW + 2), before = (int)((i - 1) * 154 / REMOTE_GAME_WINDOW + 2);
        line(p, before, py(v->bars[i - 1].ma30, lo, hi), x, py(v->bars[i].ma30, lo, hi), COLOR_MA30);
    }
    { /* last price: a dotted guide to the right edge ending in a tag */
        const chart_bar_t *last = &v->bars[v->count - 1];
        int x = (int)((v->count - 1) * 154 / REMOTE_GAME_WINDOW + 2), y = py(last->close, lo, hi);
        uint16_t c = last->close >= last->open ? COLOR_UP : COLOR_DOWN;
        for (int gx = x + 3; gx < 150; gx += 2) pixel(p, gx, y, c);
        for (int ty = -1; ty <= 1; ++ty) for (int tx = 150; tx < 155; ++tx) pixel(p, tx, y + ty, c);
    }
    for (int y = 3; y < H; ++y) for (int x = 156; x < 160; ++x) pixel(p, x, y, COLOR_TRACK); /* long-press gauge */
    int fill = (int)((v->held_ms > 500 ? 500 : v->held_ms) * 44 / 500);
    for (int y = 47; y >= 47 - fill; --y) for (int x = 156; x < 160; ++x) pixel(p, x, y, COLOR_MA30);
}
