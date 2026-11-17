#include "stock_transport.h"
#include "esphook/app_command.h"
#include "esphook/app_services.h"
#include "esphook/app_sync.h"
#include <stdio.h>
#include <string.h>
static app_lock_t *s_lock;
static remote_game_t s_remote;
static uint32_t s_sequence; /* UI task owns outgoing input events. */
bool stock_transport_init(void) { s_lock = app_lock_create(); return s_lock != NULL; }
void stock_transport_view(uint64_t now, remote_game_view_t *view)
{
    app_lock_take(s_lock); remote_game_snapshot(&s_remote, now, view); app_lock_give(s_lock);
}
void stock_transport_event(const char *action, uint64_t ms)
{
    app_lock_take(s_lock);
    uint32_t id = s_remote.active ? s_remote.data.id : 0; unsigned cursor = s_remote.cursor;
    app_lock_give(s_lock);
    char message[210]; snprintf(message, sizeof message,
        "{\"v\":1,\"type\":\"game_button\",\"action\":\"%s\",\"id\":%lu,\"pressed\":%s,\"ms\":%llu,\"cursor\":%u,\"seq\":%lu}",
        action, (unsigned long)id, !strcmp(action, "down") ? "true" : "false", (unsigned long long)ms, cursor, (unsigned long)++s_sequence);
    (void)app_transport_write(message);
}
bool stock_transport_command(const char *line)
{
    if (strncmp(line, "game ", 5)) return false;
    char error[96], reply[210]; int seq = 0;
    app_lock_take(s_lock);
    bool before = s_remote.active;
    bool ok = remote_game_apply(&s_remote, line + 5, strlen(line + 5), app_clock_ms(), &seq, error, sizeof error);
    bool closed = before && !s_remote.active; app_lock_give(s_lock);
    if (closed && app_command_active("stock")) (void)app_command_menu();
    if (ok) snprintf(reply, sizeof reply, "{\"v\":1,\"type\":\"game_ack\",\"seq\":%d,\"ok\":true,\"selected\":%s}", seq, app_command_active("stock") ? "true" : "false");
    else snprintf(reply, sizeof reply, "{\"v\":1,\"type\":\"game_ack\",\"seq\":%d,\"ok\":false,\"error\":\"%s\"}", seq, error);
    (void)app_command_reply(reply); return true;
}
