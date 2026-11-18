#include "racing_models.h"

/* The voxel models of the road (see esphook/app_stack.h). A layer is a top view, bottom layer first; in it the far side
 * is the first row, the side that faces you at yaw 0 the last, so a car is drawn nose first and the tail is the row
 * next to you. One voxel is RM_VOXEL millimetres, so a car is about 3.1 m wide and 4.6 m long, like the flat ones were.
 * Cars: b body (the paint of that car), B body shade, g glass, w tyre, k trim, y number plate, r tail light (glows).
 * Trees: n trunk, f foliage, d dark foliage, F light foliage. Lamp post: p pole, L lamp (glows). Gantry: p pillar,
 * k black, w white, R red. Models were stripped of the voxels nothing of which can be seen. */

static const char *const coupe_layers[] = {
    /* 0 */ "..........." ".kkkkkkkkk." "w.........w" "w.........w" "w.........w" ".k.......k." ".k.......k." ".k.......k." ".k.......k." ".k.......k." ".k.......k." "w.........w" "w.........w" "w.........w" ".kkkkkkkkk." "...........",
    /* 1 */ "BkkkkkkkkkB" "B.........B" "w.........w" "w.........w" "w.........w" "B.........B" "B.........B" "B.........B" "B.........B" "B.........B" "B.........B" "w.........w" "w.........w" "w.........w" "B.........B" "BkkkkkkkkkB",
    /* 2 */ "bbbbbbbbbbb" "bb.......bb" "B.........B" "B.........B" "B.........B" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "B.........B" "B.........B" "B.........B" "b.........b" "rrrbyyybrrr",
    /* 3 */ "..bbbbbbb.." "..bbbbbbb.." "bbbbbbbbbbb" "bbbbbbbbbbb" "b.........b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.........b" "bbbbbbbbbbb" "bbbbbbbbbbb" "b.........b" "rrrbbbbbrrr",
    /* 4 */ "..........." "..........." "..........." "..........." ".bgggggggb." ".g.......g." ".g.......g." ".g.......g." ".b.......b." ".g.......g." ".g.......g." ".bgggggggb." "..........." "k.........k" "kkkkkkkkkkk" "...........",
    /* 5 */ "..........." "..........." "..........." "..........." "..........." "..bbbbbbb.." "..bbbbbbb.." "..bbbbbbb.." "..bbbbbbb.." "..bbbbbbb.." "..bbbbbbb.." "..........." "..........." "..........." "..........." "...........",
};
static const uint32_t coupe_colors[] = {0xD8302A, 0x7A1A16, 0x2E4E70, 0x0C0C10, 0x20222A, 0xE8E8E0, 0xFF3A2A};
const stack_model_t rm_coupe = {.layers = coupe_layers, .keys = RM_CAR_KEYS, .colors = coupe_colors, .w = 11, .d = 16, .h = 6, .lit = RM_CAR_LIT};

static const char *const sedan_layers[] = {
    /* 0 */ "..........." ".kkkkkkkkk." "w.........w" "w.........w" "w.........w" ".k.......k." ".k.......k." ".k.......k." ".k.......k." ".k.......k." ".k.......k." ".k.......k." "w.........w" "w.........w" "w.........w" ".kkkkkkkkk." "...........",
    /* 1 */ "BkkkkkkkkkB" "B.........B" "w.........w" "w.........w" "w.........w" "B.........B" "B.........B" "B.........B" "B.........B" "B.........B" "B.........B" "B.........B" "w.........w" "w.........w" "w.........w" "B.........B" "BkkkkkkkkkB",
    /* 2 */ "bbbbbbbbbbb" "b.........b" "B.........B" "B.........B" "B.........B" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "B.........B" "B.........B" "B.........B" "b.........b" "rrrbyyybrrr",
    /* 3 */ ".bbbbbbbbb." ".bbbbbbbbb." "bbbbbbbbbbb" "bbbbbbbbbbb" "b.........b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.bbbbbbb.b" "b.........b" "bbbbbbbbbbb" "bbbbbbbbbbb" "rrrbbbbbrrr",
    /* 4 */ "..........." "..........." "..........." "..........." ".bgggggggb." ".g.......g." ".g.......g." ".g.......g." ".b.......b." ".g.......g." ".g.......g." ".g.......g." ".g.......g." ".bgggggggb." "..........." "..........." "...........",
    /* 5 */ "..........." "..........." "..........." "..........." "..........." "..bgggggb.." "..g.....g.." "..g.....g.." "..g.....g.." "..g.....g.." "..g.....g.." "..g.....g.." "..bgggggb.." "..........." "..........." "..........." "...........",
    /* 6 */ "..........." "..........." "..........." "..........." "..........." "..........." "...bbbbb..." "...bbbbb..." "...bbbbb..." "...bbbbb..." "...bbbbb..." "...bbbbb..." "..........." "..........." "..........." "..........." "...........",
};
static const uint32_t sedan_colors[] = {0xD8302A, 0x7A1A16, 0x2E4E70, 0x0C0C10, 0x20222A, 0xE8E8E0, 0xFF3A2A};
const stack_model_t rm_sedan = {.layers = sedan_layers, .keys = RM_CAR_KEYS, .colors = sedan_colors, .w = 11, .d = 17, .h = 7, .lit = RM_CAR_LIT};

