/* Host regression for the USB TX framing (see test-report-usb-ack-and-gestures.md).
 *
 * The simulated driver follows usb_serial_jtag_write_bytes() in ESP-IDF v5.5.4:
 * the bytes go into a fixed-size ring, and a write that does not fit in the
 * free space returns 0 instead of partially queueing. `drain` models the host
 * reading that many bytes out of the ring before every write attempt. */
#undef NDEBUG
#include "usb_cdc_tx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    size_t capacity, used, drain;
    unsigned writes, fail_at; /* 1-based index of the one write that fails; 0 = never */
    char host[8192];            /* bytes the host has read so far */
    size_t host_len;
} sim_t;

static void sim_init(sim_t *s, size_t capacity, size_t drain)
{
    memset(s, 0, sizeof *s);
    s->capacity = capacity;
    s->drain = drain;
}

static void sim_host_read(sim_t *s, size_t n)
{
    n = n < s->used ? n : s->used;
    s->used -= n;
}

static int sim_write(void *ctx, const char *data, size_t len)
{
    sim_t *s = ctx;
    ++s->writes;
    sim_host_read(s, s->drain);
    if (s->writes == s->fail_at || len > s->capacity - s->used) return 0;
    assert(s->host_len + len < sizeof s->host);
    memcpy(s->host + s->host_len, data, len);
    s->host_len += len;
    s->used += len;
    return (int)len;
}

/* The pre-fix cdc_write(): one write for the body, return value ignored, then
 * a separate newline write that always fits. */
static void legacy_write(sim_t *s, const char *str)
{
    size_t len = strlen(str);
    (void)sim_write(s, str, len);
    if (len == 0 || str[len - 1] != '\n') (void)sim_write(s, "\n", 1);
}

static void make_frame(char *out, size_t size, size_t length)
{
    assert(length + 1 < size && length >= 2);
    out[0] = '{';
    memset(out + 1, 'a', length - 2);
    out[length - 1] = '}';
    out[length] = '\0';
}

static void expect_host(const sim_t *s, const char *frame)
{
    size_t n = strlen(frame);
    assert(s->host_len == n + 1);
    assert(!memcmp(s->host, frame, n) && s->host[n] == '\n');
}

int main(void)
{
    char frame[4096];
    sim_t sim;

    /* 1. Root cause: with IDF's default 256-byte ring the ~482-byte `list` ack
     *    never fits, and the old code left only the stray newline on the wire. */
    make_frame(frame, sizeof frame, 482);
    sim_init(&sim, 256, 0);
    legacy_write(&sim, frame);
    assert(sim.host_len == 1 && sim.host[0] == '\n');

    /* 2. Same frame through the fixed path with the enlarged ring arrives intact. */
    sim_init(&sim, 2048, 0);
    assert(cdc_tx_frame(frame, 256, sim_write, &sim));
    expect_host(&sim, frame);

    /* 3. The failure is reported and leaves no orphan newline when the ring is too small. */
    sim_init(&sim, 128, 0);
    assert(!cdc_tx_frame(frame, 482, sim_write, &sim));
    assert(sim.host_len == 0);

    /* 4. Chunking lets frames larger than the whole ring through while the host drains. */
    make_frame(frame, sizeof frame, 3000);
    sim_init(&sim, 256, 256);
    assert(cdc_tx_frame(frame, 128, sim_write, &sim));
    expect_host(&sim, frame);

    /* 5. A host that stops reading no longer turns into silent loss: the first
     *    229-byte `hardware` ack fits a 256-byte ring, the second finds the
     *    leftovers in the way. 2048 bytes holds both. */
    make_frame(frame, sizeof frame, 229);
    sim_init(&sim, 256, 0);
    assert(cdc_tx_frame(frame, 256, sim_write, &sim));
    assert(!cdc_tx_frame(frame, 256, sim_write, &sim));
    sim_init(&sim, 2048, 0);
    assert(cdc_tx_frame(frame, 256, sim_write, &sim));
    assert(cdc_tx_frame(frame, 256, sim_write, &sim));
    assert(sim.host_len == 2 * 230);

    /* 6. A frame cut short is terminated so it cannot glue onto the next frame:
     *    chunk 1 and 2 (512 bytes) go out, chunk 3 is refused, then a newline. */
    make_frame(frame, sizeof frame, 600);
    sim_init(&sim, 4096, 0);
    sim.fail_at = 3;
    assert(!cdc_tx_frame(frame, 256, sim_write, &sim));
    assert(sim.host_len == 513 && !memcmp(sim.host, frame, 512) && sim.host[512] == '\n');
    assert(cdc_tx_frame("{\"ok\":true}", 256, sim_write, &sim));
    assert(!strcmp(sim.host + 513, "{\"ok\":true}\n"));

    /* 7. Terminator rules: no duplicate newline, empty string is a bare newline,
     *    invalid arguments are rejected. */
    sim_init(&sim, 256, 0);
    assert(cdc_tx_frame("ERR bad\n", 256, sim_write, &sim));
    assert(!strcmp(sim.host, "ERR bad\n"));
    sim_init(&sim, 256, 0);
    assert(cdc_tx_frame("", 256, sim_write, &sim));
    assert(!strcmp(sim.host, "\n"));
    assert(!cdc_tx_frame(NULL, 256, sim_write, &sim));
    assert(!cdc_tx_frame("x", 0, sim_write, &sim));
    assert(!cdc_tx_frame("x", 256, NULL, &sim));

    /* 8. A transient refusal of the terminator is retried once; two refusals are an error. */
    sim_init(&sim, 4096, 0);
    sim.fail_at = 2;
    assert(cdc_tx_frame("abc", 256, sim_write, &sim));
    assert(!strcmp(sim.host, "abc\n"));
    sim_init(&sim, 3, 0);
    assert(!cdc_tx_frame("abc", 256, sim_write, &sim));

    puts("usb cdc tx: ring-size loss model, chunking, truncated-frame terminator and error reporting passed");
    return 0;
}
