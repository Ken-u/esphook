#include "zombie_models.h"

/* The voxel models of the graveyard (see esphook/app_stack.h): a layer is a top view, bottom layer first, and in it
 * the far side is the first row, the front (what faces you at yaw 0) the last. One voxel is ZM_VOXEL world units.
 * Letters: p pants, q shoes and belt, s shirt, t torn shirt, k skin, h hair or hood, m mouth, b blood, S shoulder pads,
 * e eyes (they glow). Everything is symmetric left to right, so a mirrored model is a valid one (that is how the legs
 * take turns). The gun has its own letters: g gunmetal, d dark, w sights, k hand, a sleeve; its first row is the muzzle. */

static const char *const walker_layers[] = {
    /* 0 */ "........." "..qq.qq.." "..qq.qq.." "..qq.qq.." "..qq.qq..",
    /* 1 */ "........." "..pp.pp.." "..pp.pp.." "..pp.pp.." ".........",
    /* 2 */ "........." "..pp.pp.." "..pp.pp.." "..pp.pp.." ".........",
    /* 3 */ "........." "..pp.pp.." "..pp.pp.." "..pp.pp.." ".........",
    /* 4 */ "........." "..pp.pp.." "..pp.pp.." "..pp.pp.." ".........",
    /* 5 */ "........." "..pp.pp.." "..pp.pp.." "..pp.pp.." ".........",
    /* 6 */ "........." "..pp.pp.." "..pp.pp.." "..pp.pp.." ".........",
    /* 7 */ "........." "..pp.pp.." "..pp.pp.." "..pp.pp.." ".........",
    /* 8 */ "........." "..qqqqq.." "..qqqqq.." "..qqqqq.." ".........",
    /* 9 */ "........." "..sssss.." "..sssss.." "..tstst.." ".........",
    /* 10 */ "........." "..sssss.." "..sssss.." "..sssss.." ".........",
    /* 11 */ "........." "..sssss.." ".sssssss." "..ssbss.." ".........",
    /* 12 */ "........." "..sssss.." ".sssssss." "..ssbss.." ".........",
    /* 13 */ "........." "..sssss.." ".sssssss." ".ksssssk." ".k.....k.",
    /* 14 */ "........." "..ttttt.." ".sttttts." ".ktttttk." ".k.....k.",
    /* 15 */ "........." "...kkk..." "...kkk..." "...kkk..." ".k.....k.",
    /* 16 */ "........." "..hhhhh.." "..kkkkk.." "..kmmmk.." ".........",
    /* 17 */ "........." "..hhhhh.." "..kkkkk.." "..kekek.." ".........",
    /* 18 */ "........." "..hhhhh.." "..kkkkk.." "..kkkkk.." ".........",
    /* 19 */ "........." "..hhhhh.." "..hhhhh.." "..hhkhh.." ".........",
};
static const char *const walker_alt[] = {
    /* 0 */ "........." ".....qq.." ".....qq.." ".....qq.." ".....qq..",
    /* 1 */ "........." ".....pp.." "..qq.pp.." "..qq.pp.." "..qq.....",
    /* 2 */ "........." ".....pp.." "..pp.pp.." "..pp.pp.." ".........",
};
static const uint32_t walker_colors[] = {0x34343E, 0x1E1E26, 0x7A4B3A, 0x4E2F25, 0x86A860, 0x2A2A22, 0x2A1010, 0x7A1010, 0x3A2020, 0xFF3020};
const stack_model_t zm_walker = {.layers = walker_layers, .alt = walker_alt, .keys = ZM_KEYS, .colors = walker_colors, .w = 9, .d = 5, .h = 20, .alt_n = 3, .lit = ZM_LIT};

