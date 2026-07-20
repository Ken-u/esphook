/* main/config_store.h */
#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t config_store_init(void);

esp_err_t config_set_wifi(const char *ssid, const char *pass);
esp_err_t config_get_wifi_ssid(char *out, size_t len);
esp_err_t config_get_wifi_pass(char *out, size_t len);

/* 主机 esphook daemon 反向连接配置。传 NULL/0 表示保持原值。 */
esp_err_t config_set_link(const char *host, uint16_t port, const char *secret_hex);
esp_err_t config_get_link_host(char *out, size_t len);
uint16_t config_get_link_port(void);
esp_err_t config_get_link_secret(char *out, size_t len);

/* keymap: 3 个按键词。keys[i]=NULL 表示该项不变。 */
esp_err_t config_set_keymap(const char *keys[3]);
/* index 0..2。返回内部静态缓冲，越界返回 "ok"。 */
const char *config_get_key(int index);

/* display 名映射。type="client" 或 "session"。name=NULL=删除。 */
esp_err_t config_set_display_name(const char *type, const char *id, const char *name);
/* 无映射时 out 为空串。返回 out。 */
const char *config_get_display_name(const char *type, const char *id, char *out, size_t len);

esp_err_t config_factory_reset(void);

/* 已见 client/session 列表（持久化，供 Web 页渲染可编辑列表）。
   type="client" 或 "session"。上限各 32。 */
void config_seen_add(const char *type, const char *id);
int  config_seen_count(const char *type);            /* 当前已见条数 */
void config_seen_at(const char *type, int idx, char *out, size_t len); /* 取第 idx 个，越界返回空 */
