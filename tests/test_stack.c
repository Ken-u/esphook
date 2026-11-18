/* Host tests for the slice-stacking kit (framework/include/esphook/app_stack.h) and the models the zombie game draws with
 * it: the geometry of a known box, turning, mirroring, the painting order seen from above and below, build-up,
 * the gait layers, level of detail, the bounds, writing near and outside the canvas edges, and the cost of a crowd. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esphook/app_stack.h"
#include "zombie_models.h"

#define W APP_CANVAS_W
#define H APP_CANVAS_H
static uint16_t canvas[W * H];

static uint16_t rgb(uint32_t c) { return gfx_rgb565(c); }
static void clear(void) { memset(canvas, 0, sizeof canvas); }
typedef struct { int x0, y0, x1, y1; unsigned count; } box_t;
static box_t painted(void)
{
    box_t b = {W, H, -1, -1, 0};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (canvas[y * W + x]) {
                if (x < b.x0) b.x0 = x;
                if (y < b.y0) b.y0 = y;
                if (x > b.x1) b.x1 = x;
                if (y > b.y1) b.y1 = y;
                ++b.count;
            }
    return b;
}
static uint32_t checksum(void)
{
    uint32_t h = 2166136261U;
    for (int i = 0; i < W * H; ++i) h = (h ^ canvas[i]) * 16777619U;
    return h;
}
static uint32_t rng_state = 12345U;
static unsigned rnd(unsigned n)
{
    rng_state = rng_state * 1664525U + 1013904223U;
    return (rng_state >> 8) % n;
}

/* a 2 x 2 x 3 block: a, b and c layers, each a single bright color, so every painted pixel is nonzero */
static const char *const BLOCK[] = {"aa" "aa", "bb" "bb", "cc" "cc"};
static const uint32_t BLOCK_COLORS[] = {0xFF2020, 0x20FF20, 0x4040FF};
static const stack_model_t block = {.layers = BLOCK, .keys = "abc", .colors = BLOCK_COLORS, .w = 2, .d = 2, .h = 3};
/* a 4 x 2 x 2 bar, long across and short in depth */
static const char *const BAR[] = {"aaaa" "aaaa", "aaaa" "aaaa"};
static const stack_model_t bar = {.layers = BAR, .keys = "a", .colors = BLOCK_COLORS, .w = 4, .d = 2, .h = 2};
/* red on the left, blue on the right */
static const char *const TWO[] = {"ab"};
static const uint32_t TWO_COLORS[] = {0xFF2020, 0x4040FF};
static const stack_model_t two = {.layers = TWO, .keys = "ab", .colors = TWO_COLORS, .w = 2, .d = 1, .h = 1};
/* one voxel */
static const char *const ONE[] = {"a"};
static const stack_model_t one = {.layers = ONE, .keys = "a", .colors = BLOCK_COLORS, .w = 1, .d = 1, .h = 1};

static stack_view_t plain(int x, int y, int px_per_voxel)
{
    stack_view_t v;
    stack_view_init(&v);
    v.x = x;
    v.y = y;
    v.scale = px_per_voxel * 256;
    v.shade = 0;
    v.top_light = 0;
    return v;
}

