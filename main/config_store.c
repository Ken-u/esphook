/* main/config_store.c */
#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *NS_WIFI = "wifi";
static const char *NS_KEYMAP = "keymap";
static const char *NS_DISPLAY = "display";
static const char *NS_SEEN = "seen";
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
            char k[12];
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
        char k[12];
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
    const char *nss[4] = {NS_WIFI, NS_KEYMAP, NS_DISPLAY, NS_SEEN};
    for (int i = 0; i < 4; i++) {
        if (nvs_open(nss[i], NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    return ESP_OK;
}

/* ===== 已见 client/session 列表（持久化） ===== */
/* NVS 命名空间 seen，键 c0..c31 / s0..s31 存 id，cN/sN 存总数。
   首次 add 时遍历去重，已存在不重复加。 */
#define MAX_SEEN 32

void config_seen_add(const char *type, const char *id)
{
    if (!type || !id || !id[0]) return;
    nvs_handle_t h;
    if (nvs_open(NS_SEEN, NVS_READWRITE, &h) != ESP_OK) return;

    char prefix = (strcmp(type, "session") == 0) ? 's' : 'c';
    /* 读现有条数 */
    char cnt_key[4] = {0};
    snprintf(cnt_key, sizeof cnt_key, "%cN", prefix);
    int32_t cnt = 0;
    nvs_get_i32(h, cnt_key, &cnt);
    int n = (int)cnt;

    /* 去重：已存在则跳过 */
    for (int i = 0; i < n; i++) {
        char k[12]; snprintf(k, sizeof k, "%c%d", prefix, i);
        char existing[64] = {0};
        size_t len = sizeof existing;
        if (nvs_get_str(h, k, existing, &len) == ESP_OK &&
            strcmp(existing, id) == 0) {
            nvs_close(h);
            return;  /* 已在列表 */
        }
    }

    if (n >= MAX_SEEN) {
        /* 满了：丢最旧，整体前移。罕见，简化处理。 */
        n = MAX_SEEN - 1;
        for (int i = 0; i < n; i++) {
            char k0[12], k1[12];
            snprintf(k0, sizeof k0, "%c%d", prefix, i + 1);
            snprintf(k1, sizeof k1, "%c%d", prefix, i);
            char v[64] = {0}; size_t len = sizeof v;
            if (nvs_get_str(h, k0, v, &len) == ESP_OK) nvs_set_str(h, k1, v);
        }
    }

    char k[16]; snprintf(k, sizeof k, "%c%d", prefix, n);
    nvs_set_str(h, k, id);
    nvs_set_i32(h, cnt_key, n + 1);
    nvs_commit(h);
    nvs_close(h);
}

int config_seen_count(const char *type)
{
    nvs_handle_t h;
    if (nvs_open(NS_SEEN, NVS_READONLY, &h) != ESP_OK) return 0;
    char cnt_key[4] = {0};
    snprintf(cnt_key, sizeof cnt_key, "%cN", (strcmp(type, "session") == 0) ? 's' : 'c');
    int32_t cnt = 0;
    nvs_get_i32(h, cnt_key, &cnt);
    nvs_close(h);
    return (int)cnt;
}

void config_seen_at(const char *type, int idx, char *out, size_t len)
{
    if (len == 0) return;
    out[0] = '\0';
    if (idx < 0 || idx >= MAX_SEEN) return;
    nvs_handle_t h;
    if (nvs_open(NS_SEEN, NVS_READONLY, &h) != ESP_OK) return;
    char k[4]; snprintf(k, sizeof k, "%c%d", (strcmp(type, "session") == 0) ? 's' : 'c', idx);
    if (nvs_get_str(h, k, out, &len) != ESP_OK) out[0] = '\0';
    nvs_close(h);
}
