#include "remote_game.h"
#include <string.h>
#define W 160
#define H 48
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
void stock_chart_draw(uint16_t *p, const remote_game_view_t *v)
{
    memset(p, 0, W * H * sizeof *p);
    if (!v->count || v->count > REMOTE_GAME_WINDOW) return;
    int32_t lo = v->bars[0].low, hi = v->bars[0].high;
    for (unsigned i = 0; i < v->count; ++i) {
        const chart_bar_t *b = &v->bars[i];
        int32_t prices[] = {b->low, b->high, b->ma30, b->ma60};
        for (unsigned k = 0; k < 4; ++k) { if (prices[k] < lo) lo = prices[k]; if (prices[k] > hi) hi = prices[k]; }
    }
    if (hi == lo) ++hi;
    for (int y = 2; y < H; y += 14) line(p, 0, y, 153, y, 0x18E3);
    unsigned origin = v->cursor + 1 - v->count;
    if (v->first >= origin && v->first < origin + v->count) {
        int x = (int)((v->first - origin) * 154 / REMOTE_GAME_WINDOW + 1);
        for (int y = 0; y < H; y += 4) pixel(p, x, y, 0x528A);
    }
    for (unsigned i = 0; i < v->count; ++i) {
        const chart_bar_t *b = &v->bars[i];
        int x = (int)(i * 154 / REMOTE_GAME_WINDOW + 2);
        uint16_t c = b->close >= b->open ? 0xF986 : 0x07D0;
        line(p, x, py(b->high, lo, hi), x, py(b->low, lo, hi), c);
        int top = py(b->open > b->close ? b->open : b->close, lo, hi);
        int bottom = py(b->open < b->close ? b->open : b->close, lo, hi);
        for (int y = top; y <= bottom; ++y) { pixel(p, x - 1, y, c); pixel(p, x, y, c); }
        if (i) {
            int before = (int)((i - 1) * 154 / REMOTE_GAME_WINDOW + 2);
            line(p, before, py(v->bars[i - 1].ma30, lo, hi), x, py(b->ma30, lo, hi), 0xFFE0);
            line(p, before, py(v->bars[i - 1].ma60, lo, hi), x, py(b->ma60, lo, hi), 0x07FF);
        }
    }
    int fill = (int)(v->held_ms * 44 / 500);
    for (int y = 47; y >= 47 - fill; --y) for (int x = 156; x < 160; ++x) pixel(p, x, y, 0xFFE0);
}
