#pragma once
/* The voxel models of the star field, drawn with the slice-stacking kit (esphook/app_stack.h, docs/stack-render.md): three
 * asteroids, the enemy fighter and the cannons on your own wings. A model is ASCII art, a top view per layer, bottom layer
 * first; in it row 0 is the far side and the last row faces you at yaw 0, so a fighter has its nose on the last row. An
 * asteroid is 9 voxels across and a fighter 15 (its wings), whatever size the game gives them: the voxel is set per object. */
#include "esphook/app_stack.h"

#define SM_ROCK_KEYS "abcdg"
#define SM_ROCK_LIT 1   /* the ore glows */
#define SM_ROCK_SIZE 9
#define SM_FOE_KEYS "abcdye"
#define SM_FOE_LIT 2    /* the amber cannons and the red eye glow */
#define SM_FOE_WIDE 15

extern const stack_model_t sm_rock_a, sm_rock_b, sm_rock_c, sm_fighter, sm_cannon;
