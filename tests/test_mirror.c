#include "mirror_codec.h"
#include "mirror_testenc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char vector[2048];
static void load_vector(void)
{
    FILE *f = fopen("tests/data/mirror_strip.txt", "rb");
    assert(f);
    size_t n = fread(vector, 1, sizeof vector - 1U, f);
    fclose(f);
    while (n && (vector[n - 1U] == '\n' || vector[n - 1U] == '\r')) --n;
    vector[n] = 0;
}
static uint32_t rng = 12345U;
static uint32_t next(void) { rng = rng * 1664525U + 1013904223U; return rng >> 8; }

static void test_crc_and_base64(void)
{
    assert(mirror_crc16((const uint8_t *)"123456789", 9) == 0x29B1U); /* CRC-16/CCITT-FALSE check value */
    assert(mirror_crc16((const uint8_t *)"", 0) == 0xFFFFU);
    uint8_t data[96], back[96];
    char text[160];
    for (unsigned round = 0; round < 40U; ++round) {
        for (unsigned i = 0; i < sizeof data; ++i) data[i] = (uint8_t)next();
        mirror_test_b64(data, sizeof data, text);
        assert(strlen(text) == sizeof data / 3U * 4U);
        assert(mirror_b64_decode(text, strlen(text), back, sizeof back) == (int)sizeof back);
        assert(!memcmp(data, back, sizeof data));
    }
    assert(mirror_b64_decode("AAA", 3, back, sizeof back) < 0);    /* not a whole number of groups */
    assert(mirror_b64_decode("AA=A", 4, back, sizeof back) < 0);   /* no padding in this protocol */
    assert(mirror_b64_decode("AA*A", 4, back, sizeof back) < 0);
    assert(mirror_b64_decode("AAAA", 4, back, 2) < 0);             /* does not fit */
    assert(mirror_b64_decode("", 0, back, sizeof back) == 0);
}
static void test_vector_from_the_pc_codec(void)
{
    /* The line the Python codec makes for the same payload (apps/mirror/pc/test_mirror.py checks the other half). */
    static mirror_strip_t strip;
    assert(strlen(vector) == 915U);
    assert(mirror_parse_strip(vector, &strip) == MIRROR_OK);
    assert(strip.frame == 37U && strip.index == 5U);
    for (unsigned k = 0; k < MIRROR_COLOURS; ++k) assert(strip.data[k * 2U] == (k * 0x1111U & 0xFFU) && strip.data[k * 2U + 1U] == (k * 0x1111U >> 8));
    static uint16_t canvas[MIRROR_W * MIRROR_H];
    for (unsigned i = 0; i < MIRROR_W * MIRROR_H; ++i) canvas[i] = 0xBEEFU;
    mirror_expand(&strip, canvas);
    for (unsigned y = 0; y < MIRROR_H; ++y)
        for (unsigned x = 0; x < MIRROR_W; ++x) {
            const uint16_t px = canvas[y * MIRROR_W + x];
            if (y < 5U * MIRROR_ROWS || y >= 6U * MIRROR_ROWS) assert(px == 0xBEEFU); /* only its own rows are painted */
            else assert(px == ((x + (y - 5U * MIRROR_ROWS)) % 16U) * 0x1111U);
        }
}
static void test_refusals(void)
{
    static mirror_strip_t strip;
    char line[sizeof vector + 16U], broken[sizeof vector + 16U];
    assert(strlen(vector) < sizeof line);
    assert(mirror_parse_strip("", &strip) == MIRROR_ERR_ARGS);
    assert(mirror_parse_strip("mirror hello", &strip) == MIRROR_ERR_ARGS);
    assert(mirror_parse_strip("robot mirror status", &strip) == MIRROR_ERR_ARGS);
    /* Numbers out of range or malformed. */
    const char *rest = vector + strlen("mirror s 37 5 44aa ");
    const char *bad_heads[] = {"mirror s 256 5 ", "mirror s 37 8 ", "mirror s -1 5 ", "mirror s 37  5 ", "mirror s 37 5", "mirror s a 5 ", "mirror s 37 5 44a ",
                               "mirror s 37 5 44aag ", "mirror s 37 5 44AAX ", "mirror s 99999 5 ", "mirror s 37 5 44aa  "};
    for (unsigned i = 0; i < sizeof bad_heads / sizeof *bad_heads; ++i) {
        snprintf(line, sizeof line, "%s%s", bad_heads[i], rest);
        assert(mirror_parse_strip(line, &strip) == MIRROR_ERR_ARGS);
    }
    /* Uppercase hex is fine. */
    snprintf(line, sizeof line, "mirror s 37 5 44AA %s", rest);
    assert(mirror_parse_strip(line, &strip) == MIRROR_OK);
    /* Wrong payload length. */
    snprintf(line, sizeof line, "%s", vector);
    line[strlen(line) - 4U] = 0;
    assert(mirror_parse_strip(line, &strip) == MIRROR_ERR_ARGS);
    snprintf(line, sizeof line, "%sAAAA", vector);
    assert(mirror_parse_strip(line, &strip) == MIRROR_ERR_ARGS);
    snprintf(line, sizeof line, "%s ", vector);
    assert(mirror_parse_strip(line, &strip) == MIRROR_ERR_ARGS);
    /* A character that is not base64. */
    snprintf(broken, sizeof broken, "%s", vector);
    broken[100] = '*';
    assert(mirror_parse_strip(broken, &strip) == MIRROR_ERR_B64);
    /* A damaged payload is a checksum error, and says which strip it was. */
    snprintf(broken, sizeof broken, "%s", vector);
    broken[200] = broken[200] == 'A' ? 'B' : 'A';
    memset(&strip, 0, sizeof strip);
    assert(mirror_parse_strip(broken, &strip) == MIRROR_ERR_CRC);
    assert(strip.frame == 37U && strip.index == 5U);
    assert(!strcmp(mirror_status_name(MIRROR_ERR_CRC), "crc") && !strcmp(mirror_status_name(MIRROR_ERR_B64), "b64") &&
           !strcmp(mirror_status_name(MIRROR_ERR_ARGS), "args") && !strcmp(mirror_status_name(MIRROR_OK), "ok"));
}
static void test_made_in_c_round_trip(void)
{
    static mirror_strip_t strip;
    static uint16_t canvas[MIRROR_W * MIRROR_H];
    uint8_t payload[MIRROR_PAYLOAD];
    char line[sizeof vector + 16U];
    mirror_test_payload(payload, 0xF800U, 0x001FU, 3U);
    mirror_test_line(line, sizeof line, 255U, 7U, payload);
    assert(line[0] && strlen(line) == strlen("mirror s 255 7 xxxx ") + MIRROR_B64);
    assert(mirror_parse_strip(line, &strip) == MIRROR_OK && strip.frame == 255U && strip.index == 7U);
    mirror_expand(&strip, canvas);
    for (unsigned y = 0; y < MIRROR_ROWS; ++y)
        for (unsigned x = 0; x < MIRROR_W; ++x) assert(canvas[(7U * MIRROR_ROWS + y) * MIRROR_W + x] == (y < 3U ? 0xF800U : 0x001FU));
    assert(canvas[0] == 0); /* the rows above were not touched */
}
static void test_the_ring(void)
{
    static mirror_ring_t ring;
    mirror_ring_reset(&ring);
    assert(mirror_ring_free(&ring) == MIRROR_RING && !mirror_ring_peek(&ring));
    mirror_ring_release(&ring); /* releasing nothing changes nothing */
    assert(mirror_ring_free(&ring) == MIRROR_RING);
    for (unsigned i = 0; i < MIRROR_RING; ++i) {
        mirror_strip_t *slot = mirror_ring_reserve(&ring);
        assert(slot);
        slot->frame = (uint8_t)i; slot->index = (uint8_t)(i % MIRROR_STRIPS);
        assert(mirror_ring_free(&ring) == MIRROR_RING - i); /* a reserved slot is not taken until it is committed */
        mirror_ring_commit(&ring);
    }
    assert(mirror_ring_free(&ring) == 0 && !mirror_ring_reserve(&ring));
    for (unsigned i = 0; i < MIRROR_RING; ++i) {
        const mirror_strip_t *slot = mirror_ring_peek(&ring);
        assert(slot && slot->frame == i);
        mirror_ring_release(&ring);
        assert(mirror_ring_free(&ring) == i + 1U);
    }
    assert(!mirror_ring_peek(&ring));
    /* Far past the wrap of the 8-bit counters, with the two sides running at different speeds. */
    unsigned produced = 0, consumed = 0;
    for (unsigned step = 0; step < 5000U; ++step) {
        if (next() % 3U) {
            mirror_strip_t *slot = mirror_ring_reserve(&ring);
            if (slot) { slot->frame = (uint8_t)produced; mirror_ring_commit(&ring); ++produced; }
        } else {
            const mirror_strip_t *slot = mirror_ring_peek(&ring);
            if (slot) { assert(slot->frame == (uint8_t)consumed); mirror_ring_release(&ring); ++consumed; }
        }
        assert(produced - consumed <= MIRROR_RING && mirror_ring_free(&ring) == MIRROR_RING - (produced - consumed));
    }
    assert(produced > 600U && consumed > 600U);
}
static void test_garbage_never_crashes(void)
{
    static mirror_strip_t strip;
    char line[sizeof vector + 16U];
    unsigned accepted = 0;
    for (unsigned round = 0; round < 4000U; ++round) {
        snprintf(line, sizeof line, "%s", vector);
        const unsigned edits = 1U + next() % 4U;
        for (unsigned e = 0; e < edits; ++e) line[next() % strlen(line)] = (char)(next() % 256U);
        if (next() % 5U == 0U) line[next() % strlen(line)] = 0;
        const mirror_status_t st = mirror_parse_strip(line, &strip);
        assert(st == MIRROR_OK || st == MIRROR_ERR_ARGS || st == MIRROR_ERR_B64 || st == MIRROR_ERR_CRC);
        if (st == MIRROR_OK) { ++accepted; assert(strip.index < MIRROR_STRIPS); } /* an edit may land on a digit that is still valid */
    }
    assert(accepted < 400U);
}
int main(void)
{
    load_vector();
    test_crc_and_base64();
    test_vector_from_the_pc_codec();
    test_refusals();
    test_made_in_c_round_trip();
    test_the_ring();
    test_garbage_never_crashes();
    puts("mirror codec tests passed");
    return 0;
}