static void validity(void)
{
    assert(stack_model_valid(&block) && stack_model_valid(&bar) && stack_model_valid(&two) && stack_model_valid(&one));
    assert(stack_model_valid(&zm_walker) && stack_model_valid(&zm_runner) && stack_model_valid(&zm_brute));
    assert(stack_model_valid(&zm_tomb) && stack_model_valid(&zm_cross) && stack_model_valid(&zm_gun));
    /* the standard zombie palette is what the app indexes */
    assert(strlen(ZM_KEYS) == ZM_COLORS && ZM_LIT == 1 && ZM_KEYS[ZM_SHIRT] == 's' && ZM_KEYS[ZM_SHIRT_DARK] == 't');
    /* a walker is 100 world units tall: 20 voxels of ZM_VOXEL, and half its width is 22 units, the core's half_w */
    assert(zm_walker.h * ZM_VOXEL == 100 && zm_runner.h * ZM_VOXEL == 95 && zm_brute.h * ZM_VOXEL == 130);
    /* malformed models are rejected */
    static const char *const short_layer[] = {"aaa"};
    stack_model_t bad = {.layers = short_layer, .keys = "a", .colors = BLOCK_COLORS, .w = 2, .d = 2, .h = 1};
    assert(!stack_model_valid(&bad));
    static const char *const unknown[] = {"ax" "aa"};
    bad = (stack_model_t){.layers = unknown, .keys = "a", .colors = BLOCK_COLORS, .w = 2, .d = 2, .h = 1};
    assert(!stack_model_valid(&bad));
    static const char *const floating[] = {"...." , "aaaa"};
    bad = (stack_model_t){.layers = floating, .keys = "a", .colors = BLOCK_COLORS, .w = 2, .d = 2, .h = 2};
    assert(!stack_model_valid(&bad)); /* nothing on the ground */
    bad = block;
    bad.alt_n = 2; /* alt layers promised but not there */
    assert(!stack_model_valid(&bad));
    bad = block;
    bad.lit = 9;
    assert(!stack_model_valid(&bad));
    assert(!stack_model_valid(NULL));
}

static void geometry(void)
{
    /* straight on, level camera, 4 px voxels: an exact 8 x 12 px rectangle standing on the foot line */
    clear();
    stack_view_t v = plain(80, 40, 4);
    assert(stack_draw(canvas, &block, &v) == 12U); /* 2 x 2 voxels in each of 3 layers */
    box_t b = painted();
    assert(b.x0 == 76 && b.x1 == 83 && b.y0 == 28 && b.y1 == 39 && b.count == 8U * 12U);
    /* every layer is its own band, bottom first */
    assert(canvas[39 * W + 80] == rgb(0xFF2020) && canvas[35 * W + 80] == rgb(0x20FF20) && canvas[28 * W + 80] == rgb(0x4040FF));
    /* a bigger or smaller scale scales the picture */
    clear();
    v = plain(80, 40, 2);
    (void)stack_draw(canvas, &block, &v);
    b = painted();
    assert(b.x1 - b.x0 + 1 == 4 && b.y1 - b.y0 + 1 == 6);
    /* the bounds always hold the picture, rounded outwards */
    int x0, y0, x1, y1;
    stack_bounds(&block, &v, &x0, &y0, &x1, &y1);
    assert(x0 <= b.x0 && x1 >= b.x1 && y0 <= b.y0 && y1 >= b.y1);
    /* nothing to draw is not an error */
    assert(stack_draw(NULL, &block, &v) == 0U && stack_draw(canvas, NULL, &v) == 0U && stack_draw(canvas, &block, NULL) == 0U);
    /* a world camera with the eye at ground height is a plain flat sprite: z = 500 and 100 units tall is 20 px */
    clear();
    stack_view_world(&v, 80, 50, 500, ZM_VOXEL, 0, 100);
    assert(v.scale == 256 && v.ky0 == 0 && v.squash == 256);
    v.shade = 0;
    (void)stack_draw(canvas, &zm_walker, &v);
    b = painted();
    assert(b.y1 - b.y0 + 1 == 20 && b.y1 == 49);
}

