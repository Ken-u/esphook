#include "sdkconfig.h"
#include "basketball_runtime.h"
#include "usb_cdc.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
typedef enum { GAME_PRESS, GAME_START, GAME_STOP } event_kind_t;
typedef struct { event_kind_t kind; uint64_t ms; } game_event_t;
static QueueHandle_t s_events;
static basketball_game_t s_game;
static basketball_view_t s_view;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t basketball_runtime_init(void)
{
    if (s_events) return ESP_ERR_INVALID_STATE;
    s_events = xQueueCreate(16, sizeof(game_event_t));
    return s_events ? ESP_OK : ESP_ERR_NO_MEM;
}
static bool post(event_kind_t kind, uint64_t now)
{
    game_event_t event = {.kind = kind, .ms = now};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE;
}
void basketball_post_press(uint64_t now) { (void)post(GAME_PRESS, now); }
void basketball_runtime_tick(uint64_t now)
{
    game_event_t event;
    for (unsigned i = 0; s_events && i < 16 && xQueueReceive(s_events, &event, 0); ++i) {
        if (event.ms > now) now = event.ms;
        if (event.kind == GAME_START) basketball_start(&s_game, event.ms, esp_random());
        else if (event.kind == GAME_STOP) basketball_stop(&s_game);
        else basketball_press(&s_game, event.ms, esp_random());
    }
    basketball_tick(&s_game, now);
    basketball_view_t view;
    basketball_snapshot(&s_game, now, &view);
    portENTER_CRITICAL(&s_lock);
    s_view = view;
    portEXIT_CRITICAL(&s_lock);
}
void basketball_get_view(basketball_view_t *view)
{
    portENTER_CRITICAL(&s_lock);
    *view = s_view;
    portEXIT_CRITICAL(&s_lock);
}
bool basketball_command(const char *line)
{
    if (!strcmp(line, "robot game status")) {
        basketball_view_t v;
        basketball_get_view(&v);
        const char *phase = v.phase == BASKETBALL_PLAY ? "playing" : v.phase == BASKETBALL_RESULT ? "result" : "off";
        char reply[180];
        snprintf(reply, sizeof reply, "game state=%s seconds=%u shots=%lu made=%lu score=%lu speed=%u",
                 phase, v.seconds_left, (unsigned long)v.shots, (unsigned long)v.made,
                 (unsigned long)v.score, v.speed_pps);
        cdc_write(reply);
        return true;
    }
    event_kind_t kind;
    if (!strcmp(line, "robot game start")) kind = GAME_START;
    else if (!strcmp(line, "robot game stop")) kind = GAME_STOP;
    else return false;
    cdc_write(post(kind, (uint64_t)esp_timer_get_time() / 1000)
              ? "OK game command queued" : "ERR game unavailable or queue full");
    return true;
}
#else
esp_err_t basketball_runtime_init(void) { return ESP_ERR_NOT_SUPPORTED; }
void basketball_post_press(uint64_t now) { (void)now; }
void basketball_runtime_tick(uint64_t now) { (void)now; }
void basketball_get_view(basketball_view_t *view) { memset(view, 0, sizeof *view); }
bool basketball_command(const char *line) { (void)line; return false; }
#endif
