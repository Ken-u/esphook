#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 副屏: the PC sends a picture of one of its windows as strips of the 160 x 64 canvas. A strip is MIRROR_ROWS rows
 * wide as the screen, with its own palette of 16 RGB565 colours and one nibble per pixel, so a window of text and
 * flat colours costs about 670 bytes a strip and a strip that did not change is not sent at all. On the wire a strip is
 * one text line (the USB link carries lines, at most 1024 bytes):
 *     mirror s <frame 0..255> <strip 0..7> <crc16 as 4 hex digits> <base64 of the 672 payload bytes>
 * where the payload is the palette (16 little-endian words) followed by the indices, two pixels a byte, the left
 * pixel in the high nibble, row by row. The crc is CRC-16/CCITT-FALSE of the payload. Pure C: the USB task parses
 * a line into a slot of the ring and the UI task paints it, so the two never share anything but the ring. */
#define MIRROR_W 160
#define MIRROR_H 64
#define MIRROR_ROWS 8
#define MIRROR_STRIPS (MIRROR_H / MIRROR_ROWS)
#define MIRROR_COLOURS 16
#define MIRROR_PAYLOAD (MIRROR_COLOURS * 2 + MIRROR_W * MIRROR_ROWS / 2)
#define MIRROR_B64 (MIRROR_PAYLOAD / 3 * 4)
#define MIRROR_RING 8

typedef struct { uint8_t frame, index; uint8_t data[MIRROR_PAYLOAD]; } mirror_strip_t;
typedef enum { MIRROR_OK, MIRROR_ERR_ARGS, MIRROR_ERR_B64, MIRROR_ERR_CRC } mirror_status_t;

uint16_t mirror_crc16(const uint8_t *data, size_t len);
/* Decodes standard base64 without padding into `out`; returns the byte count, or -1 on a bad character or length. */
int mirror_b64_decode(const char *in, size_t len, uint8_t *out, size_t cap);
/* Parses one `mirror s ...` line (the text of the line, without the newline) into `out`. out->frame and out->index
 * are set whenever the status is not MIRROR_ERR_ARGS, even if the payload turns out to be bad. */
mirror_status_t mirror_parse_strip(const char *line, mirror_strip_t *out);
/* Paints the strip into rows index * MIRROR_ROWS.. of a MIRROR_W wide RGB565 canvas. */
void mirror_expand(const mirror_strip_t *strip, uint16_t *canvas);
const char *mirror_status_name(mirror_status_t status);

/* One producer (the USB task) and one consumer (the UI task). The caller holds a lock around each call, which also
 * orders the writes into a slot before the index that publishes it. */
typedef struct { mirror_strip_t slot[MIRROR_RING]; uint8_t head, tail; } mirror_ring_t;
void mirror_ring_reset(mirror_ring_t *ring);
unsigned mirror_ring_free(const mirror_ring_t *ring);
/* The slot to fill next, or NULL when the ring is full; mirror_ring_commit() publishes it. */
mirror_strip_t *mirror_ring_reserve(mirror_ring_t *ring);
void mirror_ring_commit(mirror_ring_t *ring);
/* The oldest strip, or NULL when there is none; mirror_ring_release() frees it. */
const mirror_strip_t *mirror_ring_peek(const mirror_ring_t *ring);
void mirror_ring_release(mirror_ring_t *ring);
