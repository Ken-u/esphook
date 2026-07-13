/* main/net_wifi.h */
#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* state: 0=未配置/失败 1=连接中 2=已连接 */
typedef void (*wifi_state_cb_t)(int state, const char *ip);

esp_err_t wifi_start(void);
bool wifi_is_connected(void);
const char *wifi_get_ip(void);
void wifi_set_state_cb(wifi_state_cb_t cb);
