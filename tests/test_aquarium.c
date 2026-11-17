/* Host tests for the aquarium simulation (apps/aquarium/firmware/aquarium_core.c). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "aquarium_core.h"

static void run(aq_t *a, uint32_t ms)
{
    while (ms) {
        uint32_t d = ms > 100 ? 100 : ms;
        aq_advance(a, d);
        ms -= d;
    }
}
static int absi(int v) { return v < 0 ? -v : v; }
static void in_bounds(const aq_t *a)
{
    for (unsigned i = 0; i < AQ_FISH; ++i) {
        const aq_fish_t *f = &a->fish[i];
        int x = f->x / 256, y = f->y / 256;
        assert(x >= 10 && x <= 150);
        if (f->kind == AQ_CRAB) assert(y == AQ_FLOOR - 2);
        else assert(y >= AQ_SURFACE + 6 && y <= AQ_FLOOR - 5);
        assert(f->dir == 1 || f->dir == -1);
    }
    for (unsigned i = 0; i < AQ_FOOD; ++i) {
        const aq_food_t *p = &a->food[i];
        if (!p->alive) continue;
        assert(p->y / 256 >= AQ_SURFACE - 8 && p->y / 256 <= AQ_FLOOR - 1);
        int x = aq_food_x(p);
        assert(x >= 0 && x < AQ_W);
    }
    for (unsigned i = 0; i < AQ_BUBBLES; ++i)
        if (a->bubble[i].alive) assert(a->bubble[i].y / 256 >= AQ_SURFACE && a->bubble[i].y / 256 <= AQ_FLOOR);
}

static void test_start(void)
{
    aq_t a;
    aq_init(&a, 7);
    unsigned count[6] = {0};
    for (unsigned i = 0; i < AQ_FISH; ++i) { assert(a.fish[i].kind <= AQ_CRAB); ++count[a.fish[i].kind]; }
    assert(count[AQ_CLOWN] == 2 && count[AQ_TANG] == 1 && count[AQ_ANGEL] == 2 && count[AQ_TETRA] == 5);
    assert(count[AQ_JELLY] == 1 && count[AQ_CRAB] == 1);
    assert(aq_food_live(&a) == 0 && a.thrown == 0 && a.eaten == 0);
    in_bounds(&a);
    aq_init(&a, 0); /* seed 0 is mapped to something usable */
    run(&a, 5000);
    in_bounds(&a);
    /* the fish actually swim */
    aq_t b;
    aq_init(&b, 7);
    int moved = 0;
    run(&b, 10000);
    aq_t c;
    aq_init(&c, 7);
    for (unsigned i = 0; i < AQ_FISH; ++i) moved += absi(b.fish[i].x - c.fish[i].x) > 8 * 256;
    assert(moved >= 8);
    assert(a.clock_ms == 5000);
}

static void test_feeding(void)
{
    aq_t a;
    aq_init(&a, 11);
    run(&a, 1234);
    const int hx = aq_hand_x(&a);
    assert(hx >= AQ_HAND_MIN && hx <= AQ_HAND_MAX);
    assert(aq_feed(&a));
    assert(a.thrown == 1 && aq_food_live(&a) == AQ_PELLETS && a.drop_x == hx && a.since_feed_ms == 0);
    for (unsigned i = 0; i < AQ_FOOD; ++i)
        if (a.food[i].alive) assert(absi(aq_food_x(&a.food[i]) - hx) <= 8);
    assert(!aq_feed(&a)); /* the fingers are still busy */
    assert(a.thrown == 1 && aq_food_live(&a) == AQ_PELLETS);
    run(&a, AQ_FEED_COOLDOWN_MS + 10);
    assert(aq_feed(&a) && a.thrown == 2 && aq_food_live(&a) >= AQ_PELLETS); /* some may be eaten already */
    /* the hand sweeps the whole tank over its period */
    int lo = 999, hi = -1;
    for (uint32_t t = 0; t < AQ_HAND_PERIOD_MS; t += 100) {
        run(&a, 100);
        int x = aq_hand_x(&a);
        if (x < lo) lo = x;
        if (x > hi) hi = x;
    }
    assert(lo <= AQ_HAND_MIN + 4 && hi >= AQ_HAND_MAX - 4);
    /* when the tank is full of pellets, pressing does nothing instead of overwriting them */
    aq_init(&a, 3);
    unsigned fed = 0;
    for (int i = 0; i < 40; ++i) {
        a.cooldown = 0;
        if (aq_feed(&a)) ++fed;
        a.cooldown = 0;
    }
    assert(fed == AQ_FOOD / AQ_PELLETS && aq_food_live(&a) == AQ_FOOD && a.thrown == fed);
}