static void turning(void)
{
    /* a full turn is the identity; half a turn shows the same footprint mirrored in depth */
    stack_view_t v = plain(80, 40, 3);
    clear();
    v.yaw = 0;
    (void)stack_draw(canvas, &bar, &v);
    const uint32_t front = checksum();
    const box_t front_box = painted();
    assert(front_box.x1 - front_box.x0 + 1 == 12);
    clear();
    v.yaw = 256;
    (void)stack_draw(canvas, &bar, &v);
    assert(checksum() == front);
    /* a quarter turn presents its short side: 2 voxels across instead of 4 */
    clear();
    v.yaw = 64;
    (void)stack_draw(canvas, &bar, &v);
    box_t side = painted();
    assert(side.x1 - side.x0 + 1 == 6);
    /* in between, the footprint is wider than either (a turned rectangle) at a level camera... */
    clear();
    v.yaw = 32;
    (void)stack_draw(canvas, &bar, &v);
    box_t mid = painted();
    assert(mid.x1 - mid.x0 + 1 >= 8 && mid.x1 - mid.x0 + 1 <= 14);
    /* ...and from above its depth shows: the picture is taller than a level view of the same turn */
    stack_view_t top = v;
    stack_view_pitch(&top, 32);
    clear();
    (void)stack_draw(canvas, &bar, &top);
    box_t above = painted();
    assert(above.y1 - above.y0 > mid.y1 - mid.y0);
    /* there are no holes inside a turned square: a 6 x 6 footprint turned 45 degrees from straight above is solid */
    static const char *const SLAB[] = {"aaaaaa" "aaaaaa" "aaaaaa" "aaaaaa" "aaaaaa" "aaaaaa"};
    static const stack_model_t slab = {.layers = SLAB, .keys = "a", .colors = BLOCK_COLORS, .w = 6, .d = 6, .h = 1};
    for (unsigned yaw = 0; yaw < 256U; yaw += 9U) {
        stack_view_t s = plain(80, 40, 3);
        stack_view_pitch(&s, 64); /* straight down: the layer is a flat plate, ky = 1 */
        s.yaw = yaw;
        clear();
        (void)stack_draw(canvas, &slab, &s);
        const box_t p = painted();
        for (int y = p.y0 + 4; y <= p.y1 - 4; ++y)
            for (int x = 80 - 3; x <= 80 + 3; ++x) assert(canvas[y * W + x] != 0);
    }
}

static void mirroring(void)
{
    stack_view_t v = plain(80, 40, 4);
    clear();
    (void)stack_draw(canvas, &two, &v);
    assert(canvas[39 * W + 77] == rgb(0xFF2020) && canvas[39 * W + 82] == rgb(0x4040FF));
    clear();
    v.mirror = 1;
    (void)stack_draw(canvas, &two, &v);
    assert(canvas[39 * W + 77] == rgb(0x4040FF) && canvas[39 * W + 82] == rgb(0xFF2020));
    /* a mirrored picture is the flat picture flipped about the model's centre line */
    for (const stack_model_t *m = &zm_walker; m != &zm_runner; m = &zm_runner) {
        stack_view_t a = plain(80, 55, 2), b = a;
        a.edge = b.edge = 40;
        a.step = b.step = 1;
        b.mirror = 1;
        clear();
        (void)stack_draw(canvas, m, &a);
        uint16_t first[W * H];
        memcpy(first, canvas, sizeof first);
        clear();
        (void)stack_draw(canvas, m, &b);
        int diff = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) diff += first[y * W + x] != canvas[y * W + (159 - x)];
        assert(diff < 30); /* centre rounding of an odd width: a column or so */
    }
}

static void painting_order(void)
{
    /* two voxels, one on top of the other, drawn so they overlap on screen: seen from above the upper one is
     * painted last and wins; seen from below the lower one does */
    static const char *const STACK[] = {"a", "b"};
    static const uint32_t PAL[] = {0xFF2020, 0x4040FF};
    static const stack_model_t pair = {.layers = STACK, .keys = "ab", .colors = PAL, .w = 1, .d = 1, .h = 2};
    stack_view_t v = plain(80, 40, 8);
    v.squash = 32; /* a layer step of 1 px: the second voxel sits almost on the first */
    v.ky0 = 200;   /* seen from above: a top face 6 rows tall */
    clear();
    (void)stack_draw(canvas, &pair, &v);
    const box_t above = painted();
    int blue = 0, red = 0;
    for (int y = above.y0; y <= above.y1; ++y) {
        if (canvas[y * W + 80] == rgb(0x4040FF)) ++blue;
        if (canvas[y * W + 80] == rgb(0xFF2020)) ++red;
    }
    assert(blue > red && red > 0);
    /* the upper voxel is the later paint: its first row is not covered */
    assert(canvas[above.y0 * W + 80] == rgb(0x4040FF));
    v.ky0 = -200; /* seen from below: the order flips */
    clear();
    (void)stack_draw(canvas, &pair, &v);
    const box_t below = painted();
    blue = red = 0;
    for (int y = below.y0; y <= below.y1; ++y) {
        if (canvas[y * W + 80] == rgb(0x4040FF)) ++blue;
        if (canvas[y * W + 80] == rgb(0xFF2020)) ++red;
    }
    assert(red > blue && blue > 0);
    /* a camera at the middle layer paints from both outer layers inwards: with the eye at layer 1 of 3 (ky = 0 there)
     * the middle one is last */
    static const char *const THREE[] = {"a", "b", "c"};
    static const uint32_t PAL3[] = {0xFF2020, 0x20FF20, 0x4040FF};
    static const stack_model_t tri = {.layers = THREE, .keys = "abc", .colors = PAL3, .w = 1, .d = 1, .h = 3};
    v = plain(80, 40, 8);
    v.squash = 16;
    v.ky0 = 150;
    v.ky_step = -150; /* ky = 150, 0, -150 */
    clear();
    (void)stack_draw(canvas, &tri, &v);
    const box_t eye = painted();
    int green = 0;
    for (int y = eye.y0; y <= eye.y1; ++y) green += canvas[y * W + 80] == rgb(0x20FF20);
    assert(green == 1); /* the middle layer is one row thick and the last thing painted: the 5 row tops of the layers either side overlap it, and lose */
}

