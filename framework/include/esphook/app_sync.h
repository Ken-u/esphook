#pragma once
/* Sharing data between the UI task and the USB task without touching FreeRTOS.
 *
 * An app lives on two tasks: the UI task runs init/on_event/on_tick/render and owns every LVGL object and all of
 * the session state, while `command()` (and a PC message or transport callback) runs on the USB task. They may
 * exchange nothing but plain copied data. Pick the smallest tool:
 *
 *  - app_shared_copy(dst, src)           one struct/scalar snapshot in either direction: the UI task publishes its
 *                                        status, a `command` reads it. The common case.
 *  - app_critical_enter()/exit()         several fields (or a read-modify-write such as `++frames`) that must change
 *                                        together. Interrupts are off in between, so copy a few bytes and leave: no
 *                                        blocking calls, no logging, no LVGL, no allocation, no nested sections.
 *  - app_lock_t                          a section that parses, allocates or takes long (the stock game protocol):
 *                                        a real mutex, never usable from an ISR.
 *
 * Status shared this way must be a plain struct: no pointers into the app's arena, which is released on exit. */
#include <stdbool.h>

void app_critical_enter(void);
void app_critical_exit(void);

/* Atomically with respect to every other app_critical/app_shared_copy section: dst = src. Both sides must be
 * plain assignable lvalues/expressions (no commas outside parentheses, so wrap a compound literal in a variable). */
#define app_shared_copy(dst, src)  \
    do {                           \
        app_critical_enter();      \
        (dst) = (src);             \
        app_critical_exit();       \
    } while (0)

/* A blocking mutex for longer sections. Create it once in `init`/at start-up; it is never freed. */
typedef struct app_lock app_lock_t;
app_lock_t *app_lock_create(void);
void app_lock_take(app_lock_t *lock);
void app_lock_give(app_lock_t *lock);