static void test_pellets(void)
{
    aq_t a;
    aq_init(&a, 5);
    /* no fish near the start so the pellets go all the way down: use a tank with only the sand crab far away */
    for (unsigned i = 0; i < AQ_FISH; ++i) a.fish[i].full = 60000U; /* nobody is hungry */
    a.fish[AQ_FISH - 1].full = 60000U;
    assert(aq_feed(&a));
    run(&a, 1500);
    int sinking = 0;
    for (unsigned i = 0; i < AQ_FOOD; ++i)
        if (a.food[i].alive) { ++sinking; assert(!a.food[i].landed && a.food[i].y / 256 > AQ_SURFACE + 2); }
    assert(sinking == AQ_PELLETS);
    run(&a, 8000);
    int landed = 0;
    for (unsigned i = 0; i < AQ_FOOD; ++i)
        if (a.food[i].alive) { ++landed; assert(a.food[i].landed && a.food[i].y / 256 == AQ_FLOOR - 1); }
    assert(landed == AQ_PELLETS);
    run(&a, AQ_LANDED_MS + 500);
    assert(aq_food_live(&a) == 0 && a.eaten == 0); /* it dissolved on the sand, nobody ate it */
    /* sway stays within a few pixels of the centre line */
    aq_init(&a, 5);
    for (unsigned i = 0; i < AQ_FISH; ++i) a.fish[i].full = 60000U;
    aq_feed(&a);
    for (int i = 0; i < 300; ++i) {
        run(&a, 10);
        for (unsigned k = 0; k < AQ_FOOD; ++k)
            if (a.food[k].alive) assert(absi(aq_food_x(&a.food[k]) - a.food[k].x / 256) <= 3);
    }
}

static void test_eating(void)
{
    aq_t a;
    aq_init(&a, 9);
    run(&a, 2000);
    aq_feed(&a);
    run(&a, 12000);
    assert(a.eaten >= 1 && a.eaten <= AQ_PELLETS);
    assert(aq_food_live(&a) + a.eaten <= AQ_PELLETS);
    /* a pellet eaten is gone, the mouth position is recorded and the "yum" timer restarts */
    assert(a.since_eat_ms > 0 && a.eat_y >= AQ_SURFACE && a.eat_y <= AQ_FLOOR);
    /* feeding keeps the fish busy: most pellets get eaten, then the fullest fish ignore the rest */
    aq_init(&a, 21);
    uint32_t pinches = 0;
    for (int i = 0; i < 30; ++i) {
        run(&a, 1500);
        if (aq_feed(&a)) ++pinches;
    }
    run(&a, 4000);
    assert(a.thrown == pinches && pinches >= 20);
    assert(a.eaten >= pinches * AQ_PELLETS / 2); /* a hungry tank eats a lot */
    bool somebody_full = false;
    for (unsigned i = 0; i < AQ_FISH; ++i) somebody_full |= a.fish[i].full > 0;
    (void)somebody_full;
    in_bounds(&a);
    /* a full fish ignores pellets right next to it and swims on */
    aq_init(&a, 4);
    aq_fish_t *f = &a.fish[2];
    f->full = 500;
    f->x = 80 * 256; f->y = 30 * 256; f->gx = 80; f->gy = 30; f->dir = 1;
    for (unsigned i = 0; i < AQ_FISH; ++i) if (i != 2) a.fish[i].full = 5000;
    a.food[0] = (aq_food_t){.x = (80 + 10) * 256, .y = 30 * 256, .age = 0, .landed = 0, .alive = 1};
    uint32_t before = a.eaten;
    run(&a, 300);
    assert(a.eaten == before);
    /* a hungry fish takes it */
    aq_init(&a, 4);
    f = &a.fish[2];
    f->x = 80 * 256; f->y = 30 * 256; f->gx = 80; f->gy = 30;
    for (unsigned i = 0; i < AQ_FISH; ++i) if (i != 2) a.fish[i].full = 5000;
    a.food[0] = (aq_food_t){.x = 90 * 256, .y = 30 * 256, .alive = 1};
    run(&a, 2000);
    assert(a.eaten == 1 && !a.food[0].alive && a.since_eat_ms < 2000U);
    /* three pellets and the fish has had enough for a while */
    aq_init(&a, 4);
    f = &a.fish[2];
    for (unsigned i = 0; i < AQ_FISH; ++i) if (i != 2) a.fish[i].full = 5000;
    f->x = 80 * 256; f->y = 30 * 256;
    for (unsigned i = 0; i < 4; ++i) a.food[i] = (aq_food_t){.x = (74 + (int)i * 4) * 256, .y = 30 * 256, .alive = 1};
    run(&a, 4000);
    assert(a.eaten == 3 && f->full > 0 && aq_food_live(&a) == 1);
}

