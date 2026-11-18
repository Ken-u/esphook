#include "starship_models.h"

/* The voxel models of the star field (see esphook/app_stack.h). A layer is a top view, bottom layer first; in it the far
 * side is the first row, the side that faces you at yaw 0 the last. Rocks: a base rock, b dark, c light, d crater floor, g
 * ore (glows). The fighter: a steel, b dark steel, c light panel, d red cannon, y amber glow, e eye (glow); its nose is
 * the row next to you. The cannons on your own wings: g gunmetal, d dark, y glow; row 0 is the muzzle. The models were
 * stripped of the voxels nothing of which can be seen. */

static const char *const rock_a_layers[] = {
    /* 0 */ "........." "........." "........." ".....a..." ".....a..." "........." "........." "........." ".........",
    /* 1 */ "........." "........." "...accca." "...b..a.." "..b....a." "..bb..c.." "....ba..." "........." ".........",
    /* 2 */ "........." "....cc..." "..ac..ca." ".a.....a." ".b.....a." ".bbb...a." "ab..b.a.." ".b..ba..." ".........",
    /* 3 */ "........." "..aacaa.." ".c.....a." ".a.....a." "b......a." "b......a." "a......b." ".bb...a.." "...baa...",
    /* 4 */ "........." ".cabbbb.." ".c.....b." "a.......a" "a.......a" "b.......a" "b......b." ".aa..bb.." "...aa....",
    /* 5 */ "........." "..bb.bbb." "ca..b...a" "a.......a" "a.......a" "b.......a" "b......b." ".ab...b.." "...aaa...",
    /* 6 */ "........." "......b.." ".aabbb.a." "aa......a" "a.......c" "a.......a" ".aba...a." "..ab.bb.." "...ab....",
    /* 7 */ "........." ".....b..." "..abbba.." "..a..aaa." ".a....aa." ".aaa.aaa." "....bbb.." "....a...." ".........",
    /* 8 */ "........." "........." "...aa...." "..aaa...." "..aaba..." "...ab...." "........." "........." ".........",
};
static const uint32_t rock_a_colors[] = {0x7A6C5C, 0x4E4438, 0xA89A86, 0x2A241E, 0xFF8A30};
const stack_model_t sm_rock_a = {.layers = rock_a_layers, .keys = SM_ROCK_KEYS, .colors = rock_a_colors, .w = 9, .d = 9, .h = 9, .lit = SM_ROCK_LIT};

static const char *const rock_b_layers[] = {
    /* 0 */ "........." "........." "........." "....a...." ".....aa.." "........." "........." "........." ".........",
    /* 1 */ "........." "........." ".....b..." "..bbb.aa." ".ba....a." "...aa..a." ".....aa.." "........." ".........",
    /* 2 */ "........." ".....b..." ".ab.b.a.." "b..b...a." "b.......a" "a......a." ".caa....a" "....aaaa." ".........",
    /* 3 */ "...b....." "..b.baa.." "aa.....c." "a......c." "b.......c" "a.......a" "a.......a" ".aaca..a." ".....aa..",
    /* 4 */ "...aa...." ".aa..ba.." "a......a." "c.......a" "a.......a" "a.......c" "ac......a" "..ac...a." "....aab..",
    /* 5 */ "...aa...." "..a..bba." ".a...bbb." "a.....ba." "c.......c" "c.......c" ".a....bb." "..caa.bb." ".....a...",
    /* 6 */ "........." "...aabb.." "..aab...." ".aab.b.b." ".a....ba." ".a....bb." "..a..b..." "..caab..." ".........",
    /* 7 */ "........." "........." "....b...." "....b...." "..aabbb.." "..aabbb.." "..aaab..." ".....b..." ".........",
};
static const uint32_t rock_b_colors[] = {0x7A6C5C, 0x4E4438, 0xA89A86, 0x2A241E, 0xFF8A30};
const stack_model_t sm_rock_b = {.layers = rock_b_layers, .keys = SM_ROCK_KEYS, .colors = rock_b_colors, .w = 9, .d = 9, .h = 8, .lit = SM_ROCK_LIT};

