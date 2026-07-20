/* main/config_store.c */
#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *NS_WIFI = "wifi";
static const char *NS_LINK = "link";
static const char *NS_KEYMAP = "keymap";
static const char *NS_DISPLAY = "display";
static const char *NS_SEEN = "seen";
static const char *DEFAULT_KEYS[3] = {"ok", "continue", "done"};

/* NVS key 名最多 15 个字符，不能直接把 client/session id 拼进 key。
 * 显示名使用固定短 key 的 blob 记录，记录本身保存完整 type/id/name。 */
#define MAX_DISPLAY_RECORDS 64

typedef struct {
    char type[8];
    char id[64];
    char name[32];
} display_record_t;

static void display_slot_key(int idx, char *out, size_t len)
{
    if (len < 4) {
        if (len > 0) out[0] = '\0';
        return;
    }
    out[0] = 'd';
    out[1] = (char)('0' + (idx / 10) % 10);
    out[2] = (char)('0' + idx % 10);
    out[3] = '\0';
}

static esp_err_t display_read_slot(nvs_handle_t h, int idx, display_record_t *out)
{
    char key[8];
    size_t len = sizeof(*out);
    display_slot_key(idx, key, sizeof key);
    esp_err_t err = nvs_get_blob(h, key, out, &len);
    if (err != ESP_OK || len != sizeof(*out)) return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

static bool legacy_display_key(const char *type, const char *id,
                               char *out, size_t len)
{
    int n = snprintf(out, len, "%s:%s", type, id);
    return n > 0 && (size_t)n < NVS_KEY_NAME_MAX_SIZE;
}

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

esp_err_t config_set_link(const char *host, uint16_t port, const char *secret_hex)
{
    nvs_handle_t h;
    if (nvs_open(NS_LINK, NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    esp_err_t err = ESP_OK;
    if (host) err = nvs_set_str(h, "host", host);
    if (err == ESP_OK && port > 0) err = nvs_set_u16(h, "port", port);
    if (err == ESP_OK && secret_hex) err = nvs_set_str(h, "secret", secret_hex);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t config_get_link_host(char *out, size_t len)
{
    if (len == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS_LINK, NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;
    esp_err_t err = nvs_get_str(h, "host", out, &len);
    nvs_close(h);
    if (err != ESP_OK) out[0] = '\0';
    return err;
}

uint16_t config_get_link_port(void)
{
    nvs_handle_t h;
    uint16_t port = 0;
    if (nvs_open(NS_LINK, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u16(h, "port", &port);
        nvs_close(h);
    }
    return port;
}

esp_err_t config_get_link_secret(char *out, size_t len)
{
    if (len == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS_LINK, NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;
    esp_err_t err = nvs_get_str(h, "secret", out, &len);
    nvs_close(h);
    if (err != ESP_OK) out[0] = '\0';
    return err;
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
    if (!type || !id ||
        (strcmp(type, "client") != 0 && strcmp(type, "session") != 0))
        return ESP_ERR_INVALID_ARG;
    if (strlen(id) >= sizeof(((display_record_t *)0)->id) ||
        (name && strlen(name) >= sizeof(((display_record_t *)0)->name)))
        return ESP_ERR_INVALID_SIZE;

    nvs_handle_t h;
    if (nvs_open(NS_DISPLAY, NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;

    int found = -1;
    int empty = -1;
    display_record_t rec;
    for (int i = 0; i < MAX_DISPLAY_RECORDS; i++) {
        if (display_read_slot(h, i, &rec) != ESP_OK) {
            if (empty < 0) empty = i;
            continue;
        }
        if (strcmp(rec.type, type) == 0 && strcmp(rec.id, id) == 0) {
            found = i;
            break;
        }
    }

    esp_err_t err = ESP_OK;
    if (name) {
        int slot = found >= 0 ? found : empty;
        if (slot < 0) {
            nvs_close(h);
            return ESP_ERR_NO_MEM;
        }
        memset(&rec, 0, sizeof rec);
        snprintf(rec.type, sizeof rec.type, "%s", type);
        snprintf(rec.id, sizeof rec.id, "%s", id);
        snprintf(rec.name, sizeof rec.name, "%s", name);
        char key[8];
        display_slot_key(slot, key, sizeof key);
        err = nvs_set_blob(h, key, &rec, sizeof rec);
    } else if (found >= 0) {
        char key[8];
        display_slot_key(found, key, sizeof key);
        err = nvs_erase_key(h, key);
    }

    /* 兼容早期短 id 版本，并在成功迁移后清掉旧记录。 */
    char old_key[64];
    if (legacy_display_key(type, id, old_key, sizeof old_key)) {
        nvs_erase_key(h, old_key);
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

const char *config_get_display_name(const char *type, const char *id, char *out, size_t len)
{
    if (!type || !id || len == 0) return out;
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS_DISPLAY, NVS_READONLY, &h) != ESP_OK) return out;

    display_record_t rec;
    for (int i = 0; i < MAX_DISPLAY_RECORDS; i++) {
        if (display_read_slot(h, i, &rec) == ESP_OK &&
            strcmp(rec.type, type) == 0 && strcmp(rec.id, id) == 0) {
            snprintf(out, len, "%s", rec.name);
            nvs_close(h);
            return out;
        }
    }

    /* 兼容早期使用短 type:id key 的记录。 */
    char old_key[64];
    if (legacy_display_key(type, id, old_key, sizeof old_key)) {
        if (nvs_get_str(h, old_key, out, &len) != ESP_OK) out[0] = '\0';
    }
    nvs_close(h);
    return out;
}

esp_err_t config_factory_reset(void)
{
    nvs_handle_t h;
    const char *nss[5] = {NS_WIFI, NS_LINK, NS_KEYMAP, NS_DISPLAY, NS_SEEN};
    for (int i = 0; i < 5; i++) {
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