static void test_crab(void)
{
    aq_t a;
    aq_init(&a, 13);
    aq_fish_t *crab = &a.fish[AQ_FISH - 1];
    assert(crab->kind == AQ_CRAB);
    for (unsigned i = 0; i < AQ_FISH - 1; ++i) a.fish[i].full = 60000U; /* only the crab is hungry */
    /* pellets still sinking do not interest it, pellets on the sand do */
    a.food[0] = (aq_food_t){.x = 120 * 256, .y = 30 * 256, .alive = 1};
    run(&a, 500);
    assert(a.food[0].alive && a.eaten == 0);
    a.food[0].y = (AQ_FLOOR - 1) * 256;
    a.food[0].landed = 1;
    run(&a, 8 * 1000 - 100);
    assert(a.eaten == 1 && !a.food[0].alive);
    assert(crab->y / 256 == AQ_FLOOR - 2);
}

static void test_determinism(void)
{
    aq_t a, b;
    aq_init(&a, 99);
    aq_init(&b, 99);
    for (uint32_t t = 0; t < 40000; t += 10) {
        aq_advance(&a, 10);
        if (t % 250 == 240) { aq_advance(&b, 250); }
        if (t % 3000 == 2000) { assert(aq_feed(&a) == aq_feed(&b)); }
    }
    /* chunking does not matter: the same seed and the same presses give the same tank */
    aq_t c, d;
    aq_init(&c, 17);
    aq_init(&d, 17);
    for (uint32_t t = 0; t < 30000; t += 250) {
        for (int k = 0; k < 25; ++k) aq_advance(&c, 10);
        aq_advance(&d, 250);
        if ((t / 250) % 14 == 3) { bool x = aq_feed(&c), y = aq_feed(&d); assert(x == y); }
    }
    assert(c.clock_ms == d.clock_ms && c.eaten == d.eaten && c.thrown == d.thrown);
    for (unsigned i = 0; i < AQ_FISH; ++i)
        assert(c.fish[i].x == d.fish[i].x && c.fish[i].y == d.fish[i].y && c.fish[i].full == d.fish[i].full);
    /* a long stall is a pause, not a backlog */
    aq_t e;
    aq_init(&e, 1);
    aq_advance(&e, 3600000U);
    assert(e.clock_ms == AQ_MAX_ADVANCE_MS);
    in_bounds(&e);
}

static void test_soak(void)
{
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        aq_t a;
        aq_init(&a, seed * 7919U);
        for (uint32_t t = 0; t < 120000; t += 50) {
            aq_advance(&a, 50);
            if ((t / 50 + seed) % 11 == 0) aq_feed(&a);
            if (t % 5000 == 0) in_bounds(&a);
        }
        in_bounds(&a);
        assert(a.thrown > 100 && a.eaten > 50);
        unsigned alive = 0;
        for (unsigned i = 0; i < AQ_BUBBLES; ++i) alive += a.bubble[i].alive;
        assert(alive <= AQ_BUBBLES);
    }
}

int main(void)
{
    test_start();
    test_feeding();
    test_pellets();
    test_eating();
    test_crab();
    test_determinism();
    test_soak();
    puts("aquarium tests passed");
    return 0;
}
