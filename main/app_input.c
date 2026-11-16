#include "app_input.h"
unsigned app_input_edge(app_input_t *i, bool pressed, uint64_t now)
{
    if (pressed) {
        if (i->down) return 0;
        i->down = true; i->down_ms = now; i->confirmed = false; i->blocked = false;
        if (!i->clicks || now < i->first_ms || now - i->first_ms > APP_TRIPLE_MS) {
            i->first_ms = now; i->clicks = 1;
        } else if (++i->clicks == 3) {
            i->clicks = 0; i->blocked = true;
            return INPUT_MENU;
        }
        return INPUT_DOWN;
    }
    if (!i->down) return 0;
    i->down = false;
    if (i->blocked) { i->blocked = false; return 0; }
    unsigned action = INPUT_UP;
    if (!i->confirmed && now >= i->down_ms && now - i->down_ms < APP_CONFIRM_MS) action |= INPUT_SHORT;
    /* A release after a delayed scheduler wake still confirms exactly once. */
    else if (!i->confirmed && now >= i->down_ms) { i->confirmed = true; i->clicks = 0; action |= INPUT_CONFIRM; }
    return action;
}
unsigned app_input_tick(app_input_t *i, uint64_t now)
{
    if (i->down && !i->confirmed && !i->blocked && now >= i->down_ms && now - i->down_ms >= APP_CONFIRM_MS) {
        i->confirmed = true; i->clicks = 0; return INPUT_CONFIRM;
    }
    return 0;
}
unsigned app_input_progress(const app_input_t *i, uint64_t now)
{
    if (!i->down || i->blocked || now < i->down_ms) return 0;
    uint64_t elapsed = now - i->down_ms;
    return elapsed >= APP_CONFIRM_MS ? APP_CONFIRM_MS : (unsigned)elapsed;
}
void app_input_consume(app_input_t *i) { i->blocked = i->down; i->confirmed = true; i->clicks = 0; }

bool app_button_sample(app_button_t *b, bool pressed, uint64_t now)
{
    if (!b->initialized) {
        *b = (app_button_t){.initialized = true, .raw_pressed = pressed,
            .stable_pressed = pressed, .may_press = !pressed, .changed_ms = now};
        return false;
    }
    if (pressed != b->raw_pressed) { b->raw_pressed = pressed; b->changed_ms = now; }
    if (pressed == b->stable_pressed || now < b->changed_ms || now - b->changed_ms < APP_DEBOUNCE_MS) return false;
    b->stable_pressed = pressed;
    if (!pressed) b->may_press = true;
    else if (b->may_press) { b->may_press = false; return true; }
    return false;
}

void app_button_edge(app_button_t *b, bool pressed, uint64_t now)
{
    if (!b->initialized) { (void)app_button_sample(b, pressed, now); return; }
    /* Several interrupt edges may collapse into one task notification. Restart
     * the quiet window even when the final pin level equals the last sample. */
    b->raw_pressed = pressed;
    b->changed_ms = now;
}
