/* main/display_task.c
 * 消费 display_q：统一改 display_model（避免多任务竞态），重渲染。
 * model 只在此任务改。producer（http/input）只发事件。
 * 周期 ~10fps：推进滚动 + 刷新。
 */
#include "display_task.h"
#include "events.h"
#include "display_model.h"
#include "display_view.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "disp";

static int s_net_state = 0;        /* 0=disc 1=connecting 2=connected */
static char s_ip[16] = "";
static int s_scroll_tick = 0;

static void render_all(void)
{
    if (s_net_state != 2) {
        dv_render_net(s_net_state, s_ip);
    } else {
        dv_render_status_bar();
        dv_render_main();
    }
    dv_flush();
}

static void display_task_fn(void *arg)
{
    display_event_t e;
    while (1) {
        if (xQueueReceive(display_q, &e, pdMS_TO_TICKS(100))) {
            switch (e.kind) {
                case EVT_NOTIFY:
                    dm_notify(e.client_id, e.session_id, e.title, e.body);
                    if (e.callback[0]) set_last_callback(e.callback);
                    break;
                case EVT_DISMISS:
                    dm_dismiss(e.client_id, e.session_id);
                    break;
                case EVT_NET_STATE:
                    s_net_state = e.net_state;
                    strncpy(s_ip, e.ip, sizeof s_ip - 1);
                    s_ip[sizeof s_ip - 1] = '\0';
                    break;
            }
            render_all();
        } else {
            /* 超时：周期刷新 + 滚动推进（每 100ms，约每 5 帧推一次滚动） */
            if (++s_scroll_tick >= 5) {
                s_scroll_tick = 0;
                dv_tick_scroll();
            }
            render_all();
        }
    }
}

void display_task_start(void)
{
    xTaskCreate(display_task_fn, "display", 4096, NULL, 4, NULL);
}
