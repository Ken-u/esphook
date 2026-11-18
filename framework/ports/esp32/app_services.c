#include "app_services.h"
#include "app_sync.h"
#include "app_screen.h"
#include "robot_runtime.h"
#include "robot_ui.h"
#include "app_runtime.h"
#include "config_store.h"
#include "device_link.h"
#include "display_view.h"
#include "display_model.h"
#include "events.h"
#include "http_post.h"
#include "usb_cdc.h"
#include "net_wifi.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/* app_sync.h: one short critical section shared by all apps (they only copy a few bytes) and a thin mutex. */
static portMUX_TYPE s_app_critical = portMUX_INITIALIZER_UNLOCKED;
void app_critical_enter(void) { portENTER_CRITICAL(&s_app_critical); }
void app_critical_exit(void) { portEXIT_CRITICAL(&s_app_critical); }
app_lock_t *app_lock_create(void) { return (app_lock_t *)xSemaphoreCreateMutex(); }
void app_lock_take(app_lock_t *lock) { (void)xSemaphoreTake((SemaphoreHandle_t)lock, portMAX_DELAY); }
void app_lock_give(app_lock_t *lock) { (void)xSemaphoreGive((SemaphoreHandle_t)lock); }
typedef struct { char client[24], session[48], word[16], callback[32]; } reply_t;
static QueueHandle_t s_replies;
static char s_servo_owner[APP_ID_MAX + 1]; /* UI task only. */
void app_radar_read(app_radar_t *out)
{
    robot_status_t s; robot_get_status(&s); memset(out, 0, sizeof *out);
    out->available = s.radar_ready;
    out->online = s.radar_online; out->age_ms = s.radar_age_ms; out->frames = s.frames;
    out->rejected = s.rejected; out->frame = s.raw;
    robot_ui_t v; robot_ui_get(&v); out->range_mm = v.range_mm;
}
void app_servo_read(app_servo_t *out)
{
    robot_status_t s; robot_get_status(&s); memset(out, 0, sizeof *out);
    out->available = s.servo_ready;
    out->armed = s.armed; out->following = s.following; out->fault = s.hardware_fault;
    out->angle_deg = s.angle_deg; out->target_deg = s.target_deg; out->tracking = s.state;
    out->limit_deg = s.config.limit_deg; out->speed_dps = s.config.speed_dps;
}
void app_servo_cal_read(app_servo_cal_t *out)
{
    robot_status_t s; robot_get_status(&s); memset(out, 0, sizeof *out);
    out->available = s.servo_ready; out->stored = s.config_stored;
    out->config = s.servo_ready ? s.config : robot_default_config();
}
bool app_servo_cal_apply(app_context_t *ctx, const robot_config_t *config, bool save)
{
    if (s_servo_owner[0] && strcmp(s_servo_owner, ctx->app->id)) return false;
    return robot_servo_calibrate(config, save);
}
bool app_servo_request(app_context_t *ctx, app_servo_action_t action, int value)
{
    if (s_servo_owner[0] && strcmp(s_servo_owner, ctx->app->id)) return false;
    if (!robot_servo_request(action, value)) return false;
    snprintf(s_servo_owner, sizeof s_servo_owner, "%s", ctx->app->id); return true;
}
void app_services_release(const char *id, void *user)
{
    (void)user;
    if (!strcmp(s_servo_owner, id)) { (void)robot_servo_request(APP_SERVO_DISARM, 0); s_servo_owner[0] = '\0'; }
}
void app_display_read(robot_ui_t *out) { robot_ui_get(out); }
bool app_display_update(const char *json, size_t length, char *error, size_t size)
{ return robot_ui_submit(json, length, error, size) == ESP_OK; }
void app_key_word(unsigned key, char *out, size_t size) { if (size) snprintf(out, size, "%s", config_get_key((int)key)); }
void app_hook_read(app_hook_t *out)
{
    memset(out, 0, sizeof *out); out->connected = device_link_is_connected(); out->clients = dm_client_count();
    out->current = dm_current(out->client, out->session, sizeof out->client, sizeof out->session,
                              out->title, sizeof out->title, out->body, sizeof out->body);
    out->status = dm_current_status();
    for (unsigned i = 0; i < out->clients; ++i) {
        char client[24]; dm_client_at((int)i, client, sizeof client); out->unread += dm_client_unread(client);
    }
}
bool app_hook_reply(unsigned key)
{
    if (!s_replies || key > 2) return false;
    reply_t r = {0};
    (void)dm_current(r.client, r.session, sizeof r.client, sizeof r.session, NULL, 0, NULL, 0);
    app_key_word(key, r.word, sizeof r.word); snprintf(r.callback, sizeof r.callback, "%s", last_callback());
    return xQueueSend(s_replies, &r, 0) == pdTRUE;
}
/* One JSON POST to a callback on the local network: plain HTTP, every step bounded by timeout_ms. True when the answer
 * is a 2xx. (The string work is in main/http_post.c and has host tests.) */
