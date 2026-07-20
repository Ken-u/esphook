/* main/input_task.c
 * ① 按键扫描（K1=6/K2=10/K3=11，消抖）：查词表→POST /keyevent 回传→dismiss+beep
 * ② CDC 配网命令：ssid:/pass:/connect/status/reset
 *
 * 按键回传：用 esp_http_client 向 last_callback() 发 POST /keyevent {word}。
 * dismiss 不直接改 model，只发 EVT_DISMISS（display_task 统一改 model，避免竞态）。
 */
#include "input_task.h"
#include "usb_cdc.h"
#include "config_store.h"
#include "net_wifi.h"
#include "display_model.h"
#include "display_view.h"
#include "device_link.h"
#include "events.h"
#include "beeper.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "input";
static const int KEY_GPIOS[3] = {6, 10, 11};

/* 把 word POST 到 host 的回传小程序 /keyevent。短超时，失败静默。 */
static void post_keyevent(const char *word)
{
    const char *cb = last_callback();
    if (cb[0] == '\0') {
        ESP_LOGW(TAG, "no callback, key '%s' dropped", word);
        return;
    }
    /* 拼 http://<cb>/keyevent */
    char url[64];
    snprintf(url, sizeof url, "http://%s/keyevent", cb);

    char body[64];
    snprintf(body, sizeof body, "{\"word\":\"%s\"}", word);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 800,
        .method = HTTP_METHOD_POST,
        .buffer_size = 256,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return;
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_post_field(cli, body, strlen(body));
    esp_err_t err = esp_http_client_perform(cli);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "keyevent -> %s: %d", url, esp_http_client_get_status_code(cli));
    } else {
        ESP_LOGW(TAG, "keyevent failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(cli);
}

static void on_key(int idx)
{
    const char *word = config_get_key(idx);
    ESP_LOGI(TAG, "key%d pressed -> '%s'", idx, word);

    /* 取当前通知上下文；反向连接回传时不再需要 callback 地址。 */
    char client[24], session[48];
    display_event_t e = {0};
    e.kind = EVT_DISMISS;
    if (dm_current(client, session, sizeof client, sizeof session, NULL, 0, NULL, 0)) {
        strncpy(e.client_id, client, sizeof e.client_id - 1);
        strncpy(e.session_id, session, sizeof e.session_id - 1);
    } else {
        client[0] = '\0';
        session[0] = '\0';
    }

    /* 优先沿已认证的 device link 回传；没有长连接时兼容旧 HTTP callback。 */
    if (!device_link_send_key(client, session, word)) {
        post_keyevent(word);
    }

    /* dismiss 当前通知（发事件，display_task 改 model）。即使没有当前通知，
       也发事件，让按键可以唤醒已经熄灭的屏幕。 */
    xQueueSend(display_q, &e, 0);
    beep_kind_t bk = BEEP_KEY;
    xQueueSend(beeper_q, &bk, 0);
}

static void input_task_fn(void *arg)
{
    int last[3] = {1, 1, 1};
    while (1) {
        for (int i = 0; i < 3; i++) {
            int lvl = gpio_get_level(KEY_GPIOS[i]);
            if (last[i] == 1 && lvl == 0) {
                vTaskDelay(pdMS_TO_TICKS(20)); /* 消抖 */
                if (gpio_get_level(KEY_GPIOS[i]) == 0) on_key(i);
            }
            last[i] = lvl;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void input_start(void)
{
    for (int i = 0; i < 3; i++) {
        gpio_config_t g = {
            .pin_bit_mask = (1ULL << KEY_GPIOS[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = true,
        };
        gpio_config(&g);
    }
    xTaskCreate(input_task_fn, "input", 4096, NULL, 5, NULL);
}

void input_cdc_handler(const char *line)
{
    if (strncmp(line, "ssid:", 5) == 0) {
        config_set_wifi(line + 5, NULL);
        cdc_write("OK ssid set");
    } else if (strncmp(line, "pass:", 5) == 0) {
        config_set_wifi(NULL, line + 5);
        cdc_write("OK pass set");
    } else if (strcmp(line, "connect") == 0) {
        cdc_write("connecting...");
        wifi_start();
    } else if (strcmp(line, "status") == 0) {
        char buf[112];
        snprintf(buf, sizeof buf, "wifi=%s ip=%s clients=%d link=%s",
                 wifi_is_connected() ? "connected" : "disconnected",
                 wifi_get_ip(), dm_client_count(),
                 device_link_is_connected() ? "connected" : "offline");
        cdc_write(buf);
    } else if (strcmp(line, "id") == 0) {
        uint8_t mac[6] = {0};
        char id[24];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(id, sizeof id, "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        cdc_write(id);
    } else if (strncmp(line, "daemon_host:", 12) == 0) {
        config_set_link(line + 12, 0, NULL);
        cdc_write("OK daemon host set");
    } else if (strncmp(line, "daemon_port:", 12) == 0) {
        unsigned long port = strtoul(line + 12, NULL, 10);
        if (port == 0 || port > 65535) {
            cdc_write("ERR bad daemon port");
        } else {
            config_set_link(NULL, (uint16_t)port, NULL);
            cdc_write("OK daemon port set");
        }
    } else if (strncmp(line, "daemon_secret:", 14) == 0) {
        const char *secret = line + 14;
        if (strlen(secret) != 64) {
            cdc_write("ERR daemon secret must be 64 hex chars");
        } else {
            config_set_link(NULL, 0, secret);
            cdc_write("OK daemon secret set");
        }
    } else if (strcmp(line, "reset") == 0) {
        config_factory_reset();
        cdc_write("OK factory reset, rebooting");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else if (strcmp(line, "help") == 0) {
        cdc_write("cmds: ssid:<s> pass:<p> daemon_host:<h> daemon_port:<p> daemon_secret:<64hex> connect status id reset");
    } else if (line[0] != '\0') {
        cdc_write("ERR unknown command (try: help)");
    }
}
