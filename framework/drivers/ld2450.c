#include "ld2450.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
/* LD2450 reports sign-magnitude, with bit 15 SET for positive values.
 * This is deliberately NOT a cast to a two's-complement int16_t. */
static int16_t coordinate(const uint8_t *p)
{
    uint16_t raw = le16(p);
    int16_t magnitude = (int16_t)(raw & 0x7fff);
    return (raw & 0x8000) ? magnitude : -magnitude;
}

bool ld2450_feed(ld2450_parser_t *p, uint8_t byte, ld2450_frame_t *out)
{
    static const uint8_t header[] = {0xaa, 0xff, 0x03, 0x00};
    p->bytes[p->used++] = byte;
    /* Sliding resynchronization preserves a partial header after noise or a
     * damaged frame. Payload may contain both header and footer bytes. */
    while (p->used && memcmp(p->bytes, header, p->used < 4 ? p->used : 4)) {
        memmove(p->bytes, p->bytes + 1, --p->used);
    }
    if (p->used < LD2450_FRAME_SIZE) return false;
    if (p->bytes[28] != 0x55 || p->bytes[29] != 0xcc) {
        p->rejected++;
        memmove(p->bytes, p->bytes + 1, --p->used);
        return false;
    }
    for (size_t i = 0; i < LD2450_TARGETS; ++i) {
        const uint8_t *v = p->bytes + 4 + i * 8;
        out->targets[i] = (ld2450_target_t){
            .x_mm = coordinate(v), .y_mm = coordinate(v + 2),
            .speed_cms = coordinate(v + 4), .resolution_mm = le16(v + 6),
        };
        /* A stationary target (speed=0) is still a target. All-zero slots
         * and coordinates at the radar origin must not steer the head. */
        out->targets[i].valid = out->targets[i].x_mm != 0 || out->targets[i].y_mm != 0;
    }
    p->used = 0;
    p->frames++;
    return true;
}
