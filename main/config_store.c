/* main/config_store.c */
#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *NS_WIFI = "wifi";
static const char *NS_KEYMAP = "keymap";
static const char *NS_DISPLAY = "display";
static const char *DEFAULT_KEYS[3] = {"ok", "continue", "done"};

esp_err_t config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t config_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NS_WIFI, NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    if (ssid) nvs_set_str(h, "ssid", ssid);
    if (pass) nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t config_get_wifi_ssid(char *out, size_t len)
{
    if (len == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS_WIFI, NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;
    esp_err_t e = nvs_get_str(h, "ssid", out, &len);
    nvs_close(h);
    if (e != ESP_OK) out[0] = '\0';
    return e;
}

esp_err_t config_get_wifi_pass(char *out, size_t len)
{
    if (len == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS_WIFI, NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;
    esp_err_t e = nvs_get_str(h, "pass", out, &len);
    nvs_close(h);
    if (e != ESP_OK) out[0] = '\0';
    return e;
}

esp_err_t config_set_keymap(const char *keys[3])
{
    nvs_handle_t h;
    if (nvs_open(NS_KEYMAP, NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    for (int i = 0; i < 3; i++) {
        if (keys[i]) {
            char k[8];
            snprintf(k, sizeof(k), "key%d", i);
            nvs_set_str(h, k, keys[i]);
        }
    }
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

const char *config_get_key(int index)
{
    static char buf[32];
    if (index < 0 || index > 2) return "ok";
    nvs_handle_t h;
    if (nvs_open(NS_KEYMAP, NVS_READONLY, &h) == ESP_OK) {
        char k[8];
        snprintf(k, sizeof(k), "key%d", index);
        size_t len = sizeof(buf);
        if (nvs_get_str(h, k, buf, &len) == ESP_OK) { nvs_close(h); return buf; }
        nvs_close(h);
    }
    return DEFAULT_KEYS[index];
}

esp_err_t config_set_display_name(const char *type, const char *id, const char *name)
{
    nvs_handle_t h;
    if (nvs_open(NS_DISPLAY, NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    char key[64];
    snprintf(key, sizeof(key), "%s:%s", type, id);
    if (name) nvs_set_str(h, key, name);
    else nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

const char *config_get_display_name(const char *type, const char *id, char *out, size_t len)
{
    if (len == 0) return out;
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS_DISPLAY, NVS_READONLY, &h) != ESP_OK) return out;
    char key[64];
    snprintf(key, sizeof(key), "%s:%s", type, id);
    if (nvs_get_str(h, key, out, &len) != ESP_OK) out[0] = '\0';
    nvs_close(h);
    return out;
}

esp_err_t config_factory_reset(void)
{
    nvs_handle_t h;
    const char *nss[3] = {NS_WIFI, NS_KEYMAP, NS_DISPLAY};
    for (int i = 0; i < 3; i++) {
        if (nvs_open(nss[i], NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    return ESP_OK;
}
