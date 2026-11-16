/* main/display_model.c */
#include "display_model.h"
#include <string.h>

#define MAX_SESSIONS 32

typedef struct {
    char client_id[24];
    char session_id[48];
    char title[32];
    char body[128];
    uint8_t unread;
    dm_status_t status;
    bool used;
} session_entry_t;

static session_entry_t s_sessions[MAX_SESSIONS];
static char s_cur_client[24];
static char s_cur_session[48];
static bool s_has_current = false;

static session_entry_t *find_session(const char *client, const char *session)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (s_sessions[i].used &&
            strcmp(s_sessions[i].client_id, client) == 0 &&
            strcmp(s_sessions[i].session_id, session) == 0)
            return &s_sessions[i];
    return NULL;
}

static session_entry_t *new_slot(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (!s_sessions[i].used) return &s_sessions[i];
    /* 全满：丢最旧（第一个 used 的） */
    session_entry_t *oldest = NULL;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].used) { oldest = &s_sessions[i]; break; }
    }
    if (oldest) oldest->used = false;
    return oldest;
}

void dm_init(void)
{
    memset(s_sessions, 0, sizeof(s_sessions));
    s_has_current = false;
    s_cur_client[0] = '\0';
    s_cur_session[0] = '\0';
}

static void copy_str(char *dst, size_t n, const char *src)
{
    if (n == 0) return;
    if (src) { strncpy(dst, src, n - 1); dst[n - 1] = '\0'; }
    else dst[0] = '\0';
}

void dm_notify(const char *client, const char *session, const char *title, const char *body)
{
    dm_notify_status(client, session, title, body, DM_STATUS_DONE);
}

void dm_notify_status(const char *client, const char *session, const char *title,
                      const char *body, dm_status_t status)
{
    if (status < DM_STATUS_DONE || status > DM_STATUS_ERROR) {
        status = DM_STATUS_DONE;
    }
    session_entry_t *e = find_session(client, session);
    if (!e) {
        e = new_slot();
        if (!e) return;
        copy_str(e->client_id, sizeof(e->client_id), client);
        copy_str(e->session_id, sizeof(e->session_id), session);
        e->used = true;
        e->unread = 0;
    }
    e->unread++;
    copy_str(e->title, sizeof(e->title), title);
    copy_str(e->body, sizeof(e->body), body);
    e->status = status;

    copy_str(s_cur_client, sizeof(s_cur_client), client);
    copy_str(s_cur_session, sizeof(s_cur_session), session);
    s_has_current = true;
}

void dm_dismiss(const char *client, const char *session)
{
    session_entry_t *e = find_session(client, session);
    if (!e) return;
    if (e->unread > 0) e->unread--;
    if (e->unread == 0) e->used = false;

    if (s_has_current &&
        strcmp(s_cur_client, client) == 0 &&
        strcmp(s_cur_session, session) == 0) {
        s_has_current = false;
    }
}

/* 收集不同 client_id 列表。seen 容量 max_seen，返回实际数。 */
static int collect_clients(char seen[][24], int max_seen)
{
    int n = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!s_sessions[i].used) continue;
        bool dup = false;
        for (int j = 0; j < n; j++)
            if (strcmp(seen[j], s_sessions[i].client_id) == 0) { dup = true; break; }
        if (!dup && n < max_seen) {
            strncpy(seen[n], s_sessions[i].client_id, 23);
            seen[n][23] = '\0';
            n++;
        }
    }
    return n;
}

int dm_client_count(void)
{
    char seen[MAX_SESSIONS][24];
    return collect_clients(seen, MAX_SESSIONS);
}

int dm_client_unread(const char *client)
{
    int sum = 0;
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (s_sessions[i].used && strcmp(s_sessions[i].client_id, client) == 0)
            sum += s_sessions[i].unread;
    return sum;
}

void dm_client_at(int idx, char *out, size_t len)
{
    if (len == 0) return;
    out[0] = '\0';
    char seen[MAX_SESSIONS][24];
    int n = collect_clients(seen, MAX_SESSIONS);
    if (idx < 0 || idx >= n) return;
    strncpy(out, seen[idx], len - 1);
    out[len - 1] = '\0';
}

bool dm_current(char *client_out, char *session_out, size_t clen, size_t slen,
                char *title, size_t tlen, char *body, size_t blen)
{
    if (!s_has_current) return false;
    if (client_out) copy_str(client_out, clen, s_cur_client);
    if (session_out) copy_str(session_out, slen, s_cur_session);
    session_entry_t *e = find_session(s_cur_client, s_cur_session);
    if (title) copy_str(title, tlen, e ? e->title : NULL);
    if (body) copy_str(body, blen, e ? e->body : NULL);
    return true;
}

void dm_clear_current(void)
{
    s_has_current = false;
}

dm_status_t dm_current_status(void)
{
    if (!s_has_current) return DM_STATUS_DONE;
    session_entry_t *e = find_session(s_cur_client, s_cur_session);
    return e ? e->status : DM_STATUS_DONE;
}
