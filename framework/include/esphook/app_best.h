#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app.h"

/* The best score of a game, kept after the game is left and after a reboot.
 *
 * One number per app, filed under the app's own id: a game only ever reads and writes its own, and no key has to be
 * invented (the numbers live in app_storage_* under keys that start with "b_", which an app's own storage keys must
 * not use). Everything belongs to the UI task, like the rest of the app lifecycle.
 *
 *     on_enter:       core.best = app_best_get(ctx);            // 0 until the first record
 *     tick/on_event:  if (core.phase == OVER) app_best_record(ctx, core.best);
 *
 * app_best_record() is cheap enough to call on every tick of a game over screen: it compares with a RAM copy and
 * touches flash only when the score is higher than the saved best, so a run costs at most one slow write and a
 * restart, a dull run or a repeated call costs none. A failed write (storage full or broken) still keeps the new
 * best for the rest of the power-on, and is not retried every frame. Record when a run is over, not while it is
 * under way: a score that climbs by every kill would write flash for every kill. */

/* The saved best score of the running app, 0 when it has none. */
uint32_t app_best_get(const app_context_t *ctx);
/* Saves `score` when it is higher than the saved best; true when it was (a new record), false otherwise. 0 is never
 * a record. */
bool app_best_record(const app_context_t *ctx, uint32_t score);
