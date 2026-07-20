/* main/ota_update.h */
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/* 接收一个 app .bin，写入下一个 OTA 主程序槽并重启。 */
esp_err_t ota_handle_upload(httpd_req_t *req);