static const char *const van_layers[] = {
    /* 0 */ "..........." ".kkkkkkkkk." "w.........w" "w.........w" "w.........w" ".k.......k." ".k.......k." ".k.......k." ".k.......k." "w.........w" "w.........w" "w.........w" ".kkkkkkkkk." "...........",
    /* 1 */ "BBBBBBBBBBB" "BB.......BB" "wB.......Bw" "w.........w" "w.........w" "B.........B" "B.........B" "B.........B" "B.........B" "w.........w" "w.........w" "w.........w" "B.........B" "BkkkkkkkkkB",
    /* 2 */ "..bbbbbbb.." "..b.....b.." "B.b.....b.B" "Bb.......bB" "B.........B" "b.........b" "b.........b" "b.........b" "b.........b" "B.........B" "B.........B" "B.........B" "b.........b" "rbbbyyybbbr",
    /* 3 */ "..ggggggg.." "..b.....b.." "..b.....b.." "bb.......bb" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "rbbbbBbbbbr",
    /* 4 */ "..bbbbbbb.." "..bbbbbbb.." "..bbbbbbb.." "bb.......bb" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "rbbbbBbbbbr",
    /* 5 */ "..........." "..........." "..........." "bbbbbbbbbbb" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "rbgggggggbr",
    /* 6 */ "..........." "..........." "..........." "bbbbbbbbbbb" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "rbgggggggbr",
    /* 7 */ "..........." "..........." "..........." "bbbbbbbbbbb" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "b.........b" "bbbbbbbbbbb",
    /* 8 */ "..........." "..........." "..........." ".bbbbbbbbb." ".bbbbbbbbb." ".bbbbbbbbb." ".bbbbbbbbb." ".bbbbbbbbb." ".bbbbbbbbb." ".bbbbbbbbb." ".bbbbbbbbb." ".bbbbbbbbb." ".bbbbbbbbb." "...........",
};
static const uint32_t van_colors[] = {0xD8302A, 0x7A1A16, 0x2E4E70, 0x0C0C10, 0x20222A, 0xE8E8E0, 0xFF3A2A};
const stack_model_t rm_van = {.layers = van_layers, .keys = RM_CAR_KEYS, .colors = van_colors, .w = 11, .d = 14, .h = 9, .lit = RM_CAR_LIT};

static const char *const pine_layers[] = {
    /* 0 */ "........." "........." "........." "........." "....n...." "........." "........." "........." ".........",
    /* 1 */ "........." "........." "........." "........." "....n...." "........." "........." "........." ".........",
    /* 2 */ "...fff..." ".ff...ff." ".f.....f." "f.......f" "f.......f" "f.......f" ".f.....f." ".ff...ff." "...fff...",
    /* 3 */ "...fff..." "..f...f.." ".f.....f." "f.......f" "f.......f" "f.......f" ".f.....f." "..f...f.." "...fff...",
    /* 4 */ "........." "..ddddd.." ".d.....d." ".d.....d." ".d.....d." ".d.....d." ".d.....d." "..ddddd.." ".........",
    /* 5 */ "........." "...ddd..." "..d...d.." ".d.....d." ".d.....d." ".d.....d." "..d...d.." "...ddd..." ".........",
    /* 6 */ "........." "....f...." "..ff.ff.." "..f...f.." ".f.....f." "..f...f.." "..ff.ff.." "....f...." ".........",
    /* 7 */ "........." "........." "...fff..." "..f...f.." "..f...f.." "..f...f.." "...fff..." "........." ".........",
    /* 8 */ "........." "........." "...ddd..." "..d...d.." "..d...d.." "..d...d.." "...ddd..." "........." ".........",
    /* 9 */ "........." "........." "........." "...ddd..." "...d.d..." "...ddd..." "........." "........." ".........",
    /* 10 */ "........." "........." "........." "...fff..." "...f.f..." "...fff..." "........." "........." ".........",
    /* 11 */ "........." "........." "........." "....f...." "...f.f..." "....f...." "........." "........." ".........",
    /* 12 */ "........." "........." "........." "........." "....d...." "........." "........." "........." ".........",
};
static const uint32_t pine_colors[] = {0x5A3A22, 0x2C7A3A, 0x1C5A2C, 0x4E9A48};
const stack_model_t rm_pine = {.layers = pine_layers, .keys = RM_TREE_KEYS, .colors = pine_colors, .w = 9, .d = 9, .h = 13};

