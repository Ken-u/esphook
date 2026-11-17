#include "app_best.h"
#include "app_services.h"
#include <stdio.h>
#include <string.h>

/* The stored record: a tag, so that whatever else ends up under the key is never mistaken for a score, and the score. */
#define BEST_TAG 0x54534542U /* "BEST" */
#define KEY_MAX 15U
#define ID_FULL 13U          /* "b_" plus this many characters of the id fit a key; longer ids are shortened + hashed */
typedef struct { uint32_t tag, best; } record_t;

/* The one app that is on screen is the only one that asks, so a single cached entry is enough. */
static struct { char key[KEY_MAX + 1U]; uint32_t best; bool valid; } s_cache;

static void make_key(const char *id, char out[KEY_MAX + 1U])
{
    const size_t n = strlen(id);
    if (n <= ID_FULL) { (void)snprintf(out, KEY_MAX + 1U, "b_%s", id); return; }
    uint32_t h = 2166136261U; /* FNV-1a over the whole id keeps two long ids with one prefix apart */
    for (size_t i = 0; i < n; ++i) { h ^= (unsigned char)id[i]; h *= 16777619U; }
    (void)snprintf(out, KEY_MAX + 1U, "b_%.8s%04x", id, (unsigned)(h & 0xFFFFU));
}
/* Makes the cache hold this app's best; the stored value is read only when the app changed. */
static void sync_cache(const app_context_t *ctx)
{
    char key[KEY_MAX + 1U];
    make_key(ctx->app->id, key);
    if (s_cache.valid && !strcmp(s_cache.key, key)) return;
    record_t r;
    memcpy(s_cache.key, key, sizeof key);
    s_cache.best = app_storage_load(key, &r, sizeof r) && r.tag == BEST_TAG ? r.best : 0U;
    s_cache.valid = true;
}
uint32_t app_best_get(const app_context_t *ctx)
{
    if (!ctx || !ctx->app) return 0U;
    sync_cache(ctx);
    return s_cache.best;
}
bool app_best_record(const app_context_t *ctx, uint32_t score)
{
    if (!ctx || !ctx->app) return false;
    sync_cache(ctx);
    if (score <= s_cache.best) return false;
    s_cache.best = score; /* kept for this power-on even when the write below fails: no retry on every frame */
    const record_t r = {.tag = BEST_TAG, .best = score};
    (void)app_storage_save(s_cache.key, &r, sizeof r);
    return true;
}
