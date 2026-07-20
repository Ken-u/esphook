/* main/display_model.h
 * 纯逻辑：session 列表 + 红点计数状态机。不碰屏，可单测。
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DM_STATUS_DONE = 0,
    DM_STATUS_CONFIRM,
    DM_STATUS_ERROR,
} dm_status_t;

void dm_init(void);

/* 新增或 +1，记为当前通知。title/body 可为 NULL。 */
void dm_notify(const char *client_id, const char *session_id,
               const char *title, const char *body);

/* 带状态的通知入口；dm_notify 保留为默认绿色完成态的兼容封装。 */
void dm_notify_status(const char *client_id, const char *session_id,
                      const char *title, const char *body,
                      dm_status_t status);

/* (client,session) 命中则 -1，归 0 移除；清的是当前通知则清当前。 */
void dm_dismiss(const char *client_id, const char *session_id);

int  dm_client_count(void);
int  dm_client_unread(const char *client_id);  /* 该 client 所有 session 合计 */
void dm_client_at(int idx, char *client_out, size_t len);

/* 取当前主区通知。任一 out 参数可传 NULL 忽略。无当前则返回 false。 */
bool dm_current(char *client_out, char *session_out, size_t clen, size_t slen,
                char *title, size_t tlen, char *body, size_t blen);

void dm_clear_current(void);

/* 当前提醒的颜色状态；没有当前提醒时返回 DM_STATUS_DONE。 */
dm_status_t dm_current_status(void);
