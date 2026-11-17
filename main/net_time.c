/* main/net_time.c */
#include "net_time.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include <stdbool.h>

static const char *TAG = "time";
static bool s_started;

void net_time_start(void)
{
    if (s_started) return;
    /* ntp.aliyun.com answers from mainland China and worldwide; the library renews it over DHCP changes itself. */
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    config.wait_for_sync = false; /* never block the Wi-Fi event task */
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP did not start: %s", esp_err_to_name(err));
        return;
    }
    s_started = true;
    ESP_LOGI(TAG, "SNTP started");
}