static const char *const runner_layers[] = {
    /* 0 */ "......." ".qq.qq." ".qq.qq." ".qq.qq." ".qq.qq.",
    /* 1 */ "......." ".pp.pp." ".pp.pp." ".pp.pp." ".......",
    /* 2 */ "......." ".pp.pp." ".pp.pp." ".pp.pp." ".......",
    /* 3 */ "......." ".pp.pp." ".pp.pp." ".pp.pp." ".......",
    /* 4 */ "......." ".pp.pp." ".pp.pp." ".pp.pp." ".......",
    /* 5 */ "......." ".pp.pp." ".pp.pp." ".pp.pp." ".......",
    /* 6 */ "......." ".pp.pp." ".pp.pp." ".pp.pp." ".......",
    /* 7 */ "......." ".pp.pp." ".pp.pp." ".pp.pp." ".......",
    /* 8 */ "......." "......." ".qqqqq." ".qqqqq." ".qqqqq.",
    /* 9 */ "......." "......." "..sss.." "..sss.." "..ttt..",
    /* 10 */ "......." "......." "..sss.." "..sss.." "..sss..",
    /* 11 */ "......." "......." "..sss.." ".sssss." "..sss..",
    /* 12 */ "......." "......." "..sss.." ".sssss." ".ksssk.",
    /* 13 */ "......." "......." "..sss.." ".sssss." ".ksssk.",
    /* 14 */ "......." "......." "..sss.." ".sssss." ".ksssk.",
    /* 15 */ "......." "......." "..hhh.." "..hhh.." "..kkk..",
    /* 16 */ "......." "......." "..hhh.." "..hhh.." "..kmk..",
    /* 17 */ "......." "......." "..hhh.." "..hhh.." "..eke..",
    /* 18 */ "......." "......." "..hhh.." "..hhh.." "..hhh..",
};
static const char *const runner_alt[] = {
    /* 0 */ "......." "....qq." "....qq." "....qq." "....qq.",
    /* 1 */ "......." "....pp." ".qq.pp." ".qq.pp." ".qq....",
    /* 2 */ "......." "....pp." ".pp.pp." ".pp.pp." ".......",
};
static const uint32_t runner_colors[] = {0x34343E, 0x1E1E26, 0x3E6E4A, 0x2A4A32, 0x93AE7C, 0x2C5036, 0x2A1010, 0x7A1010, 0x3A2020, 0xFF3020};
const stack_model_t zm_runner = {.layers = runner_layers, .alt = runner_alt, .keys = ZM_KEYS, .colors = runner_colors, .w = 7, .d = 5, .h = 19, .alt_n = 3, .lit = ZM_LIT};

static const char *const brute_layers[] = {
    /* 0 */ ".............." ".............." "...qqq..qqq..." "...qqq..qqq..." "...qqq..qqq..." "...qqq..qqq..." "...qqq..qqq..." "...qqq..qqq...",
    /* 1 */ ".............." ".............." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
    /* 2 */ ".............." ".............." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
    /* 3 */ ".............." ".............." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
    /* 4 */ ".............." ".............." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
    /* 5 */ ".............." ".............." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
    /* 6 */ ".............." ".............." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
    /* 7 */ ".............." ".............." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
    /* 8 */ ".............." ".............." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
    /* 9 */ ".............." ".............." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
    /* 10 */ ".............." ".............." "...qqqqqqqq..." "...qqqqqqqq..." "...qqqqqqqq..." "...qqqqqqqq..." "...qqqqqqqq..." "..............",
    /* 11 */ ".............." ".............." "...tttttttt..." "...tttttttt..." "...tttttttt..." "...tttttttt..." "...tttttttt..." "..............",
    /* 12 */ ".............." ".............." "...ssssssss..." ".ssssssssssss." ".ssssssssssss." ".ssssssssssss." "...ssssssss..." "..............",
    /* 13 */ ".............." ".............." "...ssssssss..." ".ssssssssssss." ".ssssssssssss." ".ssssssssssss." "...ssssssss..." "..............",
    /* 14 */ ".............." ".............." "...ssssssss..." ".ssssssssssss." ".ssssssssssss." ".ssssssssssss." ".kksssbbssskk." ".kk........kk.",
    /* 15 */ ".............." ".............." "...ssssssss..." ".ssssssssssss." ".ssssssssssss." ".ssssssssssss." ".kksssbbssskk." ".kk........kk.",
    /* 16 */ ".............." ".............." "...ssssssss..." ".ssssssssssss." ".ssssssssssss." ".ssssssssssss." ".kksssbbssskk." ".kk........kk.",
    /* 17 */ ".............." ".............." "...ssssssss..." ".ssssssssssss." ".ssssssssssss." ".ssssssssssss." "...ssssssss..." "..............",
    /* 18 */ ".............." ".............." ".SSSSSSSSSSSS." ".SSSSSSSSSSSS." ".SSSSSSSSSSSS." ".SSSSSSSSSSSS." ".SSSSSSSSSSSS." "..............",
    /* 19 */ ".............." ".............." ".SSSSSSSSSSSS." ".SSSSSSSSSSSS." ".SSSSSSSSSSSS." ".SSSSSSSSSSSS." ".SSSSSSSSSSSS." "..............",
    /* 20 */ ".............." ".............." ".............." ".....kkkk....." ".....kkkk....." ".....kkkk....." ".............." "..............",
    /* 21 */ ".............." ".............." ".............." ".....hhhh....." ".....kkkk....." ".....kmmk....." ".............." "..............",
    /* 22 */ ".............." ".............." ".............." ".....hhhh....." ".....kkkk....." ".....ekke....." ".............." "..............",
    /* 23 */ ".............." ".............." ".............." ".....hhhh....." ".....kkkk....." ".....hhhh....." ".............." "..............",
    /* 24 */ ".............." ".............." ".............." ".....hhhh....." ".....kkkk....." ".....kkkk....." ".............." "..............",
    /* 25 */ ".............." ".............." ".............." ".....hhhh....." ".....hhhh....." ".....hhhh....." ".............." "..............",
};
static const char *const brute_alt[] = {
    /* 0 */ ".............." ".............." "........qqq..." "........qqq..." "........qqq..." "........qqq..." "........qqq..." "........qqq...",
    /* 1 */ ".............." ".............." "........ppp..." "...qqq..ppp..." "...qqq..ppp..." "...qqq..ppp..." "...qqq..ppp..." "...qqq........",
    /* 2 */ ".............." ".............." "........ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "...ppp..ppp..." "..............",
};
static const uint32_t brute_colors[] = {0x2A2A30, 0x1A1A20, 0x5C2A2A, 0x3C1C1C, 0x7C8C66, 0x2A2A22, 0x2A1010, 0x7A1010, 0x3A2020, 0xFF3020};
const stack_model_t zm_brute = {.layers = brute_layers, .alt = brute_alt, .keys = ZM_KEYS, .colors = brute_colors, .w = 14, .d = 8, .h = 26, .alt_n = 3, .lit = ZM_LIT};

