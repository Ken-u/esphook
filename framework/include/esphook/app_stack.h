#pragma once
/* Slice stacking ("sprite stacking"): a 3D-looking object built from many thin 2D pixel slices.
 *
 * A model is a stack of `h` layers (bottom first); every layer is a w x d map of palette letters ('.' is empty), so
 * a model is just ASCII art you can read and edit in the source. Drawing puts the layers on top of each other, one
 * layer step higher each, and turns the whole stack around the vertical axis: seen from a little above, the pile of
 * flat slices reads as a solid, chunky voxel object that you can spin, tilt, lean, squash, build up from nothing
 * and cast a shadow with - on the 160x64 canvas, with integer maths only and no allocation.
 *
 *   static const char *const BOX[] = {"ss" "ss", "ss" "ss", "sk" "ks"};     // 3 layers of 2 x 2
 *   static const stack_model_t MODEL = {.layers = BOX, .keys = "sk", .colors = PAL, .w = 2, .d = 2, .h = 3};
 *   stack_view_t v; stack_view_init(&v);
 *   v.x = 80; v.y = 50; v.scale = 3 * 256; v.yaw = now / 12; stack_view_pitch(&v, 20);
 *   stack_shadow(px, &MODEL, &v, 0x000000, 90);
 *   stack_draw(px, &MODEL, &v);
 *
 * Conventions: the voxel at column u, row `r` of layer l is at x = u, depth = r, height = l. Row 0 is the far side
 * and the last row the front when yaw is 0 (so a layer reads like a map seen from above, front at the bottom).
 * Yaw is 0..255 for a full turn; a positive yaw turns the front towards the viewer's right. scale and the other
 * `8.8` values are fixed point (256 = 1.0). Colors are 0xRRGGBB. Everything is clipped to APP_CANVAS_W x
 * APP_CANVAS_H; call it from the UI task only (it writes the shared canvas). Pure C, no LVGL: host tests compile it.
 *
 * Cameras: a fixed elevation (chase/top camera) is stack_view_pitch(); a first-person camera looking at things that
 * stand on the ground is stack_view_world(), which also gives true perspective between the layers (slices above the
 * eye show their underside, those below it their top). Costs about one small rectangle fill per visible voxel;
 * stack_draw() returns that count so a scene can keep its budget (see docs/stack-render.md). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "app_canvas.h"
#include "app_gfx.h"
#include "app_math.h"

#define STACK_MAX_KEYS 14   /* palette letters per model */
#define STACK_DROP_LAYERS 4 /* while building up, a layer falls this many layer steps into place */

typedef struct {
    const char *const *layers; /* h strings, bottom layer first; each has w*d characters, d rows of w */
    const char *const *alt;    /* optional: alt_n layers that replace the lowest layers when view.step is odd (a gait) */
    const char *keys;          /* palette letters; the n-th letter is painted with colors[n] */
    const uint32_t *colors;
    uint8_t w, d, h, alt_n;
    uint8_t lit;               /* the last `lit` letters of `keys` glow: no height shading, top light or outline (eyes, lamps) */
} stack_model_t;

typedef struct {
    int x, y;                  /* where the middle of the foot print touches the canvas */
    int scale;                 /* pixels per voxel edge, 8.8 */
    unsigned yaw;              /* 0..255 for a full turn about the vertical axis */
    int ky0, ky_step;          /* screen rows per voxel of depth, 8.8, at layer 0 and per layer up: ky = ky0 + ky_step * layer.
                                * positive = seen from above (the top shows), negative = from below. see stack_view_* */
    unsigned squash;           /* 8.8 factor on the layer step (the cosine of the elevation); below 256 squashes the model */
    unsigned build;            /* 0..256: how much of the stack has been put together (256 = all); layers drop in from above */
    int lean;                  /* sideways shift per layer, 8.8 px (a falling or leaning body) */
    uint32_t tint;             /* mist or hit flash... */
    unsigned tint_amt;         /* ...mixed over every color, 0..256 */
    unsigned dark;             /* 0..256 towards black, after the tint */
    uint32_t light;            /* the color the top faces are lit with */
    unsigned top_light;        /* 0..256: how much the top faces lean to `light` */
    unsigned shade;            /* 0..256: how much darker the lowest layers are than the highest (ambient occlusion) */
    unsigned edge;             /* 0..256: outline voxels (open to the side) lean to black; 0 = off, and faster */
    const uint32_t *palette;   /* optional replacement of the model's colors (same number), e.g. a shirt color per zombie */
    uint8_t step;              /* the gait frame: odd uses model.alt for the lowest layers */
    uint8_t mirror;            /* nonzero mirrors the model left to right */
} stack_view_t;