static void building(void)
{
    stack_view_t v = plain(80, 40, 4);
    unsigned last = 0;
    for (unsigned build = 0; build <= 256U; build += 8U) {
        v.build = build;
        clear();
        const unsigned n = stack_draw(canvas, &zm_walker, &v);
        assert(n >= last);
        last = n;
        if (build == 0) assert(n == 0U && painted().count == 0U);
    }
    stack_view_t full = plain(80, 40, 4);
    clear();
    const unsigned all = stack_draw(canvas, &zm_walker, &full);
    assert(last == all);
    /* half way a layer is still falling in: it is painted higher than its final place */
    v.build = 128;
    clear();
    (void)stack_draw(canvas, &zm_walker, &v);
    const box_t half = painted();
    clear();
    (void)stack_draw(canvas, &zm_walker, &full);
    const box_t done = painted();
    assert(half.y0 > done.y0 - 4 * 4 * 2 && half.count < done.count);
    v.build = 4000; /* a nonsense value is the whole thing */
    clear();
    assert(stack_draw(canvas, &zm_walker, &v) == all);
}

static void gait(void)
{
    /* step 1 only changes the lowest alt_n layers: the rest of the picture is the same */
    stack_view_t v = plain(80, 60, 3);
    clear();
    (void)stack_draw(canvas, &zm_walker, &v);
    uint16_t before[W * H];
    memcpy(before, canvas, sizeof before);
    v.step = 1;
    clear();
    (void)stack_draw(canvas, &zm_walker, &v);
    int changed = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (before[y * W + x] != canvas[y * W + x]) ++changed;
    int first_changed = H;
    for (int y = 0; y < H && first_changed == H; ++y)
        for (int x = 0; x < W; ++x)
            if (before[y * W + x] != canvas[y * W + x]) { first_changed = y; break; }
    assert(changed > 10);
    assert(first_changed >= 60 - 3 * 3 - 1); /* nothing above the third layer (3 layers of 3 px, plus a row) */
    /* models without alt layers ignore the step */
    stack_view_t t = plain(80, 60, 3);
    clear();
    (void)stack_draw(canvas, &zm_tomb, &t);
    const uint32_t a = checksum();
    t.step = 1;
    clear();
    (void)stack_draw(canvas, &zm_tomb, &t);
    assert(checksum() == a);
}

static void lean_and_squash(void)
{
    stack_view_t v = plain(80, 40, 4);
    clear();
    (void)stack_draw(canvas, &block, &v);
    const box_t straight = painted();
    v.lean = 4 * 256; /* four pixels per layer */
    clear();
    (void)stack_draw(canvas, &block, &v);
    const box_t leaning = painted();
    assert(leaning.x1 == straight.x1 + 8 && leaning.x0 == straight.x0); /* the top layer is 2 layers up: 8 px over */
    v.lean = 0;
    v.squash = 128; /* half as tall */
    clear();
    (void)stack_draw(canvas, &block, &v);
    const box_t flat = painted();
    assert(flat.y1 - flat.y0 + 1 == 6);
    v.squash = 0; /* all layers on one line, still something */
    clear();
    (void)stack_draw(canvas, &block, &v);
    assert(painted().count > 0U);
}

