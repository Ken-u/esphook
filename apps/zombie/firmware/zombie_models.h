#pragma once
#include "esphook/app_stack.h"

/* The voxel models the graveyard is built from (zombie_models.c), for esphook/app_stack.h. One voxel is ZM_VOXEL world
 * units on a side, the same units the game core uses for distance and height (a walker is 20 voxels = 100 units tall). */
#define ZM_VOXEL 5
#define ZM_KEYS "pqstkhmbSe"
#define ZM_LIT 1 /* the last letter, the eyes, glows */
/* Palette indexes of the zombies that vary: the shirt and its torn, darker edge. */
#define ZM_SHIRT 2
#define ZM_SHIRT_DARK 3
#define ZM_COLORS 10

extern const stack_model_t zm_walker, zm_runner, zm_brute, zm_tomb, zm_cross, zm_gun;
