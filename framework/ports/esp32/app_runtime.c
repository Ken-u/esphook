#include "app_runtime.h"
#include "app_manager.h"
#include "app_protocol.h"
#include "app_services.h"
#include "app_ui.h"
#include "display_view.h"
#include "display_model.h"
#include "config_store.h"
#include "usb_cdc.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { R_KEY, R_OPEN, R_MENU, R_STANDBY, R_MESSAGE, R_USB_KEY, R_USB_RELEASE } request_kind_t;
typedef struct { request_kind_t kind; uint64_t ms; unsigned key; bool down;
                 char id[APP_ID_MAX + 1], text[APP_MESSAGE_MAX + 1]; } request_t;
static QueueHandle_t s_requests;
static app_manager_t s_manager;
static char s_active[APP_ID_MAX + 1], s_standby[APP_ID_MAX + 1];
static bool s_started;
static bool s_map_reload;
static app_key_router_t s_keys;
static uint64_t s_reboot_at;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
extern esp_err_t app_services_init(void);
extern void app_services_release(const char *id, void *user);
static void *scene_create(void *user) { (void)user; return app_ui_layer(lv_screen_active()); }
static void scene_delete(void *root, void *user) { (void)user; lv_obj_delete(root); }
static bool save_standby(const char *id, void *user) { (void)user; return config_set_standby(id) == ESP_OK; }
static uint64_t clock_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static void key_edge(void *user,unsigned key,bool down,bool cancel,uint64_t ms)
{
    app_manager_t *m=user;
    if(cancel) app_manager_cancel_key(m,key,ms); else app_manager_key(m,key,down,ms);
}
static void publish(void)
{
    portENTER_CRITICAL(&s_lock);
    snprintf(s_active, sizeof s_active, "%s", app_active_id(&s_manager.context));
    snprintf(s_standby, sizeof s_standby, "%s", app_standby_id(&s_manager.context));
    portEXIT_CRITICAL(&s_lock);
}
esp_err_t app_runtime_init(void)
{
    if (s_requests) return ESP_ERR_INVALID_STATE;
    s_requests = xQueueCreate(16, sizeof(request_t));
    if (!s_requests) return ESP_ERR_NO_MEM;
    for (size_t i = 0; i < esphook_app_count; ++i)
        if (esphook_apps[i]->init && !esphook_apps[i]->init()) return ESP_ERR_INVALID_STATE;
    return app_services_init();
}
static bool post(const request_t *r) { return s_requests && xQueueSend(s_requests, r, 0) == pdTRUE; }
void app_runtime_key(unsigned key, bool down, uint64_t ms)
{ request_t r = {.kind = R_KEY, .key = key, .down = down, .ms = ms}; (void)post(&r); }
static const app_descriptor_t *find_app(const char *id)
{
    for (size_t i = 0; i < esphook_app_count; ++i) if (!strcmp(id, esphook_apps[i]->id)) return esphook_apps[i];
    return NULL;
}
bool app_runtime_is_active(const char *id)
{ portENTER_CRITICAL(&s_lock); bool active = !strcmp(s_active, id); portEXIT_CRITICAL(&s_lock); return active; }
bool app_runtime_open(const char *id)
{
    if (!id || !find_app(id)) return false;
    request_t r = {.kind = R_OPEN, .ms = clock_ms()}; snprintf(r.id, sizeof r.id, "%s", id); return post(&r);
}
bool app_runtime_menu(void) { request_t r = {.kind = R_MENU, .ms = clock_ms()}; return post(&r); }
bool app_runtime_message(const char *id, const char *json)
{
    if (!id || !json || strlen(json) > APP_MESSAGE_MAX || !app_runtime_is_active(id)) return false;
    request_t r = {.kind = R_MESSAGE, .ms = clock_ms()};
    snprintf(r.id, sizeof r.id, "%s", id); strcpy(r.text, json); return post(&r);
}
static void boot(uint64_t now)
{
    char standby[APP_ID_MAX + 1];
    if (config_get_standby(standby, sizeof standby) != ESP_OK || !find_app(standby) ||
        !(find_app(standby)->flags & APP_STANDBY)) strcpy(standby, "face");
    app_platform_t platform = {.scene_create = scene_create, .scene_delete = scene_delete,
        .release_services = app_services_release, .save_standby = save_standby};
    ESP_ERROR_CHECK(app_manager_init(&s_manager, esphook_apps, esphook_app_count, &platform, standby, now) ? ESP_OK : ESP_ERR_INVALID_STATE);
    app_keymap_t map;(void)config_get_input_keymap(&map);
    app_key_router_init(&s_keys,&map,key_edge,&s_manager);
    s_started = true; publish();
}
void app_runtime_display_event(const display_event_t *e, uint64_t now)
{
    if (!s_started) boot(now);
    if (e->kind == EVT_NOTIFY) {
        dm_notify_status(e->client_id, e->session_id, e->title, e->body, e->status);
        if (e->callback[0]) set_last_callback(e->callback);
        /* Notifications do not interrupt an active game or settings edit. */
        if (s_manager.idle) {
            for (size_t i = 0; i < esphook_app_count; ++i) {
                if (!(esphook_apps[i]->flags & APP_NOTIFICATIONS)) continue;
                (void)app_open(&s_manager.context, esphook_apps[i]->id); app_manager_commit(&s_manager); break;
            }
        }
    } else if (e->kind == EVT_DISMISS) dm_dismiss(e->client_id, e->session_id);
    app_event_t event = {.kind = e->kind == EVT_NET_STATE ? APP_EVENT_NETWORK : APP_EVENT_NOTIFY, .ms = now};
    app_manager_event(&s_manager, &event); publish();
}
void app_runtime_tick(uint64_t now)
{
    if (!s_started) boot(now);
    portENTER_CRITICAL(&s_lock);bool reload=s_map_reload;s_map_reload=false;portEXIT_CRITICAL(&s_lock);
    if(reload) { app_keymap_t map;(void)config_get_input_keymap(&map);(void)app_key_router_configure(&s_keys,&map); }
    request_t r;
    for (unsigned i = 0; i < 16 && xQueueReceive(s_requests, &r, 0); ++i) {
        s_manager.context.now_ms = r.ms;
        app_key_router_tick(&s_keys,r.ms);
        if (r.kind == R_KEY || r.kind == R_USB_KEY)
            app_key_router_edge(&s_keys,r.kind==R_KEY?APP_KEY_PHYSICAL:APP_KEY_USB,r.key,r.down,false,r.ms);
        else if(r.kind==R_USB_RELEASE) app_key_router_release_usb(&s_keys,r.ms);
        else if (r.kind == R_OPEN) (void)app_open(&s_manager.context, r.id);
        else if (r.kind == R_MENU) app_menu(&s_manager.context);
        else if (r.kind == R_STANDBY) {
            if (!app_set_standby(&s_manager.context, r.id)) cdc_write("ERR standby save failed");
        } else if (!strcmp(app_active_id(&s_manager.context), r.id)) {
            app_event_t e = {.kind = APP_EVENT_MESSAGE, .ms = r.ms, .text = r.text};
            app_manager_event(&s_manager, &e);
        }
        app_manager_commit(&s_manager); publish();
    }
    app_key_router_tick(&s_keys,now);
    app_manager_tick(&s_manager, now); publish(); app_manager_render(&s_manager);
    portENTER_CRITICAL(&s_lock);uint64_t restart=s_reboot_at;portEXIT_CRITICAL(&s_lock);
    if(restart && now>=restart) app_device_restart();
    /* Every app controls its own idle behavior; active animations stay awake. */
    dv_activity();
}
static void ack(const app_request_t *request, bool ok, bool queued)
{
    cJSON *j = cJSON_CreateObject(); if (!j) return;
    cJSON_AddNumberToObject(j, "v", 1); cJSON_AddStringToObject(j, "type", "app_ack");
    cJSON_AddNumberToObject(j, "seq", request->seq); cJSON_AddBoolToObject(j, "ok", ok);
    if (!ok) cJSON_AddStringToObject(j, "error", "invalid request, app unavailable or queue full");
    if (queued) cJSON_AddBoolToObject(j, "queued", true);
    if(ok && request->op==APP_OP_KEYMAP) {
        app_keymap_t map;(void)config_get_input_keymap(&map);
        cJSON *mapping=cJSON_AddObjectToObject(j,"mapping");
        cJSON *physical=cJSON_AddArrayToObject(mapping,"physical"), *usb=cJSON_AddArrayToObject(mapping,"usb"), *words=cJSON_AddArrayToObject(mapping,"words");
        for(unsigned i=0;i<3;++i) {
            cJSON_AddItemToArray(physical,cJSON_CreateNumber(map.physical[i]));
            cJSON_AddItemToArray(usb,cJSON_CreateNumber(map.usb[i]));
            cJSON_AddItemToArray(words,cJSON_CreateString(map.words[i]));
        }
        cJSON_AddNumberToObject(j,"usb_lease_ms",APP_USB_KEY_LEASE_MS);
    }
    if(ok && request->op==APP_OP_HARDWARE) {
        app_hardware_info_t info;app_hardware_info(&info);
        cJSON *hardware=cJSON_AddObjectToObject(j,"hardware");
        cJSON_AddStringToObject(hardware,"active",app_hardware_profile_name(info.active));
        cJSON_AddStringToObject(hardware,"configured",app_hardware_profile_name(info.configured));
        cJSON_AddBoolToObject(hardware,"restart_required",info.restart_required);
        cJSON_AddBoolToObject(hardware,"drivers_compiled",info.drivers_compiled);
        cJSON_AddNumberToObject(hardware,"physical_keys",info.physical_keys);
        const char *names[]={"unknown","initialized_connection_unknown","not_detected","detected","disabled","error"};
        cJSON_AddStringToObject(hardware,"screen",names[info.screen]);
        cJSON_AddStringToObject(hardware,"radar",names[info.radar]);
        cJSON_AddStringToObject(hardware,"servo",names[info.servo]);
        cJSON_AddNumberToObject(hardware,"radar_frames",info.radar_frames);
        if(request->reboot) cJSON_AddBoolToObject(j,"rebooting",true);
    }
    if (ok && (request->op == APP_OP_STATUS || request->op == APP_OP_LIST)) {
        char active[24], standby[24];
        portENTER_CRITICAL(&s_lock); strcpy(active, s_active); strcpy(standby, s_standby); portEXIT_CRITICAL(&s_lock);
        cJSON_AddStringToObject(j, "active", active); cJSON_AddStringToObject(j, "standby", standby);
        cJSON_AddNumberToObject(j, "api", APP_API_VERSION);
        if (request->op == APP_OP_LIST) {
            cJSON *apps = cJSON_AddArrayToObject(j, "apps");
            for (size_t i = 0; i < esphook_app_count; ++i) {
                const app_descriptor_t *a = esphook_apps[i]; if (a->flags & APP_HIDDEN) continue;
                cJSON *item = cJSON_CreateObject(); cJSON_AddItemToArray(apps, item);
                cJSON_AddStringToObject(item, "id", a->id); cJSON_AddStringToObject(item, "title", a->title);
                cJSON_AddBoolToObject(item, "standby", a->flags & APP_STANDBY);
            }
        }
    }
    char *body = cJSON_PrintUnformatted(j); if (body) { cdc_write(body); free(body); }
    cJSON_Delete(j);
}
bool app_runtime_command(const char *line)
{
    if (!strncmp(line, "app ", 4)) {
        app_request_t r; bool ok = app_protocol_parse(line + 4, strlen(line + 4), &r), queued = false;
        if (ok) {
            if (r.op == APP_OP_OPEN) ok = queued = app_runtime_open(r.app);
            else if (r.op == APP_OP_MENU) ok = queued = app_runtime_menu();
            else if (r.op == APP_OP_MESSAGE) ok = queued = app_runtime_message(r.app, r.message);
            else if (r.op == APP_OP_STANDBY) {
                const app_descriptor_t *a = find_app(r.app);
                request_t request = {.kind = R_STANDBY, .ms = clock_ms()}; strcpy(request.id, r.app);
                ok = queued = a && (a->flags & APP_STANDBY) && post(&request);
            } else if(r.op==APP_OP_KEY) {
                request_t request={.kind=R_USB_KEY,.key=r.key,.down=r.pressed,.ms=clock_ms()};ok=queued=post(&request);
            } else if(r.op==APP_OP_KEYS_RELEASE) {
                request_t request={.kind=R_USB_RELEASE,.ms=clock_ms()};ok=queued=post(&request);
            } else if(r.op==APP_OP_KEYMAP && r.keymap_set) {
                ok=config_set_input_keymap(&r.keymap)==ESP_OK;
                if(ok) { portENTER_CRITICAL(&s_lock);s_map_reload=true;portEXIT_CRITICAL(&s_lock); }
            } else if(r.op==APP_OP_HARDWARE && r.hardware_set) {
                ok=app_hardware_set_profile(r.hardware_profile);
                if(ok && r.reboot) { portENTER_CRITICAL(&s_lock);s_reboot_at=clock_ms()+500;portEXIT_CRITICAL(&s_lock); }
            }
        }
        ack(&r, ok, queued); return true;
    }
    if (!strcmp(line, "robot menu")) { cdc_write(app_runtime_menu() ? "OK menu queued" : "ERR menu queue full"); return true; }
    for (size_t i = 0; i < esphook_app_count; ++i)
        if (esphook_apps[i]->command && esphook_apps[i]->command(line)) return true;
    return false;
}