static bool post_json(const char *callback, const char *path, const char *body, unsigned timeout_ms)
{
    char host[HTTP_POST_HOST_MAX], port[HTTP_POST_PORT_MAX], request[224];
    if (!http_post_target(callback, host, sizeof host, port, sizeof port)) return false;
    const int length = http_post_request(request, sizeof request, callback, path, body);
    if (length < 0) return false;
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *found = NULL;
    if (getaddrinfo(host, port, &hints, &found) != 0 || !found) return false;
    bool ok = false;
    const int fd = socket(found->ai_family, found->ai_socktype, 0);
    if (fd >= 0) {
        const struct timeval wait = {.tv_sec = (time_t)(timeout_ms / 1000U), .tv_usec = (suseconds_t)(timeout_ms % 1000U) * 1000};
        const int flags = fcntl(fd, F_GETFL, 0);
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK); /* connect() would block without a bound otherwise */
        int rc = connect(fd, found->ai_addr, found->ai_addrlen);
        if (rc < 0 && errno == EINPROGRESS) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(fd, &writable);
            struct timeval left = wait;
            int error = 0;
            socklen_t size = sizeof error;
            rc = select(fd + 1, NULL, &writable, NULL, &left) > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0 ? 0 : -1;
        }
        if (rc == 0) {
            (void)fcntl(fd, F_SETFL, flags);
            (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof wait);
            (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
            int sent = 0;
            while (sent < length) {
                const int n = send(fd, request + sent, (size_t)(length - sent), 0);
                if (n <= 0) break;
                sent += n;
            }
            char reply[32];
            size_t got = 0;
            while (sent == length && got < 12) { /* the status line is all that matters */
                const int n = recv(fd, reply + got, sizeof reply - got, 0);
                if (n <= 0) break;
                got += (size_t)n;
            }
            const int status = http_post_status(reply, got);
            ok = status >= 200 && status < 300;
        }
        close(fd);
    }
    freeaddrinfo(found);
    return ok;
}
static void reply_worker(void *user)
{
    (void)user; reply_t r;
    for (;;) {
        if (xQueueReceive(s_replies, &r, portMAX_DELAY) != pdTRUE) continue;
        bool sent = device_link_send_key(r.client, r.session, r.word);
        if (!sent && r.callback[0]) {
            cJSON *j = cJSON_CreateObject(); cJSON_AddStringToObject(j, "word", r.word);
            char *body = cJSON_PrintUnformatted(j);
            if (body) sent = post_json(r.callback, "/keyevent", body, 800);
            free(body); cJSON_Delete(j);
        }
        if (sent) {
            display_event_t e = {.kind = EVT_DISMISS}; strcpy(e.client_id, r.client); strcpy(e.session_id, r.session);
            (void)xQueueSend(display_q, &e, pdMS_TO_TICKS(100));
        } else cdc_write("ERR hook reply unavailable; notification retained");
    }
}
esp_err_t app_services_init(void)
{
    extern void app_hardware_init(void);
    app_hardware_init();
    s_replies = xQueueCreate(8, sizeof(reply_t));
    if (!s_replies) return ESP_ERR_NO_MEM;
    return xTaskCreate(reply_worker, "app_replies", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void app_device_info(app_device_info_t *out)
{
    memset(out, 0, sizeof *out); const esp_app_desc_t *d = esp_app_get_description();
    snprintf(out->version, sizeof out->version, "%s", d->version);
    snprintf(out->build, sizeof out->build, "%s %s", d->date, d->time);
    (void)config_get_link_host(out->daemon_host, sizeof out->daemon_host); out->daemon_port = config_get_link_port();
    snprintf(out->ip, sizeof out->ip, "%s", wifi_get_ip());
    robot_status_t status;robot_get_status(&status);
    out->robot_hardware=app_hardware_active()==APP_PROFILE_ROBOT || status.radar_online;
}
uint32_t app_random(void) { return esp_random(); }
uint64_t app_clock_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static volatile int s_tz_min = APP_TZ_DEFAULT_MIN;
void app_time_read(app_time_t *out)
{
    const time_t now = time(NULL); /* set over USB (app_time_set) or by SNTP once Wi-Fi is up */
    app_time_from_epoch(now > 0 ? (uint32_t)now : 0U, s_tz_min, out);
}
bool app_time_set(uint32_t epoch, int tz_min)
{
    if (epoch < APP_EPOCH_MIN || !app_time_tz_valid(tz_min)) return false;
    const struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
    if (settimeofday(&tv, NULL) != 0) return false;
    s_tz_min = tz_min;
    return true;
}
static bool storage_key_ok(const char *key)
{
    size_t n = key ? strlen(key) : 0;
    if (n < 1 || n > 15) return false;
    for (size_t i = 0; i < n; ++i)
        if (!((key[i] >= 'a' && key[i] <= 'z') || (key[i] >= '0' && key[i] <= '9') || key[i] == '_')) return false;
    return true;
}
bool app_storage_load(const char *key, void *out, size_t size)
{
    if (!out || !size || size > APP_STORAGE_MAX || !storage_key_ok(key)) return false;
    unsigned char buffer[APP_STORAGE_MAX]; size_t length = sizeof buffer;
    if (config_app_blob_get(key, buffer, &length) != ESP_OK || length != size) return false;
    memcpy(out, buffer, size);
    return true;
}
bool app_storage_save(const char *key, const void *data, size_t size)
{
    if (!data || !size || size > APP_STORAGE_MAX || !storage_key_ok(key)) return false;
    return config_app_blob_set(key, data, size) == ESP_OK;
}
void app_screen_read(app_screen_t *out) { dv_screen_read(out); }
bool app_screen_apply(const app_screen_t *want, bool save) { return dv_screen_apply(want, save); }
bool app_screen_raw(const uint8_t *bytes, size_t length) { return dv_screen_raw(bytes, length); }
bool app_screen_forget(void) { return dv_screen_forget(); }
bool app_transport_write(const char *line) { return cdc_write(line) == ESP_OK; }
bool app_host_event(const app_context_t *ctx, const char *action, unsigned key, const char *text)
{
    cJSON *j = cJSON_CreateObject(); if (!j) return false;
    cJSON_AddNumberToObject(j, "v", 1); cJSON_AddStringToObject(j, "type", "app_event");
    cJSON_AddStringToObject(j, "app", ctx->app->id); cJSON_AddStringToObject(j, "action", action);
    cJSON_AddNumberToObject(j, "ms", (double)ctx->now_ms); cJSON_AddNumberToObject(j, "key", key);
    if (text) cJSON_AddStringToObject(j, "text", text);
    char *body = cJSON_PrintUnformatted(j); bool ok = body && cdc_write(body) == ESP_OK;
    free(body); cJSON_Delete(j); return ok;
}