static inline void stack_view_init(stack_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->scale = 256;
    v->squash = 256;
    v->build = 256;
    v->light = 0xFFFFFF;
    v->top_light = 70;
    v->shade = 80;
}
/* The camera helpers below set only the camera fields of a view that stack_view_init() has prepared.
 * A fixed camera elevation: `pitch` is 0 (level, side on) .. 64 (straight down), in 1/256ths of a turn. */
static inline void stack_view_pitch(stack_view_t *v, unsigned pitch)
{
    if (pitch > 64U) pitch = 64U;
    v->ky0 = app_sin256(pitch);
    v->ky_step = 0;
    v->squash = (unsigned)app_sin256(pitch + 64U);
}
/* A first-person camera and something standing on the ground at distance `z`: the voxel edge is `voxel` world units,
 * the eye is `eye` units above the ground and `foc` is the focal length in pixels per unit at z = 1 (the same numbers a
 * flat sprite uses: height_px = height * foc / z). x and foot_y are where its foot touches the screen. */
static inline void stack_view_world(stack_view_t *v, int x, int foot_y, int z, int voxel, int eye, int foc)
{
    if (z < 1) z = 1;
    v->x = x;
    v->y = foot_y;
    v->scale = voxel * foc * 256 / z;
    v->ky0 = eye * 256 / z;
    v->ky_step = -voxel * 256 / z;
    v->squash = 65536U / gfx_isqrt(65536U + (uint32_t)(v->ky0 * v->ky0));
}

/* The model is well formed: every layer is w*d characters of palette letters or '.', alt layers are as well, and the
 * lowest layer is not empty. Models are constant data, so this is for tests and for asserts while authoring. */
static inline bool stack_model_valid(const stack_model_t *m)
{
    if (!m || !m->layers || !m->keys || !m->colors || !m->w || !m->d || !m->h || m->alt_n > m->h) return false;
    const size_t keys = strlen(m->keys), cells = (size_t)m->w * m->d;
    if (keys == 0 || keys > STACK_MAX_KEYS || m->lit > keys || (m->alt_n && !m->alt)) return false;
    bool ground = false;
    for (unsigned l = 0; l < (unsigned)m->h + m->alt_n; ++l) {
        const char *s = l < m->h ? m->layers[l] : m->alt[l - m->h];
        if (!s || strlen(s) != cells) return false;
        for (size_t i = 0; i < cells; ++i) {
            if (s[i] != '.' && !strchr(m->keys, s[i])) return false;
            if (l == 0 && s[i] != '.') ground = true;
        }
    }
    return ground;
}

static inline const char *stack_layer(const stack_model_t *m, unsigned l, unsigned step)
{
    return (step & 1U) && m->alt && l < m->alt_n ? m->alt[l] : m->layers[l];
}
static inline int stack_iabs(int v) { return v < 0 ? -v : v; }

/* The rectangle (inclusive x0..x1, y0..y1, unclipped) that stack_draw() paints inside, rounded outwards: for culling
 * and for placing things on it. */
static inline void stack_bounds(const stack_model_t *m, const stack_view_t *v, int *x0, int *y0, int *x1, int *y1)
{
    const int c = app_sin256(v->yaw + 64U), s = app_sin256(v->yaw), sc = v->scale > 1 ? v->scale : 1;
    const int cs = stack_iabs(c) + stack_iabs(s);
    const int hw = ((stack_iabs(c) * m->w + stack_iabs(s) * m->d) * sc >> 17) + ((sc * cs) >> 16) + 2;
    const int hd = (stack_iabs(s) * m->w + stack_iabs(c) * m->d) * sc >> 17;
    const int ky_hi = stack_iabs(v->ky0), ky_lo = stack_iabs(v->ky0 + v->ky_step * (m->h - 1));
    const int ky = ky_hi > ky_lo ? ky_hi : ky_lo;
    const int lh = (int)(((int64_t)sc * v->squash) >> 8);
    const int rise = (int)m->h + (v->build < 256U ? STACK_DROP_LAYERS : 0);
    const int lean = stack_iabs(v->lean) * (int)m->h >> 8;
    const int depth = (hd * ky >> 8) + ((sc * cs >> 8) * ky >> 17) + 2;
    *x0 = v->x - hw - lean;
    *x1 = v->x + hw + lean;
    *y0 = v->y - (int)(((int64_t)lh * rise) >> 8) - depth - 2;
    *y1 = v->y + depth + 2 + ((lh * 3) >> 8) + 1;
}

