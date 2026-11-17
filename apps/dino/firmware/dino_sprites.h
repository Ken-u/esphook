#pragma once
#include <stddef.h>
#include <stdint.h>

/* 1-bit pixel art, '#' is drawn and '.' is transparent. Rows must all be `w` characters wide
 * (tests/test_dino.c checks this, so a missing dot cannot silently shear a sprite). */
typedef struct {
    uint8_t w, h;
    const char *const *rows;
} dino_sprite_t;

static const char *const DINO_RUN_A[] = {
    "........########", "........##.#####", "........########", "........########",
    "........#####...", "........#####...", "#.....########..", "##....#########.",
    "###..##########.", ".#############..", "..############.#", "...##########...",
    "....########....", ".....###..###...", ".....###..#.....", ".....####.......",
};
static const char *const DINO_RUN_B[] = {
    "........########", "........##.#####", "........########", "........########",
    "........#####...", "........#####...", "#.....########..", "##....#########.",
    "###..##########.", ".#############..", "..############.#", "...##########...",
    "....########....", ".....###..###...", ".....#....###...", "..........####..",
};
static const char *const DINO_CACTUS_S[] = {
    "..##..", "..##..", "#.##.#", "#.##.#", "#.##.#", "######", "..###.", "..##..", "..##..", "..##..",
};
static const char *const DINO_CACTUS_L[] = {
    "...##...", "...##...", "...##...", "#..##.#.", "#..##.##", "#..##.##", "##.##.##", "########",
    "...##...", "...##...", "...##...", "...##...", "...##...", "...##...", "...##...",
};
static const char *const DINO_BIRD_UP[] = {
    "....##......", "...####.....", "..######..#.", "###########.", ".#########..", "..#######...", "....###.....", "............",
};
static const char *const DINO_BIRD_DOWN[] = {
    "............", "..#.........", "..##....##..", "###########.", ".#########..", "..#######...", ".#.#####.#..", "#...###...#.",
};

#define DINO_SPRITE(name, width, rows_) static const dino_sprite_t name = {(width), (uint8_t)(sizeof(rows_) / sizeof((rows_)[0])), (rows_)}
DINO_SPRITE(DINO_SPRITE_RUN_A, 16, DINO_RUN_A);
DINO_SPRITE(DINO_SPRITE_RUN_B, 16, DINO_RUN_B);
DINO_SPRITE(DINO_SPRITE_CACTUS_S, 6, DINO_CACTUS_S);
DINO_SPRITE(DINO_SPRITE_CACTUS_L, 8, DINO_CACTUS_L);
DINO_SPRITE(DINO_SPRITE_BIRD_UP, 12, DINO_BIRD_UP);
DINO_SPRITE(DINO_SPRITE_BIRD_DOWN, 12, DINO_BIRD_DOWN);
#undef DINO_SPRITE

static const dino_sprite_t *const DINO_ALL_SPRITES[] = {
    &DINO_SPRITE_RUN_A, &DINO_SPRITE_RUN_B, &DINO_SPRITE_CACTUS_S, &DINO_SPRITE_CACTUS_L,
    &DINO_SPRITE_BIRD_UP, &DINO_SPRITE_BIRD_DOWN,
};
#define DINO_ALL_SPRITES_COUNT (sizeof(DINO_ALL_SPRITES) / sizeof(DINO_ALL_SPRITES[0]))