static const char *const tomb_layers[] = {
    /* 0 */ "ddddd" "ddddd",
    /* 1 */ "ggggg" "ogggo",
    /* 2 */ "ggggg" "ogggg",
    /* 3 */ "ggggg" "ggigg",
    /* 4 */ "ggggg" "ggigg",
    /* 5 */ "ggggg" "ggggg",
    /* 6 */ ".ggg." ".ggg.",
    /* 7 */ "..g.." "..g..",
};
static const uint32_t tomb_colors[] = {0x6E7490, 0x30333E, 0x22252E, 0x3E5A38};
const stack_model_t zm_tomb = {.layers = tomb_layers, .keys = "gdio", .colors = tomb_colors, .w = 5, .d = 2, .h = 8};

static const char *const cross_layers[] = {
    /* 0 */ "..ddd.." "..ddd..",
    /* 1 */ "...g..." "...g...",
    /* 2 */ "...g..." "...g...",
    /* 3 */ "...g..." "...g...",
    /* 4 */ ".ggggg." ".ggggg.",
    /* 5 */ ".ggggg." ".ggggg.",
    /* 6 */ "...g..." "...i...",
    /* 7 */ "...g..." "...g...",
};
static const uint32_t cross_colors[] = {0x6E7490, 0x30333E, 0x22252E, 0x3E5A38};
const stack_model_t zm_cross = {.layers = cross_layers, .keys = "gdio", .colors = cross_colors, .w = 7, .d = 2, .h = 8};

static const char *const gun_layers[] = {
    /* 0 */ "........." "........." "........." "........." "........." "........." "........." "........." "........." ".....kk.." "...ddkk.." "aaaddkk.." "aaaddkaaa" "aaa...aaa",
    /* 1 */ "........." "........." "........." "........." "........." "........." "........." ".kk......" ".kk......" ".kk..kk.." ".kkddkk.." "aaaddkk.." "aaaddkaaa" "aaa...aaa",
    /* 2 */ "........." "........." "........." "........." "........." "........." "...ddd..." ".kkddd..." ".kkddd..." ".kkddkk.." ".kkddkk.." "aaaddkk.." "aaaddkaaa" "aaa...aaa",
    /* 3 */ "....d...." "....g...." "....g...." "....g...." "....g...." "...ggg..." "...ggg..." ".kkggg..." ".kkggg..." ".kkggkk.." ".kkggkk.." "aaaggkk.." "aaa..kaaa" "aaa...aaa",
    /* 4 */ "....d...." "....g...." "....g...." "....g...." "....g...." "...ggg..." "...ggg..." "...ggg..." "...ggg..." "...ggg..." "...ggg..." "...ggg..." "........." ".........",
    /* 5 */ "........." "....w...." "........." "........." "........." "...ggg..." "...ggg..." "...ggg..." "...ggg..." "...ggg..." "...ggg..." "...ggg..." "........." ".........",
    /* 6 */ "........." "........." "........." "........." "........." "........." "....d...." "....d...." "....d...." "....d...." "....d...." "...w.w..." "........." ".........",
};
static const uint32_t gun_colors[] = {0x4A505E, 0x262A35, 0xC8CCD6, 0x7A5A44, 0x1E2A44};
const stack_model_t zm_gun = {.layers = gun_layers, .keys = "gdwka", .colors = gun_colors, .w = 9, .d = 14, .h = 7};