/* A soft shadow on the ground under the model, as wide as its foot print turned by the yaw. */
static inline void stack_shadow(uint16_t *px, const stack_model_t *m, const stack_view_t *v, uint32_t rgb, unsigned alpha)
{
    const int c = app_sin256(v->yaw + 64U), s = app_sin256(v->yaw), sc = v->scale > 1 ? v->scale : 1;
    int ky = stack_iabs(v->ky0);
    if (ky < 56) ky = 56; /* a level camera still sees a flat patch of ground */
    const int rx = ((stack_iabs(c) * m->w + stack_iabs(s) * m->d) * sc >> 17) + 1;
    const int ry = ((stack_iabs(s) * m->w + stack_iabs(c) * m->d) * sc >> 17) * ky >> 8;
    gfx_tint_ellipse(px, v->x, v->y, rx, ry + 1, rgb, alpha);
}

/* Where the middle of the voxel at column `col`, row `r` of layer `l` is on the canvas, under the same view stack_draw()
 * uses (muzzle flashes, glints and sparks go there). The build-up animation is not taken into account. */
static inline void stack_project(const stack_model_t *m, const stack_view_t *v, int col, int r, int l, int *sx, int *sy)
{
    const int w = m->w, d = m->d, sc = v->scale > 1 ? v->scale : 1;
    const int c = app_sin256(v->yaw + 64U), s = app_sin256(v->yaw);
    const int u = v->mirror ? w - 1 - col : col;
    const int lh = (int)(((int64_t)sc * v->squash) >> 8);
    const int ky = v->ky0 + v->ky_step * l;
    const int ux = (c * sc) >> 8, uz = (-s * sc) >> 8, rx = (s * sc) >> 8, rz = (c * sc) >> 8;
    const int uy = (uz * ky) >> 8, ry = (rz * ky) >> 8;
    const int ox = v->x * 256 + (int)(((int64_t)v->lean * l)) - ((w - 1) * ux + (d - 1) * rx) / 2;
    const int oy = v->y * 256 - (l + 1) * lh - ((w - 1) * uy + (d - 1) * ry) / 2;
    if (sx) *sx = (ox + u * ux + r * rx + 128) >> 8;
    if (sy) *sy = (oy + u * uy + r * ry + (lh >> 1) + 128) >> 8;
}

