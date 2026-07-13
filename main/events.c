/* main/events.c */
#include "events.h"
#include <string.h>

static char s_last_callback[32] = "";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

QueueHandle_t display_q;
QueueHandle_t beeper_q;

const char *last_callback(void)
{
    static char tmp[32];
    portENTER_CRITICAL(&s_lock);
    memcpy(tmp, s_last_callback, sizeof tmp);
    portEXIT_CRITICAL(&s_lock);
    return tmp;
}

void set_last_callback(const char *cb)
{
    if (!cb) return;
    portENTER_CRITICAL(&s_lock);
    strncpy(s_last_callback, cb, sizeof s_last_callback - 1);
    s_last_callback[sizeof s_last_callback - 1] = '\0';
    portEXIT_CRITICAL(&s_lock);
}
