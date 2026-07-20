/* main/ota_update.c
 *
 * OTA 只更新 ota_0/ota_1 应用程序分区。中文字体位图在固定的 fontdata
 * 数据分区中，只有 USB 完整烧录才会更新，不会被网页 OTA 覆盖。
 */
#include "ota_update.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ota";

#define OTA_RX_BUFFER_SIZE 4096

static bool s_ota_busy;

static esp_err_t ota_error(httpd_req_t *req, const char *message)
{
    char body[160];
    snprintf(body, sizeof body, "{\"ok\":false,\"err\":\"%s\"}", message);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}

esp_err_t ota_handle_upload(httpd_req_t *req)
{
    if (s_ota_busy) return ota_error(req, "ota busy");
    s_ota_busy = true;

    if (req->content_len <= 0) {
        s_ota_busy = false;
        return ota_error(req, "empty firmware");
    }

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update) {
        s_ota_busy = false;
        return ota_error(req, "no ota partition");
    }
    if ((size_t)req->content_len > update->size) {
        s_ota_busy = false;
        return ota_error(req, "firmware too large");
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(update, (size_t)req->content_len,
                                  &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_ota_busy = false;
        return ota_error(req, "ota begin failed");
    }

    uint8_t *buffer = malloc(OTA_RX_BUFFER_SIZE);
    if (!buffer) {
        esp_ota_abort(handle);
        s_ota_busy = false;
        return ota_error(req, "no memory");
    }

    int remaining = req->content_len;
    while (remaining > 0) {
        int want = remaining > OTA_RX_BUFFER_SIZE ? OTA_RX_BUFFER_SIZE : remaining;
        int received = httpd_req_recv(req, (char *)buffer, want);
        if (received <= 0) {
            ESP_LOGE(TAG, "firmware receive failed: %d", received);
            free(buffer);
            esp_ota_abort(handle);
            s_ota_busy = false;
            return ota_error(req, "firmware receive failed");
        }

        err = esp_ota_write(handle, buffer, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buffer);
            esp_ota_abort(handle);
            s_ota_busy = false;
            return ota_error(req, "ota write failed");
        }
        remaining -= received;
    }
    free(buffer);

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        s_ota_busy = false;
        return ota_error(req, "invalid firmware");
    }

    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set boot partition failed: %s", esp_err_to_name(err));
        s_ota_busy = false;
        return ota_error(req, "set boot partition failed");
    }

    ESP_LOGI(TAG, "OTA written to %s, rebooting", update->label);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":true}");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}