static const char *const rock_c_layers[] = {
    /* 0 */ "........." "........." "...ba...." "..b..c..." "..g..c..." "...aaa..." "...b....." "........." ".........",
    /* 1 */ "........." "...bg...." "..b..aa.." "..g....a." "..b...c.." "..a...c.." "..ab.a..." "....a...." ".........",
    /* 2 */ "..gaa...." "..a..g..." "..b...aaa" "...b....a" "..b.....g" ".b.....c." ".b.....b." ".aba..bb." "....ac...",
    /* 3 */ "..gaa...." "..a..aaa." ".b......a" "..b.....a" "bb......a" "g......ba" "b.....b.b" "ab...b..." "..aaaag..",
    /* 4 */ "..acaa..." ".c....ca." "c.......a" "a.......a" "b.......a" "bb....bbb" "gb...b..b" "bb...b..." "..aaabb..",
    /* 5 */ "...aac..." ".cc...a.." "c......c." "a.......a" "bb......a" "..b....ba" "..b...b.." ".b...b..." "..ccccb..",
    /* 6 */ "....a...." ".ccc.ac.." "aa.....c." "a......a." "bb......a" "..b....aa" "..b...c.." ".bb..a..." "...aa....",
    /* 7 */ "........." "....aa..." "..aa..a.." ".a.....a." ".g.....a." ".bb...g.." "..bbbb..." "...bga..." ".........",
    /* 8 */ "........." "........." "...bbaa.." "..abbba.." "..aaagb.." "...aba..." "........." "........." ".........",
};
static const uint32_t rock_c_colors[] = {0x7A6C5C, 0x4E4438, 0xA89A86, 0x2A241E, 0xFF8A30};
const stack_model_t sm_rock_c = {.layers = rock_c_layers, .keys = SM_ROCK_KEYS, .colors = rock_c_colors, .w = 9, .d = 9, .h = 9, .lit = SM_ROCK_LIT};

static const char *const fighter_layers[] = {
    /* 0 */ "..............." "......bbb......" "......a.a......" "......a.a......" "......a.a......" "......a.a......" "......a.a......" "......a.a......" ".......a.......",
    /* 1 */ "......y.y......" "dbbbbb.a.bbbbbd" "dbbbb.....bbbbd" "ybbb.......bbby" "..b.........b.." "...bbb...bbb..." "......a.a......" "......a.a......" ".......a.......",
    /* 2 */ "..............." "d.....aaa.....d" ".....aaaaa....." "....aaaaaaa...." "...aaaaaaaaa..." "...aaa...aaa..." "......a.a......" "......aaa......" "...............",
    /* 3 */ "..............." "..............." "..............." "..............." "..............." "......ccc......" "......c.c......" "......cec......" "...............",
    /* 4 */ "..............." "..............." "..............." "..............." ".......b......." ".......b......." ".......b......." "..............." "...............",
};
static const uint32_t fighter_colors[] = {0x5E6A84, 0x333C52, 0x9AA8C6, 0xC8322A, 0xFFC040, 0xFF2A2A};
const stack_model_t sm_fighter = {.layers = fighter_layers, .keys = SM_FOE_KEYS, .colors = fighter_colors, .w = 15, .d = 9, .h = 5, .lit = SM_FOE_LIT};

static const char *const cannon_layers[] = {
    /* 0 */ "....." "....." "....." "....." "....." "....." ".ggg." ".ggg." ".ggg." "ddddd" "ddddd" "ddddd",
    /* 1 */ "..d.." "..g.." "..y.." "..g.." "..g.." "..g.." ".ggg." ".ggg." ".ggg." "ddddd" "ddddd" "ddddd",
    /* 2 */ "..d.." "..g.." "..g.." "..g.." "..g.." "..g.." ".ggg." ".ggg." ".ggg." ".ggg." ".ggg." ".ggg.",
    /* 3 */ "....." "....." "....." "....." "..d.." "....." ".ddd." ".ddd." "....." "....." "....." ".....",
};
static const uint32_t cannon_colors[] = {0x4A505E, 0x262A35, 0xFFC040};
const stack_model_t sm_cannon = {.layers = cannon_layers, .keys = "gdy", .colors = cannon_colors, .w = 5, .d = 12, .h = 4, .lit = 1};