/* Paints the model. Returns how many voxels were drawn (0 when it is off screen or nothing is built yet). */
static inline unsigned stack_draw(uint16_t *px, const stack_model_t *m, const stack_view_t *v)
{
    if (!px || !m || !v || !m->layers || !m->keys || !m->colors || !m->w || !m->d || !m->h) return 0U;
    int bx0, by0, bx1, by1;
    stack_bounds(m, v, &bx0, &by0, &bx1, &by1);
    if (bx1 < 0 || by1 < 0 || bx0 >= APP_CANVAS_W || by0 >= APP_CANVAS_H) return 0U;
    const int w = m->w, d = m->d, h = m->h, sc = v->scale > 1 ? v->scale : 1;
    const int c = app_sin256(v->yaw + 64U), s = app_sin256(v->yaw), cs = stack_iabs(c) + stack_iabs(s);
    const uint32_t *colors = v->palette ? v->palette : m->colors;
    uint8_t index[128]; /* which palette index a letter is */
    memset(index, 0xFF, sizeof index);
    unsigned keys = 0;
    for (; m->keys[keys] && keys < STACK_MAX_KEYS; ++keys) {
        const unsigned char ch = (unsigned char)m->keys[keys];
        if (ch < 128U && index[ch] == 0xFF) index[ch] = (uint8_t)keys;
    }
    uint16_t lut[4][3][STACK_MAX_KEYS] = {{{0}}}; /* RGB565 of each letter in 4 height bands x (side, top, outline), made when first needed */
    unsigned made = 0;
    /* how big a voxel is on the canvas: the layer step (8.8 px) and the footprint of a square turned by the yaw */
    const int lh = (int)(((int64_t)sc * v->squash) >> 8);
    const int stride = sc < 96 ? 3 : sc < 150 ? 2 : 1; /* tiny models skip layers and draw the remaining ones thicker */
    const int bw = (((sc * cs) + 65535) >> 16) > 0 ? (((sc * cs) + 65535) >> 16) : 1;
    const int side_h = (((lh * stride) + 255) >> 8) > 0 ? (((lh * stride) + 255) >> 8) : 1;
    /* the steps from one voxel to the next, 8.8 px: along a layer's columns (u) and rows (r), across the screen and in depth */
    const int ux = (c * sc) >> 8, uz = (-s * sc) >> 8;
    const int rx = (s * sc) >> 8, rz = (c * sc) >> 8;
    const int u_first = uz >= 0 ? 0 : w - 1, u_dir = uz >= 0 ? 1 : -1; /* far to near inside a layer */
    const int r_first = rz >= 0 ? 0 : d - 1, r_dir = rz >= 0 ? 1 : -1;
    /* layers are painted from the one farthest from the eye towards it; the eye is at the layer where ky crosses 0 */
    const int eye_layer = v->ky_step == 0 ? (v->ky0 >= 0 ? 1000 : -1000) : -v->ky0 / v->ky_step;
    const int built = (int)(((unsigned)(h + STACK_DROP_LAYERS) * (v->build > 256U ? 256U : v->build)) >> 8);
    unsigned drawn = 0;
    int lo = 0, hi = h - 1;
    while (lo <= hi) {
        const int l = stack_iabs(lo - eye_layer) >= stack_iabs(hi - eye_layer) ? lo++ : hi--;
        if ((l % stride) != stride - 1 && l != h - 1) continue;
        const int phase = built - l; /* a layer is not there until it is built; the newest ones are still falling into place */
        if (phase <= 0) continue;
        const int fall = phase < STACK_DROP_LAYERS ? (STACK_DROP_LAYERS - phase) * lh : 0;
        const int ky = v->ky0 + v->ky_step * l;
        const char *here = stack_layer(m, (unsigned)l, v->step);
        const char *above = l + 1 < h ? stack_layer(m, (unsigned)l + 1U, v->step) : NULL;
        const int band = l * 4 / h;
        if (!(made & (1U << band))) {
            const unsigned darker = v->shade * (unsigned)(3 - band) / 3U;
            for (unsigned k = 0; k < keys; ++k) {
                const bool glow = k + m->lit >= keys;
                const uint32_t side = glow ? colors[k] : gfx_mix(colors[k], 0x000000, darker);
                const uint32_t out[3] = {side, glow ? side : gfx_mix(side, v->light, v->top_light), glow ? side : gfx_mix(side, 0x000000, v->edge)};
                for (unsigned t = 0; t < 3U; ++t) {
                    uint32_t col = v->tint_amt ? gfx_mix(out[t], v->tint, v->tint_amt) : out[t];
                    if (v->dark) col = gfx_mix(col, 0x000000, v->dark);
                    lut[band][t][k] = gfx_rgb565(col);
                }
            }
            made |= 1U << band;
        }
        /* going one step along the depth also moves down the screen by ky of it */
        const int uy = (uz * ky) >> 8, ry = (rz * ky) >> 8;
        const int top_px = (((((sc * cs) >> 8) * stack_iabs(ky)) + 32768) >> 16); /* rows of top face one voxel shows */
        const int origin_x = v->x * 256 + (int)(((int64_t)v->lean * l)) - ((w - 1) * ux + (d - 1) * rx) / 2;
        const int origin_y = v->y * 256 - (l + 1) * lh - fall - ((w - 1) * uy + (d - 1) * ry) / 2;
        const bool big = bw * (side_h + top_px) >= 6; /* worth the test for a voxel nothing of which shows */
        for (int ri = 0; ri < d; ++ri) {
            const int r = r_first + ri * r_dir;
            for (int ui = 0; ui < w; ++ui) {
                const int u = u_first + ui * u_dir, col = v->mirror ? w - 1 - u : u, cell = r * w + col;
                const char ch = here[cell];
                if (ch == '.' || (unsigned char)ch >= 128U) continue;
                const uint8_t k = index[(unsigned char)ch];
                if (k == 0xFF) continue;
                const int cx = (origin_x + u * ux + r * rx + 128) >> 8, cy = (origin_y + u * uy + r * ry + 128) >> 8;
                if (cx + bw < 0 || cx - bw >= APP_CANVAS_W || cy + side_h + top_px < 0 || cy - top_px >= APP_CANVAS_H) continue;
                const bool open_top = !above || above[cell] == '.';
                bool rim = false;
                if (v->edge || (big && ky >= 0 && !open_top)) {
                    rim = col == 0 || here[cell - 1] == '.' || col == w - 1 || here[cell + 1] == '.' || r == 0 || here[cell - w] == '.' ||
                          r == d - 1 || here[cell + w] == '.';
                    if (big && ky >= 0 && !open_top && !rim) continue; /* covered above and on every side: nothing of it shows */
                }
                const int x0 = cx - bw / 2, y0 = cy - top_px / 2;
                if (top_px > 0) gfx_rect(px, x0, y0, bw, top_px, lut[band][(open_top && ky > 0) ? 1 : (rim && v->edge ? 2 : 0)][k]);
                gfx_rect(px, x0, y0 + top_px, bw, side_h, lut[band][rim && v->edge ? 2 : 0][k]);
                ++drawn;
            }
        }
    }
    return drawn;
}