static const char *const round_layers[] = {
    /* 0 */ "........." "........." "........." "........." "....n...." "........." "........." "........." ".........",
    /* 1 */ "........." "........." "........." "........." "....n...." "........." "........." "........." ".........",
    /* 2 */ "........." "........." "........." "........." "....n...." "........." "........." "........." ".........",
    /* 3 */ "........." "........." "...fff..." "..f...F.." "..f...f.." "..f...f.." "...Fff..." "........." ".........",
    /* 4 */ "........." "...Fff..." "..F...f.." ".F.....F." ".f.....f." ".f.....f." "..f...f.." "...Fdf..." ".........",
    /* 5 */ "...ffF..." "..f...f.." ".f.....f." "d.......F" "f.......f" "F.......f" ".f.....f." "..f...f.." "...Fff...",
    /* 6 */ "...fff..." ".ff...ff." ".f.....f." "f.......f" "f.......F" "f.......f" ".f.....f." ".ff...ff." "...fFf...",
    /* 7 */ "...fff..." ".ff...Ff." ".f.....f." "f.......f" "f.......f" "f.......F" ".F.....F." ".ff...Ff." "...ffF...",
    /* 8 */ "....f...." "..ff.ff.." ".f.....f." ".f.....f." "f.......f" ".f.....f." ".f.....f." "..ff.ff.." "....f....",
    /* 9 */ "........." "...fff..." "..ff.ff.." ".ff...Ff." ".f.....f." ".ff...ff." "..fF.ff.." "...fff..." ".........",
    /* 10 */ "........." "........." "....f...." "...f.f..." "..f...F.." "...f.F..." "....F...." "........." ".........",
    /* 11 */ "........." "........." "........." "....f...." "...fff..." "....f...." "........." "........." ".........",
};
static const uint32_t round_colors[] = {0x5A3A22, 0x2F7E3C, 0x1E5E2E, 0x5CAA52};
const stack_model_t rm_round = {.layers = round_layers, .keys = RM_TREE_KEYS, .colors = round_colors, .w = 9, .d = 9, .h = 12};

static const char *const post_layers[] = {
    /* 0 */ "...p",
    /* 1 */ "...p",
    /* 2 */ "...p",
    /* 3 */ "...p",
    /* 4 */ "...p",
    /* 5 */ "...p",
    /* 6 */ "...p",
    /* 7 */ "...p",
    /* 8 */ "...p",
    /* 9 */ "...p",
    /* 10 */ "L..p",
    /* 11 */ "pppp",
};
static const uint32_t post_colors[] = {0x9098A8, 0xFFE8A0};
const stack_model_t rm_post = {.layers = post_layers, .keys = "pL", .colors = post_colors, .w = 4, .d = 1, .h = 12, .lit = 1};

static const char *const gantry_layers[] = {
    /* 0 */ "pp.........................pp" "pp.........................pp",
    /* 1 */ "pp.........................pp" "pp.........................pp",
    /* 2 */ "pp.........................pp" "pp.........................pp",
    /* 3 */ "pp.........................pp" "pp.........................pp",
    /* 4 */ "pp.........................pp" "pp.........................pp",
    /* 5 */ "pp.........................pp" "pp.........................pp",
    /* 6 */ "pp.........................pp" "pp.........................pp",
    /* 7 */ "pp.........................pp" "pp.........................pp",
    /* 8 */ "pp.........................pp" "pp.........................pp",
    /* 9 */ "pp.........................pp" "pp.........................pp",
    /* 10 */ "pp.........................pp" "pp.........................pp",
    /* 11 */ "pp.........................pp" "pp.........................pp",
    /* 12 */ "pp.........................pp" "pp.........................pp",
    /* 13 */ "RRRRRRRRRRRRRRRRRRRRRRRRRRRRR" "RRRRRRRRRRRRRRRRRRRRRRRRRRRRR",
    /* 14 */ "wwkkwwkkwwkkwwkkwwkkwwkkwwkkw" "wwkkwwkkwwkkwwkkwwkkwwkkwwkkw",
    /* 15 */ "kkwwkkwwkkwwkkwwkkwwkkwwkkwwk" "kkwwkkwwkkwwkkwwkkwwkkwwkkwwk",
    /* 16 */ "wwkkwwkkwwkkwwkkwwkkwwkkwwkkw" "wwkkwwkkwwkkwwkkwwkkwwkkwwkkw",
    /* 17 */ "kkwwkkwwkkwwkkwwkkwwkkwwkkwwk" "kkwwkkwwkkwwkkwwkkwwkkwwkkwwk",
};
static const uint32_t gantry_colors[] = {0xD8D8E0, 0x181820, 0xF4F4F4, 0xE03A2E};
const stack_model_t rm_gantry = {.layers = gantry_layers, .keys = "pkwR", .colors = gantry_colors, .w = 29, .d = 2, .h = 18};
