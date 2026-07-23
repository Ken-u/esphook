/* main/device_link.h */
#pragma once

#include <stdbool.h>

/* 启动到主机 esphook daemon 的主动连接任务。 */
void device_link_start(void);

/* 当前是否有已认证的 device link。 */
bool device_link_is_connected(void);

/* 配置被 LAN 配对更新后，主动断开旧认证连接并重新握手。 */
void device_link_request_reconnect(void);

/* 通过已认证的长连接回传按键；未连接或队列满返回 false。 */
bool device_link_send_key(const char *client_id, const char *session_id,
                          const char *word);
