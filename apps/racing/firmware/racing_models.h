#pragma once
/* The voxel models of the road, drawn with the slice-stacking kit (esphook/app_stack.h, docs/stack-render.md): three cars,
 * two trees, a lamp post and the checkpoint gantry. A model is ASCII art, a top view per layer, bottom layer first; in it
 * row 0 is the far side and the last row faces you at yaw 0, so a car is drawn nose first and its tail is next to you. One
 * voxel is RM_VOXEL millimetres, which makes a car about 3.1 m wide, 4.6 m long and 1.7 m high, as the flat ones were. */
#include "esphook/app_stack.h"

#define RM_VOXEL 285
#define RM_CAR_KEYS "bBgwkyr"
#define RM_CAR_LIT 1   /* the tail lights glow */
#define RM_BODY 0      /* palette indexes of a car: the paint, its shade, and the tail light that can be dimmed or lit */
#define RM_BODY_SHADE 1
#define RM_TAIL 6
#define RM_CAR_COLORS 7
#define RM_TREE_KEYS "nfdF"

extern const stack_model_t rm_coupe, rm_sedan, rm_van, rm_pine, rm_round, rm_post, rm_gantry;
