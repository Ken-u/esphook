/* main/net_wifi.c */
#include "net_wifi.h"
#include "config_store.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi";
static bool s_connected = false;
static char s_ip[16] = "";
static wifi_state_cb_t s_cb = NULL;
static bool s_inited = false;

static void notify(int state, const char *ip)
{
    if (s_cb) s_cb(state, ip);
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
    s_connected = true;
    ESP_LOGI(TAG, "got ip: %s", s_ip);
    notify(2, s_ip);
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started, connecting");
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = '\0';
        ESP_LOGW(TAG, "disconnected, retry in 10s");
        notify(1, "");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_wifi_connect();
    }
}

esp_err_t wifi_start(void)
{
    if (s_inited) {
        /* 已初始化过，直接重连 */
        esp_wifi_connect();
        return ESP_OK;
    }

    char ssid[64] = {0}, pass[64] = {0};
    config_get_wifi_ssid(ssid, sizeof ssid);
    config_get_wifi_pass(pass, sizeof pass);
    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "no wifi credentials");
        notify(0, "");
        return ESP_ERR_NOT_FOUND;
    }

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof wc.sta.ssid - 1);
    strncpy((char *)wc.sta.password, pass, sizeof wc.sta.password - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    notify(1, "");
    s_inited = true;
    return ESP_OK;
}

bool wifi_is_connected(void) { return s_connected; }
const char *wifi_get_ip(void) { return s_ip; }
void wifi_set_state_cb(wifi_state_cb_t cb) { s_cb = cb; }