static void lighting(void)
{
    stack_view_t v = plain(80, 40, 4);
    v.shade = 120;
    v.top_light = 0;
    static const char *const COLUMN[] = {"a", "a", "a", "a"};
    static const stack_model_t column = {.layers = COLUMN, .keys = "a", .colors = BLOCK_COLORS, .w = 1, .d = 1, .h = 4};
    clear();
    (void)stack_draw(canvas, &column, &v);
    /* the lowest layer is darker than the same color higher up, which gets none of the shade */
    assert(canvas[39 * W + 80] != canvas[35 * W + 80] && canvas[35 * W + 80] != canvas[31 * W + 80]);
    assert(canvas[27 * W + 80] == rgb(0xFF2020) && canvas[39 * W + 80] != rgb(0xFF2020));
    /* a glowing letter ignores it */
    static const char *const GLOW[] = {"ag"};
    static const uint32_t PAL[] = {0xFF2020, 0xFFFF00};
    static const stack_model_t lamp = {.layers = GLOW, .keys = "ag", .colors = PAL, .w = 2, .d = 1, .h = 1, .lit = 1};
    clear();
    (void)stack_draw(canvas, &lamp, &v);
    assert(canvas[39 * W + 82] == rgb(0xFFFF00) && canvas[39 * W + 77] != rgb(0xFF2020)); /* the lamp is lit, the red one in the same layer is shaded */
    /* the tint mixes over everything, and dark takes it towards black */
    stack_view_t w = plain(80, 40, 4);
    w.tint = 0xFFFFFF;
    w.tint_amt = 256;
    clear();
    (void)stack_draw(canvas, &block, &w);
    assert(canvas[39 * W + 80] == 0xFFFF);
    w.dark = 256;
    clear();
    (void)stack_draw(canvas, &block, &w);
    assert(painted().count == 0U); /* black is 0, the clear color: nothing to see... */
    /* ...and a palette override recolors without touching the model */
    static const uint32_t green[] = {0x00FF00, 0x00FF00, 0x00FF00};
    stack_view_t p = plain(80, 40, 4);
    p.palette = green;
    clear();
    (void)stack_draw(canvas, &block, &p);
    assert(canvas[39 * W + 80] == rgb(0x00FF00) && BLOCK_COLORS[0] == 0xFF2020);
    /* seen from above the top faces take the light, not the sides */
    stack_view_t t = plain(80, 40, 4);
    t.top_light = 200;
    t.light = 0xFFFFFF;
    stack_view_pitch(&t, 24);
    clear();
    (void)stack_draw(canvas, &one, &t);
    const box_t b = painted();
    assert(canvas[b.y0 * W + 80] != canvas[b.y1 * W + 80]);
    /* the outline darkens voxels that are open to the side, not the ones inside */
    static const char *const ROW[] = {"aaa" "aaa" "aaa"};
    static const stack_model_t pad = {.layers = ROW, .keys = "a", .colors = BLOCK_COLORS, .w = 3, .d = 3, .h = 1};
    stack_view_t e = plain(80, 40, 4);
    e.edge = 128;
    clear();
    (void)stack_draw(canvas, &pad, &e);
    assert(canvas[39 * W + 80] != canvas[39 * W + 70] || canvas[38 * W + 80] != canvas[38 * W + 76]); /* the middle voxel differs from a corner */
}

