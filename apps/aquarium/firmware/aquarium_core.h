#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 水族箱: a small reef tank. A hand swings back and forth above the water; one press drops a pinch of fish food
 * where it is, the pellets sink, sway, and the fish swim over to eat them (the crab waits for what reaches the
 * sand). Fish that have eaten enough lose interest for a while. The core is pure C, has no ESP/LVGL dependency
 * and advances in fixed 10 ms steps, so the result never depends on how often it is ticked.
 * Positions are 1/256 px, speeds 1/256 px per step. */
#define AQ_W 160
#define AQ_SURFACE 9 /* row of the water line */
#define AQ_FLOOR 57  /* row where the sand starts */
#define AQ_FISH 12
#define AQ_FOOD 20
#define AQ_BUBBLES 14
#define AQ_PELLETS 4         /* pellets in one pinch */
#define AQ_STEP_MS 10U
#define AQ_MAX_ADVANCE_MS 250U /* a longer gap between calls is a pause, not a backlog */
#define AQ_FEED_COOLDOWN_MS 160U
#define AQ_LANDED_MS 8000U   /* pellets dissolve after this long on the sand */
#define AQ_FULL_MS 6000U     /* how long a fish that has eaten its fill ignores food */
#define AQ_HAND_PERIOD_MS 7000U
#define AQ_HAND_MIN 16
#define AQ_HAND_MAX 144
#define AQ_CHEST_X 25        /* where the treasure chest blows its bubbles */
#define AQ_ANEMONE_X 126     /* the clownfish's home */
#define AQ_COUNT_MAX 99999U
#define AQ_AGE_MAX 60000U

typedef enum { AQ_CLOWN, AQ_TANG, AQ_ANGEL, AQ_TETRA, AQ_JELLY, AQ_CRAB } aq_kind_t;

typedef struct {
    int32_t x, y;     /* centre, 1/256 px */
    int16_t vx, vy;   /* 1/256 px per step */
    int16_t gx, gy;   /* where it is heading, whole px */
    uint16_t wander;  /* steps until it picks another goal */
    uint16_t full;    /* steps left of not wanting food */
    uint16_t tail;    /* swim phase (the tail wag, the jellyfish pulse) */
    uint8_t kind, eaten;
    int8_t dir;       /* +1 swims right, -1 left */
    uint8_t chase;    /* heading for a pellet */
} aq_fish_t;
typedef struct {
    int32_t x, y;     /* 1/256 px; x is the centre line the pellet sways around */
    uint16_t age;     /* steps since it was dropped, until it lands */
    uint16_t landed;  /* steps since it reached the sand, 0 while it sinks */
    uint8_t alive;
} aq_food_t;
typedef struct {
    int16_t x, y;     /* x in px, y in 1/256 px */
    uint8_t speed, size, alive; /* speed in 1/256 px per step, size 0..2 */
} aq_bubble_t;
typedef struct {
    aq_fish_t fish[AQ_FISH];
    aq_food_t food[AQ_FOOD];
    aq_bubble_t bubble[AQ_BUBBLES];
    uint32_t rng, clock_ms, carry_ms, thrown, eaten;
    uint32_t since_feed_ms, since_eat_ms; /* saturating at AQ_AGE_MAX; for the splash and the little "yum" */
    uint16_t cooldown, bubble_wait, school_wait;
    int16_t school_x, school_y, school_gx, school_gy; /* the tetras' meeting point, whole px */
    int16_t eat_x, eat_y;                              /* where the latest pellet was eaten, whole px */
    int16_t drop_x;                                    /* where the latest pinch fell, whole px */
} aq_t;

void aq_init(aq_t *a, uint32_t seed);
/* Advances the tank by dt_ms (at most AQ_MAX_ADVANCE_MS per call; the rest is dropped). */
void aq_advance(aq_t *a, uint32_t dt_ms);
/* Drops a pinch of food at the hand; false while the previous pinch is still falling off the fingers or the tank is
 * already full of pellets. */
bool aq_feed(aq_t *a);
/* The hand sweeps AQ_HAND_MIN..AQ_HAND_MAX along the water line. */
int aq_hand_x(const aq_t *a);
/* Pellet x in whole px: the centre line plus a little sway while it sinks. */
int aq_food_x(const aq_food_t *f);
int aq_fish_len(unsigned kind); /* body length in px, for drawing and for where the mouth is */
unsigned aq_food_live(const aq_t *a);
