#include "mirror_codec.h"
#include <string.h>

uint16_t mirror_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
        for (unsigned bit = 0; bit < 8U; ++bit) crc = (uint16_t)((crc & 0x8000U) ? ((unsigned)crc << 1) ^ 0x1021U : (unsigned)crc << 1);
    }
    return crc;
}
static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
int mirror_b64_decode(const char *in, size_t len, uint8_t *out, size_t cap)
{
    if (len % 4U || len / 4U * 3U > cap) return -1;
    size_t n = 0;
    for (size_t i = 0; i < len; i += 4U) {
        int v[4];
        for (unsigned k = 0; k < 4U; ++k) if ((v[k] = b64_value(in[i + k])) < 0) return -1;
        const uint32_t word = (uint32_t)v[0] << 18 | (uint32_t)v[1] << 12 | (uint32_t)v[2] << 6 | (uint32_t)v[3];
        out[n++] = (uint8_t)(word >> 16); out[n++] = (uint8_t)(word >> 8); out[n++] = (uint8_t)word;
    }
    return (int)n;
}
static bool number(const char **p, unsigned max, unsigned *value)
{
    const char *s = *p;
    unsigned v = 0, digits = 0;
    while (*s >= '0' && *s <= '9' && digits < 4U) { v = v * 10U + (unsigned)(*s - '0'); ++s; ++digits; }
    if (!digits || v > max || *s != ' ') return false;
    *value = v;
    *p = s + 1;
    return true;
}
mirror_status_t mirror_parse_strip(const char *line, mirror_strip_t *out)
{
    static const char head[] = "mirror s ";
    if (strncmp(line, head, sizeof head - 1U)) return MIRROR_ERR_ARGS;
    const char *p = line + sizeof head - 1U;
    unsigned frame, index, crc = 0;
    if (!number(&p, 255U, &frame) || !number(&p, MIRROR_STRIPS - 1U, &index)) return MIRROR_ERR_ARGS;
    out->frame = (uint8_t)frame; /* known from here on, so that a bad payload can still be reported against its strip */
    out->index = (uint8_t)index;
    for (unsigned i = 0; i < 4U; ++i, ++p) {
        const char c = *p;
        const unsigned d = c >= '0' && c <= '9' ? (unsigned)(c - '0') : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a') + 10U : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A') + 10U : 16U;
        if (d > 15U) return MIRROR_ERR_ARGS;
        crc = crc << 4 | d;
    }
    if (*p++ != ' ' || strlen(p) != MIRROR_B64) return MIRROR_ERR_ARGS;
    if (mirror_b64_decode(p, MIRROR_B64, out->data, sizeof out->data) != (int)MIRROR_PAYLOAD) return MIRROR_ERR_B64;
    if (mirror_crc16(out->data, MIRROR_PAYLOAD) != crc) return MIRROR_ERR_CRC;
    return MIRROR_OK;
}
void mirror_expand(const mirror_strip_t *strip, uint16_t *canvas)
{
    uint16_t palette[MIRROR_COLOURS];
    for (unsigned i = 0; i < MIRROR_COLOURS; ++i) palette[i] = (uint16_t)(strip->data[i * 2U] | strip->data[i * 2U + 1U] << 8);
    const uint8_t *nibbles = strip->data + MIRROR_COLOURS * 2U;
    uint16_t *row = canvas + (size_t)strip->index * MIRROR_ROWS * MIRROR_W;
    for (unsigned i = 0; i < MIRROR_W * MIRROR_ROWS / 2U; ++i) {
        row[i * 2U] = palette[nibbles[i] >> 4];
        row[i * 2U + 1U] = palette[nibbles[i] & 15U];
    }
}
const char *mirror_status_name(mirror_status_t status)
{
    switch (status) {
    case MIRROR_OK: return "ok";
    case MIRROR_ERR_B64: return "b64";
    case MIRROR_ERR_CRC: return "crc";
    default: return "args";
    }
}
void mirror_ring_reset(mirror_ring_t *ring) { ring->head = ring->tail = 0; }
unsigned mirror_ring_free(const mirror_ring_t *ring) { return MIRROR_RING - (unsigned)(uint8_t)(ring->head - ring->tail); }
mirror_strip_t *mirror_ring_reserve(mirror_ring_t *ring) { return mirror_ring_free(ring) ? &ring->slot[ring->head % MIRROR_RING] : NULL; }
void mirror_ring_commit(mirror_ring_t *ring) { ++ring->head; }
const mirror_strip_t *mirror_ring_peek(const mirror_ring_t *ring) { return ring->head == ring->tail ? NULL : &ring->slot[ring->tail % MIRROR_RING]; }
void mirror_ring_release(mirror_ring_t *ring) { if (ring->head != ring->tail) ++ring->tail; }
