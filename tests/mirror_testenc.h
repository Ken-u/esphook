#pragma once
/* The PC side of the strip protocol in C, only for the tests: palette + nibbles -> `mirror s ...` line. */
#include "mirror_codec.h"
#include <stdio.h>
#include <string.h>

static inline void mirror_test_b64(const uint8_t *in, size_t n, char *out)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t w = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        *out++ = alphabet[w >> 18]; *out++ = alphabet[w >> 12 & 63U]; *out++ = alphabet[w >> 6 & 63U]; *out++ = alphabet[w & 63U];
    }
    *out = 0;
}
/* A strip of two colours: `top` above row `split` and `bottom` from it down (split 0..8), as RGB565. */
static inline void mirror_test_payload(uint8_t payload[MIRROR_PAYLOAD], uint16_t top, uint16_t bottom, unsigned split)
{
    memset(payload, 0, MIRROR_PAYLOAD);
    payload[0] = (uint8_t)top; payload[1] = (uint8_t)(top >> 8);
    payload[2] = (uint8_t)bottom; payload[3] = (uint8_t)(bottom >> 8);
    for (unsigned row = 0; row < MIRROR_ROWS; ++row)
        memset(payload + MIRROR_COLOURS * 2U + row * MIRROR_W / 2U, row < split ? 0x00 : 0x11, MIRROR_W / 2U);
}
static inline void mirror_test_line(char *line, size_t cap, unsigned frame, unsigned index, const uint8_t payload[MIRROR_PAYLOAD])
{
    char b64[MIRROR_B64 + 1U];
    mirror_test_b64(payload, MIRROR_PAYLOAD, b64);
    const int n = snprintf(line, cap, "mirror s %u %u %04x %s", frame, index, (unsigned)mirror_crc16(payload, MIRROR_PAYLOAD), b64);
    if (n < 0 || (size_t)n >= cap) line[0] = 0;
}