static void projecting(void)
{
    /* the projected middle of a voxel is inside what that voxel paints */
    for (int i = 0; i < 300; ++i) {
        stack_view_t v = plain(30 + (int)rnd(100), 20 + (int)rnd(40), 2 + (int)rnd(5));
        v.yaw = rnd(256);
        v.ky0 = (int)rnd(300);
        v.squash = 96U + rnd(160);
        int sx, sy;
        stack_project(&one, &v, 0, 0, 0, &sx, &sy);
        clear();
        (void)stack_draw(canvas, &one, &v);
        const box_t b = painted();
        assert(b.count > 0U && sx >= b.x0 - 1 && sx <= b.x1 + 1 && sy >= b.y0 - 1 && sy <= b.y1 + 1);
    }
    /* the muzzle of the gun is above the grip on the screen, as it is in the game */
    stack_view_t g = plain(83, 66, 3);
    stack_view_pitch(&g, 26);
    int mx, my, gx, gy;
    stack_project(&zm_gun, &g, 4, 0, 3, &mx, &my);
    stack_project(&zm_gun, &g, 4, 12, 1, &gx, &gy);
    assert(my < gy && mx >= 80 && mx <= 86);
}

static void level_of_detail(void)
{
    /* a model far away has fewer slices to draw but is still there, with its head */
    unsigned prev = 1U << 30;
    for (int sc = 256; sc >= 40; sc -= 36) {
        stack_view_t v = plain(80, 50, 1);
        v.scale = sc;
        clear();
        const unsigned n = stack_draw(canvas, &zm_walker, &v);
        assert(n > 0U && n <= prev + 40U);
        const box_t b = painted();
        assert(b.count > 0U);
        /* the head is the top layer, and skin green shows in the top rows */
        bool skin = false;
        for (int x = b.x0; x <= b.x1; ++x) skin |= canvas[b.y0 * W + x] != 0;
        assert(skin);
        prev = n;
    }
    stack_view_t tiny = plain(80, 50, 1);
    tiny.scale = 30; /* a tenth of a voxel per pixel */
    clear();
    (void)stack_draw(canvas, &zm_walker, &tiny);
    assert(painted().count > 0U && painted().count < 40U);
}

static void edges_and_safety(void)
{
    /* drawing anywhere near and far outside the canvas writes nothing outside it (the guard words stay) */
    enum { GUARD = 4096 };
    static uint16_t arena[GUARD + W * H + GUARD];
    for (int i = 0; i < GUARD; ++i) arena[i] = arena[GUARD + W * H + i] = 0xA5A5;
    uint16_t *px = arena + GUARD;
    const stack_model_t *models[] = {&zm_walker, &zm_runner, &zm_brute, &zm_tomb, &zm_cross, &zm_gun, &block, &two};
    for (int i = 0; i < 4000; ++i) {
        stack_view_t v;
        stack_view_init(&v);
        v.x = (int)rnd(520) - 180;
        v.y = (int)rnd(420) - 180;
        v.scale = (int)rnd(1400) + 1;
        v.yaw = rnd(256);
        v.ky0 = (int)rnd(500) - 150;
        v.ky_step = (int)rnd(60) - 50;
        v.squash = rnd(300);
        v.build = rnd(300);
        v.lean = (int)rnd(2000) - 1000;
        v.edge = rnd(2) ? 60U : 0U;
        v.mirror = (uint8_t)rnd(2);
        v.step = (uint8_t)rnd(2);
        v.tint_amt = rnd(300);
        v.dark = rnd(300);
        const stack_model_t *m = models[rnd(8)];
        const unsigned n = stack_draw(px, m, &v);
        int x0, y0, x1, y1;
        stack_bounds(m, &v, &x0, &y0, &x1, &y1);
        if (x1 < 0 || y1 < 0 || x0 >= W || y0 >= H) assert(n == 0U); /* entirely off screen: skipped */
        stack_shadow(px, m, &v, 0, rnd(300));
        if ((i & 63) == 0) memset(px, 0, sizeof(uint16_t) * W * H);
    }
    for (int i = 0; i < GUARD; ++i) assert(arena[i] == 0xA5A5 && arena[GUARD + W * H + i] == 0xA5A5);
}

