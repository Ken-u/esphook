#include "app_input.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    app_input_t i = {0};
    assert(app_input_edge(&i, true, 0) == INPUT_DOWN);
    assert(app_input_progress(&i, 499) == 499 && !app_input_tick(&i, 499));
    assert(app_input_tick(&i, 500) == INPUT_CONFIRM);
    assert(!app_input_tick(&i, 600));
    assert(app_input_edge(&i, false, 650) == INPUT_UP);
    assert(app_input_edge(&i, true, 700) == INPUT_DOWN);
    assert(app_input_edge(&i, false, 750) == (INPUT_UP | INPUT_SHORT));
    assert(app_input_edge(&i, true, 800) == INPUT_DOWN); /* Long hold is not click one of a triple. */
    assert(app_input_edge(&i, false, 850) & INPUT_SHORT);
    assert(app_input_edge(&i, true, 900) == INPUT_MENU);
    assert(!app_input_tick(&i, 1500));
    assert(!app_input_edge(&i, false, 1600)); /* Third click's release is consumed. */
    i = (app_input_t){0};
    assert(app_input_edge(&i, true, 2000) == INPUT_DOWN);
    assert(app_input_edge(&i, false, 2050) & INPUT_SHORT);
    assert(app_input_edge(&i, true, 2400) == INPUT_DOWN);
    assert(app_input_edge(&i, false, 2450) & INPUT_SHORT);
    assert(app_input_edge(&i, true, 2800) == INPUT_MENU); /* 800ms inclusive. */
    app_input_consume(&i); assert(!app_input_edge(&i, false, 2850));
    assert(app_input_edge(&i, true, 4000) == INPUT_DOWN);
    assert(app_input_edge(&i, false, 4500) == (INPUT_UP | INPUT_CONFIRM));
    i = (app_input_t){0};
    assert(!app_input_edge(&i, false, 10));
    assert(app_input_edge(&i, true, 100) == INPUT_DOWN);
    assert(!app_input_edge(&i, true, 200));
    assert(app_input_tick(&i, 600) == INPUT_CONFIRM);
    app_input_consume(&i); assert(!app_input_edge(&i, false, 700));
    puts("app input: global triple, 500ms confirmation, short release and consumed launch passed");
}
