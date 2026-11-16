/* main/usb_cdc_tx.c */
#include "usb_cdc_tx.h"
#include <string.h>

/* The terminator needs only one free byte, but retry once so a momentarily full
 * ring does not leave two frames on the same line. */
#define CDC_TX_NEWLINE_TRIES 2

bool cdc_tx_frame(const char *line, size_t chunk, cdc_tx_sink_t sink, void *ctx)
{
    if (!line || !sink || chunk == 0) return false;
    size_t len = strlen(line), sent = 0;
    while (sent < len) {
        size_t n = len - sent < chunk ? len - sent : chunk;
        if (sink(ctx, line + sent, n) != (int)n) {
            if (sent) (void)sink(ctx, "\n", 1);
            return false;
        }
        sent += n;
    }
    if (len && line[len - 1] == '\n') return true;
    for (unsigned i = 0; i < CDC_TX_NEWLINE_TRIES; ++i)
        if (sink(ctx, "\n", 1) == 1) return true;
    return false;
}
