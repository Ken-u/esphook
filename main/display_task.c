#include "display_task.h"
#include "esphook/app_runtime.h"
#include "display_view.h"
#include "events.h"
#include "esp_timer.h"
#include "freertos/task.h"
static void display_task_fn(void *arg)
{
    (void)arg; display_event_t event;
    for (;;) {
        if (xQueueReceive(display_q, &event, pdMS_TO_TICKS(5))) {
            app_runtime_display_event(&event, (uint64_t)esp_timer_get_time()/1000);
        }
        app_runtime_tick((uint64_t)esp_timer_get_time()/1000);
        dv_process();
    }
}
void display_task_start(void) { xTaskCreate(display_task_fn,"display",6144,NULL,4,NULL); }
