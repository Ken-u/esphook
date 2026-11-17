#include "esphook/app_best.h"
#include "esphook/app_services.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A fake settings store: a few slots, a write counter and a switch that makes writes fail. */
static struct { char key[16]; unsigned char data[APP_STORAGE_MAX]; size_t size; } slots[16];
static unsigned writes, reads;
static bool fail_writes;
bool app_storage_load(const char *key, void *out, size_t size)
{
    ++reads;
    for (unsigned i = 0; i < 16; ++i)
        if (!strcmp(slots[i].key, key) && slots[i].size == size) { memcpy(out, slots[i].data, size); return true; }
    return false;
}
bool app_storage_save(const char *key, const void *data, size_t size)
{
    assert(size && size <= APP_STORAGE_MAX);
    assert(strlen(key) >= 1 && strlen(key) <= 15); /* what the real store enforces: 1..15 characters of [a-z0-9_] */
    for (const char *p = key; *p; ++p) assert((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_');
    if (fail_writes) return false;
    ++writes;
    for (unsigned i = 0; i < 16; ++i)
        if (!slots[i].key[0] || !strcmp(slots[i].key, key)) { strcpy(slots[i].key, key); memcpy(slots[i].data, data, size); slots[i].size = size; return true; }
    return false;
}

static app_descriptor_t desc(const char *id) { return (app_descriptor_t){.id = id, .title = id}; }
static app_context_t ctx_of(const app_descriptor_t *d) { return (app_context_t){.app = d}; }

static void basics(void)
{
    app_descriptor_t dino = desc("dino");
    const app_context_t c = ctx_of(&dino);
    assert(app_best_get(&c) == 0);
    assert(!app_best_record(&c, 0) && writes == 0);                     /* 0 is never a record */
    assert(app_best_record(&c, 120) && app_best_get(&c) == 120 && writes == 1);
    assert(!app_best_record(&c, 120) && !app_best_record(&c, 119) && !app_best_record(&c, 1) && writes == 1);
    assert(app_best_record(&c, 121) && app_best_get(&c) == 121 && writes == 2);
    assert(app_best_record(&c, 0xFFFFFFFFU) && !app_best_record(&c, 0xFFFFFFFFU));
    assert(!app_best_record(&c, 5000) && app_best_get(&c) == 0xFFFFFFFFU);
    assert(!app_best_get(NULL) && !app_best_record(NULL, 1));
    const app_context_t none = {0};
    assert(!app_best_get(&none) && !app_best_record(&none, 1));
}
static void cheap_calls(void)
{
    /* a game over screen may call record on every tick: it must not read or write flash after the first look */
    app_descriptor_t ski = desc("ski");
    const app_context_t c = ctx_of(&ski);
    (void)app_best_get(&c);
    const unsigned r0 = reads, w0 = writes;
    for (unsigned i = 0; i < 500; ++i) { (void)app_best_record(&c, 10); (void)app_best_get(&c); }
    assert(writes == w0 + 1 && reads == r0);  /* the one real record; nothing else */
    for (unsigned i = 0; i < 500; ++i) (void)app_best_record(&c, 10);
    assert(writes == w0 + 1 && reads == r0);
}
static void per_app_and_persistence(void)
{
    app_descriptor_t a = desc("miner"), b = desc("jump");
    const app_context_t ca = ctx_of(&a), cb = ctx_of(&b);
    assert(app_best_record(&ca, 300) && app_best_get(&ca) == 300);
    assert(app_best_get(&cb) == 0);                       /* another app does not see it */
    assert(app_best_record(&cb, 40) && app_best_get(&cb) == 40);
    /* swapping the apps reloads from the store: that is what a reboot or re-entering the game looks like */
    assert(app_best_get(&ca) == 300 && app_best_get(&cb) == 40);
    assert(!app_best_record(&ca, 299) && app_best_record(&ca, 301) && app_best_get(&cb) == 40 && app_best_get(&ca) == 301);
}
static void foreign_data_is_ignored(void)
{
    /* whatever else sits under the key, a wrong size or a wrong tag counts as no best at all */
    app_descriptor_t z = desc("zombie");
    const app_context_t c = ctx_of(&z);
    const unsigned char junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    assert(app_storage_save("b_zombie", junk, sizeof junk));
    app_descriptor_t other = desc("racing"); const app_context_t co = ctx_of(&other);
    (void)app_best_get(&co);                              /* drop the cache entry */
    assert(app_best_get(&c) == 0);
    assert(app_best_record(&c, 7) && app_best_get(&c) == 7);
    const unsigned char shorter[3] = {9, 9, 9};
    assert(app_storage_save("b_starship", shorter, sizeof shorter));
    app_descriptor_t s = desc("starship"); const app_context_t cs = ctx_of(&s);
    assert(app_best_get(&cs) == 0 && app_best_record(&cs, 3) && app_best_get(&cs) == 3);
}
static void failed_writes(void)
{
    app_descriptor_t r = desc("racing");
    const app_context_t c = ctx_of(&r);
    const unsigned w0 = writes;
    fail_writes = true;
    assert(app_best_record(&c, 900) && app_best_get(&c) == 900); /* still a record for this power-on */
    assert(!app_best_record(&c, 900) && !app_best_record(&c, 899) && writes == w0); /* and not tried again every frame */
    fail_writes = false;
    assert(app_best_record(&c, 901) && writes == w0 + 1);
}
static void keys(void)
{
    /* ids of any length get a valid key, and two long ids that share a prefix do not share a key */
    const char *ids[] = {"a", "basketball", "abcdefghijklm", "abcdefghijklmn", "abcdefgh_one_long_id", "abcdefgh_two_long_id",
                         "abcdefghijklmnopqrstuvw", "abcdefghijklmnopqrstuvx"};
    app_descriptor_t d[8];
    for (unsigned i = 0; i < 8; ++i) {
        d[i] = desc(ids[i]);
        const app_context_t c = ctx_of(&d[i]);
        assert(app_best_get(&c) == 0);                       /* a fresh id has no best, however it was shortened */
        assert(app_best_record(&c, 100 + i));
    }
    for (unsigned i = 0; i < 8; ++i) {
        const app_context_t c = ctx_of(&d[i]);
        assert(app_best_get(&c) == 100 + i);                 /* no id overwrote another's number */
    }
}
int main(void)
{
    basics();
    cheap_calls();
    per_app_and_persistence();
    foreign_data_is_ignored();
    failed_writes();
    keys();
    puts("app best score: per-app persistent record, cheap repeated calls, foreign data, failed writes and long ids passed");
    return 0;
}
