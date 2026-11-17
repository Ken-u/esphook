#pragma once
#include <stddef.h>
#include <stdint.h>

/* Pixel art: '.' is transparent and every other character selects a colour through the key string given to
 * gfx_blit (see DINO_SPRITE_KEYS and the palettes in app.c). Rows must all be `w` characters wide
 * (tests/test_dino.c checks this, so a missing dot cannot silently shear a sprite). */
typedef struct {
    uint8_t w, h;
    const char *const *rows;
} dino_sprite_t;
/* o outline, g body, d body shade, l belly, w eye white, k pupil, y beak tip, c/C cactus shade/light,
 * a/b/B/e bird outline, body, wing light, beak. */
#define DINO_SPRITE_KEYS "ogdlwkycCabBepf"

static const char *const DINO_RUN_A[] = {
    "..........oooooo..",
    ".........oggggggo.",
    ".........oggwkggg.",
    ".........oggggggg.",
    ".........ogggggoo.",
    ".........ogggggd..",
    "..o......oggggoo..",
    ".ogo...oogggggo...",
    ".oggooggggggggo.o.",
    ".ogggggggggggggoo.",
    "..oggggggggggglo..",
    "..ogggggggggglldo.",
    "...oggggggggllo...",
    "....ogggggggdo....",
    ".....oggdoggdo....",
    ".....ogo.oggo.....",
    ".....og..ogo......",
    "....ooo..ooo......",
};

static const char *const DINO_RUN_B[] = {
    "..........oooooo..",
    ".........oggggggo.",
    ".........oggwkggg.",
    ".........oggggggg.",
    ".........ogggggoo.",
    ".........ogggggd..",
    "..o......oggggoo..",
    ".ogo...oogggggo...",
    ".oggooggggggggo.o.",
    ".ogggggggggggggoo.",
    "..oggggggggggglo..",
    "..ogggggggggglldo.",
    "...oggggggggllo...",
    "....ogggggggdo....",
    ".....oggdoggdo....",
    ".....oggo.ogo.....",
    ".....ogo..og......",
    ".....ooo..ooo.....",
};

static const char *const DINO_JUMP_ROWS[] = {
    "..........oooooo..",
    ".........oggggggo.",
    ".........oggwkggg.",
    ".........oggggggg.",
    ".........ogggggoo.",
    ".........ogggggd..",
    "..o......oggggoo..",
    ".ogo...oogggggo...",
    ".oggooggggggggo.o.",
    ".ogggggggggggggoo.",
    "..oggggggggggglo..",
    "..ogggggggggglldo.",
    "...oggggggggllo...",
    "....ogggggggdo....",
    ".....oggdogggo....",
    ".....ogoo.oggo....",
    ".....ooo..ooo.....",
    "..................",
};

static const char *const DINO_DEAD_ROWS[] = {
    "..........oooooo..",
    ".........oggggggo.",
    ".........ogkwkggg.",
    ".........oggkggggo",
    ".........ogggggoo.",
    ".........ogggggd..",
    "..o......oggggoo..",
    ".ogo...oogggggo...",
    ".oggooggggggggo.o.",
    ".ogggggggggggggoo.",
    "..oggggggggggglo..",
    "..ogggggggggglldo.",
    "...oggggggggllo...",
    "....ogggggggdo....",
    ".....oggdoggdo....",
    ".....ogo.oggo.....",
    ".....og..ogo......",
    "....ooo..ooo......",
};

static const char *const DINO_CACTUS_S[] = {
    "..oCCo..",
    "..oCco..",
    "o.oCco.o",
    "oCoCcoCo",
    "oCCCccCo",
    ".oooCcoo",
    "...oCco.",
    "...oCco.",
    "...oCco.",
    "...oCco.",
    "...oCco.",
    "..oooooo",
};

static const char *const DINO_CACTUS_L[] = {
    "...oCCo...",
    "...oCco...",
    ".o.oCco.o.",
    "oCooCcooCo",
    "oCooCcooCo",
    "oCcoCcocCo",
    ".oCCCccCo.",
    "..oooCcoo.",
    "....oCco..",
    "....oCco..",
    "....oCco..",
    "....oCco..",
    "....oCco..",
    "....oCco..",
    "....oCco..",
    "....oCco..",
    "...ooooooo",
};

static const char *const DINO_BIRD_UP[] = {
    "......a.......",
    ".....aBa......",
    "....aBBBa.....",
    "...aBBBBBaa...",
    ".aaBBBBBBBeea.",
    "aBBBBBBBkBeey.",
    ".aaBBBBBBBeea.",
    "...aBBBBBaa...",
    "....aaaaa.....",
};

static const char *const DINO_BIRD_DOWN[] = {
    "..............",
    "..............",
    "...aaaaa......",
    ".aaBBBBBaa....",
    "aBBBBBBBBkeea.",
    ".aBBBBBBBBeey.",
    "..aBBBBBBBeea.",
    "...aBBBBBaa...",
    "....a.a.a.....",
};

#define DINO_SPRITE(name, width, rows_) static const dino_sprite_t name = {(width), (uint8_t)(sizeof(rows_) / sizeof((rows_)[0])), (rows_)}
DINO_SPRITE(DINO_SPRITE_RUN_A, 18, DINO_RUN_A);
DINO_SPRITE(DINO_SPRITE_RUN_B, 18, DINO_RUN_B);
DINO_SPRITE(DINO_SPRITE_JUMP, 18, DINO_JUMP_ROWS);
DINO_SPRITE(DINO_SPRITE_DEAD, 18, DINO_DEAD_ROWS);
DINO_SPRITE(DINO_SPRITE_CACTUS_S, 8, DINO_CACTUS_S);
DINO_SPRITE(DINO_SPRITE_CACTUS_L, 10, DINO_CACTUS_L);
DINO_SPRITE(DINO_SPRITE_BIRD_UP, 14, DINO_BIRD_UP);
DINO_SPRITE(DINO_SPRITE_BIRD_DOWN, 14, DINO_BIRD_DOWN);
#undef DINO_SPRITE

static const dino_sprite_t *const DINO_ALL_SPRITES[] = {
    &DINO_SPRITE_RUN_A, &DINO_SPRITE_RUN_B, &DINO_SPRITE_JUMP, &DINO_SPRITE_DEAD, &DINO_SPRITE_CACTUS_S,
    &DINO_SPRITE_CACTUS_L, &DINO_SPRITE_BIRD_UP, &DINO_SPRITE_BIRD_DOWN,
};
#define DINO_ALL_SPRITES_COUNT (sizeof(DINO_ALL_SPRITES) / sizeof(DINO_ALL_SPRITES[0]))
