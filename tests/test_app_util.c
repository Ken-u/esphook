#include "esphook/app_math.h"
#include "esphook/app_rand.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* The cores used to carry private copies of everything below; these references are those copies, so a green run proves
 * that moving the games onto the shared headers changed no behaviour. */
static uint32_t ref_xorshift(uint32_t *s)
{
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return *s;
}
static int ref_wave127(unsigned phase)
{
    const int x = (int)(phase & 127U), v = x * (128 - x) * 127 / 4096;
    return (phase & 128U) ? -v : v;
}
static uint32_t ref_hash(uint32_t z)
{
    z += 0x9e3779b9U; z ^= z >> 16; z *= 0x85ebca6bU; z ^= z >> 13; z *= 0xc2b2ae35U;
    return z ^ (z >> 16);
}

static void ints(void)
{
    assert(app_abs(-7) == 7 && app_abs(7) == 7 && app_abs(0) == 0);
    assert(app_min(-3, 4) == -3 && app_max(-3, 4) == 4 && app_min(5, 5) == 5);
    assert(app_clamp(5, 0, 10) == 5 && app_clamp(-5, 0, 10) == 0 && app_clamp(50, 0, 10) == 10 && app_clamp(0, 0, 10) == 0);
    assert(app_clamp(-300, -256, 256) == -256);
    assert(app_lerp(10, 20, 0, 4) == 10 && app_lerp(10, 20, 4, 4) == 20 && app_lerp(10, 20, 1, 4) == 12);
    assert(app_lerp(20, 10, 1, 4) == 18 && app_lerp(0, 100, 7, 256) == 2); /* truncates toward zero */
    assert(app_lerp(30, 30, 3, 5) == 30);
}
static void saturation(void)
{
    assert(app_sat_add(0, 5, 100) == 5 && app_sat_add(95, 5, 100) == 100 && app_sat_add(96, 5, 100) == 100);
    assert(app_sat_add(100, 1, 100) == 100 && app_sat_add(0, 100, 100) == 100 && app_sat_add(0, 101, 100) == 100);
    assert(app_sat_add(UINT32_MAX - 1U, UINT32_MAX - 1U, UINT32_MAX) == UINT32_MAX); /* never wraps */
    assert(app_sat_add(5, UINT32_MAX, 100) == 100 && app_sat_add(UINT32_MAX, 5, UINT32_MAX) == UINT32_MAX);
    assert(app_age_add(0, 10) == 10 && app_age_add(APP_AGE_MAX - 1U, 5) == APP_AGE_MAX && app_age_add(APP_AGE_MAX, 10) == APP_AGE_MAX);
    uint32_t age = 0; /* a counter left running for hours stays put */
    for (unsigned i = 0; i < 100000; ++i) age = app_age_add(age, 10);
    assert(age == APP_AGE_MAX);
}
static void roots(void)
{
    for (uint32_t v = 0; v < 70000U; ++v) {
        const uint64_t r = app_isqrt(v);
        assert(r * r <= v && (r + 1U) * (r + 1U) > v);
    }
    uint32_t seed = 12345U;
    for (unsigned i = 0; i < 200000; ++i) {
        const uint32_t v = ref_xorshift(&seed);
        const uint64_t r = app_isqrt(v);
        assert(r * r <= v && (r + 1U) * (r + 1U) > v);
    }
    assert(app_isqrt(UINT32_MAX) == 65535U && app_isqrt(0) == 0 && app_isqrt(1) == 1 && app_isqrt(65535U * 65535U) == 65535U);
}
static void waves(void)
{
    for (unsigned phase = 0; phase < 1024U; ++phase) {
        assert(app_wave127(phase) == ref_wave127(phase) && app_wave127(phase) == app_wave127(phase + 256U));
        assert(app_wave127(phase) >= -127 && app_wave127(phase) <= 127 && app_wave127(phase + 128U) == -app_wave127(phase));
        assert(app_sin256(phase) >= -256 && app_sin256(phase) <= 256 && app_sin256(phase + 128U) == -app_sin256(phase));
        assert(app_sin256(phase) == app_sin256(phase + 256U));
    }
    assert(app_wave127(0) == 0 && app_wave127(64) == 127 && app_wave127(128) == 0 && app_wave127(192) == -127);
    assert(app_sin256(0) == 0 && app_sin256(64) == 256 && app_sin256(192) == -256);
    assert(app_wave127(10) == 36);
}
static void hashing(void)
{
    static uint8_t seen[1U << 16];
    for (uint32_t i = 0; i < 20000U; ++i) {
        const uint32_t h = app_hash32(i);
        assert(h == ref_hash(i));
        seen[h >> 16] = 1;
    }
    unsigned buckets = 0;
    for (unsigned i = 0; i < sizeof seen; ++i) buckets += seen[i];
    assert(buckets > 14000U); /* 20000 draws over 65536 buckets: ~17000 expected for a well mixed hash */
    for (uint32_t i = 1; i < 5000U; ++i) assert(app_hash32(i) != app_hash32(i - 1U)); /* a bijection never repeats a neighbour */
}
static void random_numbers(void)
{
    uint32_t s = 1U;
    assert(app_rand_next(&s) == 270369U && s == 270369U); /* xorshift32 13/17/5 from 1 */
    uint32_t a = 0xC0FFEEU, b = 0xC0FFEEU;
    for (unsigned i = 0; i < 100000; ++i) assert(app_rand_next(&a) == ref_xorshift(&b)); /* the sequences the games always drew */
    uint32_t zero = 0;
    const uint32_t first = app_rand_next(&zero);
    assert(first != 0 && zero == first && app_rand_seed(0) == APP_RAND_FALLBACK && app_rand_seed(77U) == 77U);
    uint32_t again = APP_RAND_FALLBACK;
    assert(app_rand_next(&again) == first); /* an unseeded state behaves exactly like the fallback seed */
    for (unsigned i = 0; i < 100000; ++i) assert(app_rand_next(&zero) != 0U); /* xorshift never reaches 0 from non-zero */

    uint32_t r = 99U;
    unsigned seen[10] = {0};
    for (unsigned i = 0; i < 10000; ++i) {
        const int v = app_rand_range(&r, 0, 9);
        assert(v >= 0 && v <= 9);
        ++seen[v];
    }
    for (unsigned i = 0; i < 10; ++i) assert(seen[i] > 800U && seen[i] < 1200U);
    for (unsigned i = 0; i < 1000; ++i) {
        const int v = app_rand_range(&r, -50, -45), w = app_rand_range(&r, 7, 7);
        assert(v >= -50 && v <= -45 && w == 7);
    }
    unsigned never = 0, always = 0, quarter = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        never += app_rand_chance(&r, 0);
        always += app_rand_chance(&r, 100);
        quarter += app_rand_chance(&r, 25);
    }
    assert(never == 0 && always == 100000U && quarter > 23000U && quarter < 27000U);
}
static void stepping(void)
{
    uint32_t carry = 0;
    assert(app_step_take(&carry, 4, 10, 0) == 0 && carry == 4);
    assert(app_step_take(&carry, 4, 10, 0) == 0 && carry == 8);
    assert(app_step_take(&carry, 4, 10, 0) == 1 && carry == 2);
    assert(app_step_take(&carry, 38, 10, 0) == 4 && carry == 0);
    assert(app_step_take(&carry, 1000, 10, 200) == 20 && carry == 0); /* a stalled frame cannot burst */
    assert(app_step_take(&carry, 1000, 10, 0) == 100 && carry == 0); /* 0 = no cap */
    assert(app_step_take(&carry, 5, 0, 0) == 0 && carry == 0);      /* a zero step never divides by zero */
    /* Exactly what the cores did with a while loop, for frame times of every kind. */
    uint32_t fast = 0, slow = 0, seed = 7U;
    unsigned fast_steps = 0, slow_steps = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        uint32_t dt = ref_xorshift(&seed) % 700U;
        fast_steps += app_step_take(&fast, dt, 20U, 200U);
        if (dt > 200U) dt = 200U;
        slow += dt;
        while (slow >= 20U) { slow -= 20U; ++slow_steps; }
        assert(fast == slow && fast_steps == slow_steps && fast < 20U);
    }
}
static double ref_sqrt(double v) /* the maths library is not linked into this test */
{
    double r = v > 1.0 ? v : 1.0;
    for (int i = 0; i < 40; ++i) r = 0.5 * (r + v / r);
    return r;
}
static double ref_atan_turns(double x) /* in 1/256 turn: halve the angle twice, then a short series */
{
    const double sign = x < 0 ? -1.0 : 1.0;
    double a = x < 0 ? -x : x;
    const int flip = a > 1.0;
    if (flip) a = 1.0 / a;
    a = a / (1.0 + ref_sqrt(1.0 + a * a));
    a = a / (1.0 + ref_sqrt(1.0 + a * a));
    double term = a, sum = a;
    for (int k = 1; k < 12; ++k) { term *= -a * a; sum += term / (2 * k + 1); }
    double rad = 4.0 * sum;
    if (flip) rad = 1.5707963267948966 - rad;
    return sign * rad * 256.0 / 6.283185307179586;
}
static void angles(void)
{
    assert(app_atan256(0, 100) == 0 && app_atan256(100, 100) == 32 && app_atan256(-100, 100) == -32);
    assert(app_atan256(5, 0) == 64 && app_atan256(-5, 0) == -64 && app_atan256(0, 0) == 0 && app_atan256(7, -3) == 64);
    for (int den = 1; den <= 400; den += 7) {
        int previous = -65;
        for (int num = -800; num <= 800; num += 9) {
            const int got = app_atan256(num, den);
            const double want = ref_atan_turns((double)num / den), err = (double)got - want;
            assert(err < 0.8 && err > -0.8 && got >= -64 && got <= 64);
            assert(app_atan256(-num, den) == -got && got >= previous); /* odd and never going back */
            previous = got;
        }
    }
    assert(app_atan256(INT32_MAX / 2, 1) == 64 && app_atan256(1, INT32_MAX / 2) == 0); /* huge ratios do not overflow */
}
int main(void)
{
    ints();
    saturation();
    roots();
    waves();
    angles();
    hashing();
    random_numbers();
    stepping();
    puts("app util: integer maths, saturation, roots, waves, angles, hashing, deterministic random and fixed steps passed");
    return 0;
}
