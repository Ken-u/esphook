/* main/input_task.c
 * ① 机器人：GPIO6 游戏键；原 Hook：K1=6/K2=10/K3=11 扫描与回传
 * ② CDC 配网命令：ssid:/pass:/connect/status/reset
 *
 * 按键回传：用 esp_http_client 向 last_callback() 发 POST /keyevent {word}。
 * dismiss 不直接改 model，只发 EVT_DISMISS（display_task 统一改 model，避免竞态）。
 */
#include "input_task.h"
#include "sdkconfig.h"
#include "robot_runtime.h"
#include "robot_ui.h"
#include "game_runtime.h"
#include "esp_timer.h"
#include "esp_attr.h"
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

#ifndef CONFIG_DESKTOP_FOLLOW_ROBOT
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


#else
static void IRAM_ATTR robot_button_isr(void *arg)
{
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)arg, &wake);
    if (wake) portYIELD_FROM_ISR();
}
static void robot_button_task(void *arg)
{
    (void)arg;
    app_button_t button = {0};
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
    (void)app_button_sample(&button, gpio_get_level(APP_BUTTON_GPIO) == 0, now);
    TickType_t wait = portMAX_DELAY;
    while (1) {
        uint32_t edges = ulTaskNotifyTake(pdTRUE, wait);
        now = (uint64_t)esp_timer_get_time() / 1000;
        bool pressed = gpio_get_level(APP_BUTTON_GPIO) == 0;
        if (edges) app_button_edge(&button, pressed, now);
        bool was_pressed = button.stable_pressed;
        bool down = app_button_sample(&button, pressed, now);
        if (down) game_post_button(true, now);
        else if (was_pressed && !button.stable_pressed) game_post_button(false, now);
        if (button.raw_pressed != button.stable_pressed) {
            uint64_t elapsed = now >= button.changed_ms ? now - button.changed_ms : 0;
            unsigned remaining = elapsed < APP_DEBOUNCE_MS
                               ? APP_DEBOUNCE_MS - (unsigned)elapsed : 1;
            wait = pdMS_TO_TICKS(remaining);
            if (!wait) wait = 1;
        } else {
            wait = portMAX_DELAY; /* Idle and held states never poll the pin. */
        }
    }
}
void input_start(void)
{
    /* Sole physical button: GPIO6 to GND, internal pull-up. Never claim GPIO10. */
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << APP_BUTTON_GPIO, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
    TaskHandle_t button_task = NULL;
    if (xTaskCreate(robot_button_task, "game_button", 2048, NULL, 5, &button_task) != pdPASS) {
        ESP_LOGE("input", "could not start GPIO6 button task");
        return;
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(APP_BUTTON_GPIO, robot_button_isr, button_task));
    ESP_ERROR_CHECK(gpio_set_intr_type(APP_BUTTON_GPIO, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_intr_enable(APP_BUTTON_GPIO));
    /* Catch a transition between the task's initial sample and IRQ enable. */
    xTaskNotifyGive(button_task);
}
#endif

void input_cdc_handler(const char *line)
{
    if (game_command(line)) return;
    if (robot_ui_command(line) || robot_command(line)) return;
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
        cdc_write("apps: short GPIO6 opens menu, triple-click returns menu | robot menu | robot game start/stop/status");
        cdc_write("robot help: tracking / servo calibration");
        cdc_write("cmds: ssid:<s> pass:<p> daemon_host:<h> daemon_port:<p> daemon_secret:<64hex> connect status id reset");
    } else if (line[0] != '\0') {
        cdc_write("ERR unknown command (try: help)");
    }
}
