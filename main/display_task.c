/* main/display_task.c
 * 消费 display_q：统一改 display_model（避免多任务竞态），重渲染。
 * model 只在此任务改。producer（http/input）只发事件。
 * LVGL 在此任务中独占运行；标签滚动由 LVGL 动画以细粒度 tick 驱动。
 */
#include "display_task.h"
#include "sdkconfig.h"
#include "robot_runtime.h"
#include "robot_ui.h"
#include "game_runtime.h"
#include "esp_timer.h"
#include "events.h"
#include "display_model.h"
#include "display_view.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static int s_net_state = 0;        /* 0=disc 1=connecting 2=connected */
static char s_ip[16] = "";
static void render_all(void)
{
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    app_view_t app;
    game_get_app_view(&app);
    if (app.app == APP_MENU) { dv_render_launcher(&app); return; }
    if (app.app == APP_STOCK) {
        static remote_game_view_t remote;
        game_get_remote_view(&remote); dv_render_stock(&remote); return;
    }
    if (app.app == APP_BASKETBALL) {
        basketball_view_t game; game_get_basketball_view(&game);
        dv_render_basketball(&game); return;
    }
    robot_status_t robot; robot_get_status(&robot);
    robot_ui_t view; robot_ui_get(&view);
    if (app.app == APP_RADAR) view.mode = UI_RADAR;
    dv_render_robot(&robot, &view);
#else
    /* state=2 也传给 view，让待机页能显示最新 IP；连接页本身不再覆盖提醒。 */
    dv_render_net(s_net_state, s_ip);
    if (s_net_state == 2) {
        dv_render_status_bar();
        dv_render_main();
    }
#endif
}

static void display_task_fn(void *arg)
{
    display_event_t e;
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    TickType_t face_tick = 0;
    app_kind_t last_app = APP_IDLE;
    game_runtime_tick((uint64_t)esp_timer_get_time() / 1000);
#endif
    render_all();
    while (1) {
        if (xQueueReceive(display_q, &e, pdMS_TO_TICKS(5))) {
            /* 任意显示事件都算活动；新提醒/网络状态变化会唤醒屏幕。 */
            dv_activity();
            switch (e.kind) {
                case EVT_NOTIFY:
                    dm_notify_status(e.client_id, e.session_id, e.title, e.body, e.status);
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
        }
        /* LVGL timer 同时负责重绘、标签滚动和动画，目标调用频率约 200Hz。 */
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
        game_runtime_tick((uint64_t)esp_timer_get_time() / 1000);
        app_view_t app;
        game_get_app_view(&app);
        TickType_t interval = pdMS_TO_TICKS(app.app == APP_MENU ? 16 : app.app == APP_BASKETBALL ? 33 : 80);
        if (app.app != last_app || xTaskGetTickCount() - face_tick >= interval) {
            last_app = app.app;
            face_tick = xTaskGetTickCount();
            render_all();
        }
#endif
        dv_process();
    }
}

void display_task_start(void)
{
    xTaskCreate(display_task_fn, "display", 4096, NULL, 4, NULL);
}
