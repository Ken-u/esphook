/* main/usb_cdc_tx.h
 * Hardware independent line framing for the USB Serial/JTAG TX path.
 * Kept free of ESP-IDF headers so host tests can drive it with a simulated driver.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Queue len bytes; must return len on success and 0 (or any other value) when
 * the driver could not take all of them (ring full, timeout, ...). */
typedef int (*cdc_tx_sink_t)(void *ctx, const char *data, size_t len);

/* Send one text line in chunks of at most `chunk` bytes, terminated by '\n'.
 * Returns true only if every byte, including the terminator, was accepted.
 *
 * Failure rules (these are what keep a lost frame from corrupting the stream):
 *  - nothing was accepted: nothing else is sent, so no orphan '\n' appears;
 *  - the frame was cut short: a '\n' is appended so the truncated line cannot
 *    be glued to the next frame. The host drops it as invalid JSON. */
bool cdc_tx_frame(const char *line, size_t chunk, cdc_tx_sink_t sink, void *ctx);
