#include "sdkconfig.h"
#include "game_runtime.h"
#include "robot_ui.h"
#include "usb_cdc.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
typedef enum { GAME_BUTTON, GAME_START, GAME_STOP, GAME_MENU } event_kind_t;
typedef struct { event_kind_t kind; uint64_t ms; bool pressed; } game_event_t;
static QueueHandle_t s_events;
static SemaphoreHandle_t s_remote_lock;
static basketball_game_t s_game;
static remote_game_t s_remote;
static app_input_t s_input;
static app_view_t s_app;
static basketball_view_t s_view;
static uint64_t s_activity;
static uint32_t s_button_sequence;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
esp_err_t game_runtime_init(void)
{
    if (s_events) return ESP_ERR_INVALID_STATE;
    s_remote_lock = xSemaphoreCreateMutex();
    if (!s_remote_lock) return ESP_ERR_NO_MEM;
    s_events = xQueueCreate(16, sizeof(game_event_t));
    return s_events ? ESP_OK : ESP_ERR_NO_MEM;
}
static bool post(event_kind_t kind, uint64_t ms, bool pressed)
{
    game_event_t e = {kind, ms, pressed};
    return s_events && xQueueSend(s_events, &e, 0) == pdTRUE;
}
void game_post_button(bool pressed, uint64_t now) { (void)post(GAME_BUTTON, now, pressed); }
void game_get_remote_view(remote_game_view_t *v)
{
    if (!s_remote_lock) { memset(v, 0, sizeof *v); return; }
    xSemaphoreTake(s_remote_lock, portMAX_DELAY);
    remote_game_snapshot(&s_remote, (uint64_t)esp_timer_get_time() / 1000, v);
    xSemaphoreGive(s_remote_lock);
}
void game_get_app_view(app_view_t *v) { portENTER_CRITICAL(&s_lock); *v = s_app; portEXIT_CRITICAL(&s_lock); }
void game_get_basketball_view(basketball_view_t *v) { portENTER_CRITICAL(&s_lock); *v = s_view; portEXIT_CRITICAL(&s_lock); }
static void stock_event(const char *action, uint64_t now)
{
    uint32_t id; unsigned cursor;
    xSemaphoreTake(s_remote_lock, portMAX_DELAY);
    id = s_remote.active ? s_remote.data.id : 0; cursor = s_remote.cursor;
    xSemaphoreGive(s_remote_lock);
    char message[210];
    snprintf(message, sizeof message,
        "{\"v\":1,\"type\":\"game_button\",\"action\":\"%s\",\"id\":%lu,\"pressed\":%s,\"ms\":%llu,\"cursor\":%u,\"seq\":%lu}",
        action, (unsigned long)id, !strcmp(action, "down") ? "true" : "false",
        (unsigned long long)now, cursor, (unsigned long)++s_button_sequence);
    cdc_write(message);
}
static void navigate(app_kind_t *app, app_kind_t target, uint64_t now)
{
    if (*app == APP_STOCK && target != APP_STOCK) stock_event("menu", now);
    if (target != APP_BASKETBALL) basketball_stop(&s_game);
    bool entering_stock = *app != APP_STOCK && target == APP_STOCK;
    *app = target; s_activity = now;
    portENTER_CRITICAL(&s_lock); s_app.app = target; portEXIT_CRITICAL(&s_lock);
    if (entering_stock) stock_event("launch", now);
    if (target == APP_IDLE) {
        char error[96]; const char *json = "{\"v\":1,\"mode\":\"auto\"}";
        (void)robot_ui_submit(json, strlen(json), error, sizeof error);
    }
}
static void dispatch(app_kind_t *app, unsigned actions, unsigned *selected, uint64_t now)
{
    if (!actions) return;
    s_activity = now;
    if (actions & INPUT_MENU) { navigate(app, APP_MENU, now); return; }
    if (*app == APP_MENU) {
        if (actions & INPUT_SHORT) *selected = (*selected + 1) % 3;
        if (actions & INPUT_CONFIRM) {
            app_kind_t target = *selected == 0 ? APP_BASKETBALL : *selected == 1 ? APP_STOCK : APP_RADAR;
            navigate(app, target, now);
            if (target == APP_BASKETBALL) basketball_start(&s_game, now, esp_random());
            app_input_consume(&s_input); /* Selection release must not shoot or sell. */
        }
    } else if (*app == APP_IDLE) {
        if (actions & INPUT_SHORT) navigate(app, APP_MENU, now);
    } else if (*app == APP_BASKETBALL) {
        if (actions & INPUT_SHORT) {
            if (s_game.phase == BASKETBALL_RESULT) navigate(app, APP_MENU, now);
            else (void)basketball_shoot(&s_game, now);
        }
    } else if (*app == APP_RADAR) {
        if (actions & INPUT_SHORT) navigate(app, APP_MENU, now);
    } else if (*app == APP_STOCK) {
        if (actions & INPUT_DOWN) stock_event("down", now);
        if (actions & INPUT_CONFIRM) stock_event("hold", now);
        if (actions & INPUT_UP) {
            stock_event("up", now);
            xSemaphoreTake(s_remote_lock, portMAX_DELAY);
            bool offline = !s_remote.active || now < s_remote.updated_ms || now - s_remote.updated_ms > REMOTE_GAME_WATCHDOG_MS;
            xSemaphoreGive(s_remote_lock);
            if (offline && (actions & INPUT_SHORT)) navigate(app, APP_MENU, now);
        }
    }
}
void game_runtime_tick(uint64_t now)
{
    app_kind_t app = s_app.app; unsigned selected = s_app.selected;
    game_event_t e;
    for (unsigned i = 0; s_events && i < 16 && xQueueReceive(s_events, &e, 0); ++i) {
        if (e.ms > now) now = e.ms;
        if (e.kind == GAME_START) {
            navigate(&app, APP_BASKETBALL, e.ms); basketball_start(&s_game, e.ms, esp_random());
            app_input_consume(&s_input);
        } else if (e.kind == GAME_STOP) { navigate(&app, APP_IDLE, e.ms); app_input_consume(&s_input); }
        else if (e.kind == GAME_MENU) { navigate(&app, APP_MENU, e.ms); app_input_consume(&s_input); }
        else dispatch(&app, app_input_edge(&s_input, e.pressed, e.ms), &selected, e.ms);
    }
    dispatch(&app, app_input_tick(&s_input, now), &selected, now);
    if (app == APP_MENU && now >= s_activity && now - s_activity >= APP_MENU_IDLE_MS) navigate(&app, APP_IDLE, now);
    basketball_tick(&s_game, now);
    basketball_view_t v; basketball_snapshot(&s_game, now, &v);
    app_view_t av = {.app = app, .selected = selected, .hold_ms = app_input_progress(&s_input, now)};
    portENTER_CRITICAL(&s_lock); s_app = av; s_view = v; portEXIT_CRITICAL(&s_lock);
}
bool game_command(const char *line)
{
    if (!strncmp(line, "game ", 5)) {
        if (!s_remote_lock) { cdc_write("ERR game not ready"); return true; }
        char error[96], reply[210]; int seq = 0;
        xSemaphoreTake(s_remote_lock, portMAX_DELAY);
        bool before = s_remote.active;
        bool ok = remote_game_apply(&s_remote, line + 5, strlen(line + 5),
                  (uint64_t)esp_timer_get_time() / 1000, &seq, error, sizeof error);
        bool closed = before && !s_remote.active;
        xSemaphoreGive(s_remote_lock);
        if (closed) (void)post(GAME_MENU, (uint64_t)esp_timer_get_time() / 1000, false);
        app_view_t app; game_get_app_view(&app);
        if (ok) snprintf(reply, sizeof reply, "{\"v\":1,\"type\":\"game_ack\",\"seq\":%d,\"ok\":true,\"selected\":%s}", seq, app.app == APP_STOCK ? "true" : "false");
        else snprintf(reply, sizeof reply, "{\"v\":1,\"type\":\"game_ack\",\"seq\":%d,\"ok\":false,\"error\":\"%s\"}", seq, error);
        cdc_write(reply); return true;
    }
    if (!strcmp(line, "robot game status")) {
        basketball_view_t v; game_get_basketball_view(&v);
        app_view_t av; game_get_app_view(&av);
        char reply[170]; snprintf(reply, sizeof reply, "app=%d selected=%u seconds=%u shots=%lu made=%lu score=%lu",
            av.app, av.selected, v.seconds_left, (unsigned long)v.shots, (unsigned long)v.made, (unsigned long)v.score);
        cdc_write(reply); return true;
    }
    event_kind_t kind;
    if (!strcmp(line, "robot game start")) kind = GAME_START;
    else if (!strcmp(line, "robot game stop")) kind = GAME_STOP;
    else if (!strcmp(line, "robot menu")) kind = GAME_MENU;
    else return false;
    cdc_write(post(kind, (uint64_t)esp_timer_get_time() / 1000, false) ? "OK game command queued" : "ERR game unavailable");
    return true;
}
#else
esp_err_t game_runtime_init(void) { return ESP_ERR_NOT_SUPPORTED; }
void game_post_button(bool p, uint64_t n) { (void)p; (void)n; }
void game_runtime_tick(uint64_t n) { (void)n; }
void game_get_basketball_view(basketball_view_t *v) { memset(v, 0, sizeof *v); }
void game_get_remote_view(remote_game_view_t *v) { memset(v, 0, sizeof *v); }
void game_get_app_view(app_view_t *v) { memset(v, 0, sizeof *v); }
bool game_command(const char *line) { (void)line; return false; }
#endif