static void bounds_hold(void)
{
    /* the bounds are the promise stack_draw keeps: nothing is painted outside them */
    const stack_model_t *models[] = {&zm_walker, &zm_runner, &zm_brute, &zm_tomb, &zm_cross, &zm_gun};
    for (int i = 0; i < 1500; ++i) {
        stack_view_t v;
        stack_view_init(&v);
        v.x = 40 + (int)rnd(80);
        v.y = 30 + (int)rnd(30);
        v.scale = 64 + (int)rnd(500);
        v.yaw = rnd(256);
        v.ky0 = (int)rnd(300) - 40;
        v.ky_step = -(int)rnd(25);
        v.squash = 100U + rnd(157);
        v.build = i % 5 ? 256U : rnd(257);
        v.lean = i % 3 ? 0 : (int)rnd(600) - 300;
        v.edge = (unsigned)(i % 2) * 60U;
        v.mirror = (uint8_t)(i & 1);
        v.step = (uint8_t)((i >> 1) & 1);
        v.top_light = 50;
        const stack_model_t *m = models[rnd(6)];
        clear();
        (void)stack_draw(canvas, m, &v);
        const box_t b = painted();
        int x0, y0, x1, y1;
        stack_bounds(m, &v, &x0, &y0, &x1, &y1);
        if (b.count) {
            if (!(b.x0 >= x0 && b.x1 <= x1 && b.y0 >= y0 && b.y1 <= y1))
                printf("bounds broken: model %ld view x=%d y=%d scale=%d yaw=%u ky0=%d step=%d squash=%u build=%u lean=%d painted %d..%d,%d..%d bounds %d..%d,%d..%d\n",
                       (long)(m - models[0]), v.x, v.y, v.scale, v.yaw, v.ky0, v.ky_step, v.squash, v.build, v.lean, b.x0, b.x1, b.y0, b.y1, x0, x1, y0, y1);
            assert(b.x0 >= x0 && b.x1 <= x1 && b.y0 >= y0 && b.y1 <= y1);
        }
    }
}

static void determinism_and_cost(void)
{
    stack_view_t v;
    stack_view_init(&v);
    stack_view_world(&v, 70, 40, 300, ZM_VOXEL, 90, 100);
    v.yaw = 17;
    clear();
    const unsigned a = stack_draw(canvas, &zm_walker, &v);
    const uint32_t sum = checksum();
    clear();
    assert(stack_draw(canvas, &zm_walker, &v) == a && checksum() == sum);
    /* a whole bad night, worse than any real one: 12 zombies of every kind from the edge of the mist to arm's reach, all 24
     * gravestones in view and the gun */
    unsigned total = 0, worst = 0;
    for (int trial = 0; trial < 50; ++trial) {
        clear();
        unsigned frame = 0;
        for (int i = 0; i < 24; ++i) {
            stack_view_t p;
            stack_view_init(&p);
            stack_view_world(&p, (int)rnd(160), 24 + 4000 / (300 + (int)rnd(1150)), 300 + (int)rnd(1150), ZM_VOXEL, 90, 100);
            p.yaw = rnd(256);
            frame += stack_draw(canvas, i & 1 ? &zm_tomb : &zm_cross, &p);
        }
        for (int i = 0; i < 12; ++i) {
            const int z = 130 + (int)rnd(1400);
            stack_view_t p;
            stack_view_init(&p);
            stack_view_world(&p, 20 + (int)rnd(120), 24 + 4000 / z, z, ZM_VOXEL, 90, 100);
            p.yaw = rnd(256);
            p.edge = z < 480 ? 70U : 0U;
            p.step = 1;
            frame += stack_draw(canvas, i % 3 == 0 ? &zm_brute : i % 3 == 1 ? &zm_runner : &zm_walker, &p);
        }
        stack_view_t g = plain(83, 66, 3);
        stack_view_pitch(&g, 26);
        frame += stack_draw(canvas, &zm_gun, &g);
        total += frame;
        if (frame > worst) worst = frame;
    }
    printf("stack crowd: %u voxels on average, %u at worst (budget 7000)\n", total / 50U, worst);
    assert(worst <= 7000U);
}

int main(void)
{
    validity();
    geometry();
    turning();
    mirroring();
    painting_order();
    building();
    gait();
    lean_and_squash();
    lighting();
    projecting();
    level_of_detail();
    edges_and_safety();
    bounds_hold();
    determinism_and_cost();
    puts("stack: slice rendering geometry, order, build-up, gait, lighting, bounds, canvas edges and cost passed");
    return 0;
}
