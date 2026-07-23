/* main/device_link.c
 *
 * 到主机 esphook daemon 的主动 TCP 长连接：
 *   4-byte network-order length + UTF-8 JSON
 *
 * device link 只负责传输和认证，显示状态仍由 display_task 统一处理。
 */
#include "device_link.h"
#include "config_store.h"
#include "net_wifi.h"
#include "events.h"
#include "beeper.h"
#include "usb_cdc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "mbedtls/md.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "link";

#define LINK_HOST_MAX 96
#define LINK_SECRET_MAX 65
#define LINK_FRAME_MAX 8192
#define LINK_RETRY_MS 5000
#define LINK_HEARTBEAT_US (20LL * 1000000LL)
#define PAIR_DISCOVERY_PORT 18766
#define PAIR_FRAME_MAX 512

typedef struct {
    char client_id[24];
    char session_id[48];
    char word[32];
} key_tx_t;

static QueueHandle_t s_key_q;
static volatile bool s_connected;
static volatile bool s_reconnect_requested;
static bool s_started;
static char s_last_pair_id[64];
static char s_last_pair_secret[LINK_SECRET_MAX];

static void copy_string(char *dst, size_t len, const char *src)
{
    if (len == 0) return;
    if (src) {
        strncpy(dst, src, len - 1);
        dst[len - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}
static void device_id(char *out, size_t len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode_secret(const char *hex, uint8_t *out, size_t out_len)
{
    if (!hex || strlen(hex) != out_len * 2) return false;
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_value(hex[i * 2]);
        int lo = hex_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool valid_secret_hex(const char *secret)
{
    uint8_t decoded[32];
    return decode_secret(secret, decoded, sizeof decoded);
}

static bool make_proof(const char *secret_hex, const char *id,
                       const char *client_nonce, const char *server_nonce,
                       char *out, size_t out_len)
{
    uint8_t secret[32];
    uint8_t digest[32];
    char material[160];
    if (out_len < 65 || !decode_secret(secret_hex, secret, sizeof secret)) return false;
    int n = snprintf(material, sizeof material, "%s:%s:%s", id, client_nonce, server_nonce);
    if (n <= 0 || (size_t)n >= sizeof material) return false;

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info || mbedtls_md_hmac(info, secret, sizeof secret,
                                 (const unsigned char *)material, (size_t)n,
                                 digest) != 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof digest; i++) {
        snprintf(out + i * 2, out_len - i * 2, "%02x", digest[i]);
    }
    out[64] = '\0';
    return true;
}

static esp_err_t send_all(int fd, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        int n = send(fd, p, len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return ESP_FAIL;
        p += n;
        len -= (size_t)n;
    }
    return ESP_OK;
}

static esp_err_t send_json(int fd, cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    if (!text) return ESP_ERR_NO_MEM;
    size_t len = strlen(text);
    if (len == 0 || len > LINK_FRAME_MAX) {
        free(text);
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t header[4] = {
        (uint8_t)((len >> 24) & 0xff),
        (uint8_t)((len >> 16) & 0xff),
        (uint8_t)((len >> 8) & 0xff),
        (uint8_t)(len & 0xff),
    };
    esp_err_t err = send_all(fd, header, sizeof header);
    if (err == ESP_OK) err = send_all(fd, text, len);
    free(text);
    return err;
}

static esp_err_t recv_all_timeout(int fd, void *data, size_t len, int timeout_ms)
{
    uint8_t *p = (uint8_t *)data;
    while (len > 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval tv = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        int ready = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (ready == 0) return ESP_ERR_TIMEOUT;
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return ESP_FAIL;
        int n = recv(fd, p, len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return ESP_FAIL;
        p += n;
        len -= (size_t)n;
    }
    return ESP_OK;
}

static cJSON *recv_json(int fd, int timeout_ms, esp_err_t *err_out)
{
    uint8_t header[4];
    esp_err_t err = recv_all_timeout(fd, header, sizeof header, timeout_ms);
    if (err != ESP_OK) {
        if (err_out) *err_out = err;
        return NULL;
    }
    uint32_t len = ((uint32_t)header[0] << 24) |
                   ((uint32_t)header[1] << 16) |
                   ((uint32_t)header[2] << 8) | header[3];
    if (len == 0 || len > LINK_FRAME_MAX) {
        if (err_out) *err_out = ESP_ERR_INVALID_SIZE;
        return NULL;
    }
    char *text = calloc(1, len + 1);
    if (!text) {
        if (err_out) *err_out = ESP_ERR_NO_MEM;
        return NULL;
    }
    err = recv_all_timeout(fd, text, len, timeout_ms);
    if (err != ESP_OK) {
        free(text);
        if (err_out) *err_out = err;
        return NULL;
    }
    cJSON *json = cJSON_ParseWithLength(text, len);
    free(text);
    if (err_out) *err_out = json ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    return json;
}

static int connect_host(const char *host, uint16_t port)
{
    char port_text[8];
    snprintf(port_text, sizeof port_text, "%u", (unsigned)port);
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *result = NULL;
    if (getaddrinfo(host, port_text, &hints, &result) != 0) return -1;

    int fd = -1;
    for (struct addrinfo *it = result; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    return fd;
}

static bool handshake(int fd, const char *secret, char *id, size_t id_len)
{
    device_id(id, id_len);
    uint8_t nonce_bytes[16];
    esp_fill_random(nonce_bytes, sizeof nonce_bytes);
    char client_nonce[33];
    for (size_t i = 0; i < sizeof nonce_bytes; i++) {
        snprintf(client_nonce + i * 2, sizeof client_nonce - i * 2,
                 "%02x", nonce_bytes[i]);
    }
    client_nonce[32] = '\0';

    cJSON *hello = cJSON_CreateObject();
    if (!hello) return false;
    cJSON_AddStringToObject(hello, "op", "hello");
    cJSON_AddStringToObject(hello, "device_id", id);
    cJSON_AddStringToObject(hello, "client_nonce", client_nonce);
    cJSON_AddStringToObject(hello, "fw", "supermini-aihook");
    esp_err_t err = send_json(fd, hello);
    cJSON_Delete(hello);
    if (err != ESP_OK) return false;

    esp_err_t recv_err = ESP_OK;
    cJSON *challenge = recv_json(fd, 10000, &recv_err);
    if (!challenge) return false;
    cJSON *server_nonce_json = cJSON_GetObjectItem(challenge, "server_nonce");
    char proof[65] = {0};
    bool ok = cJSON_IsString(server_nonce_json) &&
              make_proof(secret, id, client_nonce, server_nonce_json->valuestring,
                         proof, sizeof proof);
    cJSON_Delete(challenge);
    if (!ok) return false;

    cJSON *auth = cJSON_CreateObject();
    if (!auth) return false;
    cJSON_AddStringToObject(auth, "op", "auth");
    cJSON_AddStringToObject(auth, "proof", proof);
    err = send_json(fd, auth);
    cJSON_Delete(auth);
    if (err != ESP_OK) return false;

    cJSON *accepted = recv_json(fd, 10000, &recv_err);
    if (!accepted) return false;
    cJSON *op = cJSON_GetObjectItem(accepted, "op");
    ok = cJSON_IsString(op) && strcmp(op->valuestring, "auth_ok") == 0;
    cJSON_Delete(accepted);
    return ok;
}

static bool queue_display_notify(int fd, cJSON *message)
{
    cJSON *client = cJSON_GetObjectItem(message, "client_id");
    cJSON *session = cJSON_GetObjectItem(message, "session_id");
    cJSON *title = cJSON_GetObjectItem(message, "title");
    cJSON *body = cJSON_GetObjectItem(message, "body");
    cJSON *status_json = cJSON_GetObjectItem(message, "status");
    if (!cJSON_IsString(client) || !cJSON_IsString(session)) return false;

    dm_status_t status = DM_STATUS_DONE;
    beep_kind_t beep = BEEP_NOTIFY;
    if (cJSON_IsString(status_json) && status_json->valuestring[0]) {
        if (strcmp(status_json->valuestring, "confirm") == 0) {
            status = DM_STATUS_CONFIRM;
            beep = BEEP_CONFIRM;
        } else if (strcmp(status_json->valuestring, "error") == 0) {
            status = DM_STATUS_ERROR;
            beep = BEEP_ERROR;
        } else if (strcmp(status_json->valuestring, "done") != 0) {
            return false;
        }
    }

    display_event_t event = {0};
    event.kind = EVT_NOTIFY;
    copy_string(event.client_id, sizeof event.client_id, client->valuestring);
    copy_string(event.session_id, sizeof event.session_id, session->valuestring);
    event.status = status;
    if (cJSON_IsString(title)) copy_string(event.title, sizeof event.title, title->valuestring);
    if (cJSON_IsString(body)) copy_string(event.body, sizeof event.body, body->valuestring);
    config_seen_add("client", event.client_id);
    config_seen_add("session", event.session_id);

    if (xQueueSend(display_q, &event, 0) != pdTRUE ||
        xQueueSend(beeper_q, &beep, 0) != pdTRUE) {
        return false;
    }
    return true;
}

static bool queue_display_dismiss(cJSON *message)
{
    cJSON *client = cJSON_GetObjectItem(message, "client_id");
    cJSON *session = cJSON_GetObjectItem(message, "session_id");
    if (!cJSON_IsString(client) || !cJSON_IsString(session)) return false;
    display_event_t event = {0};
    event.kind = EVT_DISMISS;
    copy_string(event.client_id, sizeof event.client_id, client->valuestring);
    copy_string(event.session_id, sizeof event.session_id, session->valuestring);
    return xQueueSend(display_q, &event, 0) == pdTRUE;
}

static void send_ack(int fd, cJSON *message, bool ok, const char *error)
{
    cJSON *ack = cJSON_CreateObject();
    if (!ack) return;
    cJSON_AddStringToObject(ack, "op", "ack");
    cJSON *request_id = cJSON_GetObjectItem(message, "request_id");
    if (cJSON_IsString(request_id)) cJSON_AddStringToObject(ack, "request_id", request_id->valuestring);
    cJSON_AddBoolToObject(ack, "ok", ok);
    if (!ok && error) cJSON_AddStringToObject(ack, "error", error);
    send_json(fd, ack);
    cJSON_Delete(ack);
}

static bool handle_message(int fd, cJSON *message)
{
    cJSON *op = cJSON_GetObjectItem(message, "op");
    if (!cJSON_IsString(op)) return true;
    if (strcmp(op->valuestring, "notify") == 0) {
        bool ok = queue_display_notify(fd, message);
        send_ack(fd, message, ok, ok ? NULL : "notify rejected");
    } else if (strcmp(op->valuestring, "dismiss") == 0) {
        bool ok = queue_display_dismiss(message);
        send_ack(fd, message, ok, ok ? NULL : "dismiss rejected");
    } else if (strcmp(op->valuestring, "ping") == 0) {
        cJSON *pong = cJSON_CreateObject();
        if (pong) {
            cJSON_AddStringToObject(pong, "op", "pong");
            send_json(fd, pong);
            cJSON_Delete(pong);
        }
    }
    return true;
}

static void send_pair_reply(int fd, const struct sockaddr *peer, socklen_t peer_len,
                            const char *pair_id, bool ok, const char *error)
{
    cJSON *reply = cJSON_CreateObject();
    if (!reply) return;
    cJSON_AddStringToObject(reply, "op", "pair_ack");
    if (pair_id) cJSON_AddStringToObject(reply, "pair_id", pair_id);
    cJSON_AddBoolToObject(reply, "ok", ok);
    if (ok) {
        char id[16] = {0};
        device_id(id, sizeof id);
        cJSON_AddStringToObject(reply, "device_id", id);
        cJSON_AddStringToObject(reply, "ip", wifi_get_ip());
    } else if (error) {
        cJSON_AddStringToObject(reply, "err", error);
    }
    char *text = cJSON_PrintUnformatted(reply);
    if (text) {
        sendto(fd, text, strlen(text), 0, peer, peer_len);
        free(text);
    }
    cJSON_Delete(reply);
}

static void handle_pair_packet(int fd, const char *data, size_t len,
                               const struct sockaddr *peer, socklen_t peer_len)
{
    cJSON *message = cJSON_ParseWithLength(data, len);
    if (!message) return;
    cJSON *op = cJSON_GetObjectItem(message, "op");
    cJSON *pair_id = cJSON_GetObjectItem(message, "pair_id");
    if (!cJSON_IsString(op) || strcmp(op->valuestring, "pair_request") != 0 ||
        !cJSON_IsString(pair_id) || !pair_id->valuestring[0] ||
        strlen(pair_id->valuestring) >= sizeof s_last_pair_id) {
        cJSON_Delete(message);
        return;
    }
    cJSON *target_id = cJSON_GetObjectItem(message, "device_id");
    if (cJSON_IsString(target_id) && target_id->valuestring[0]) {
        char local_id[16] = {0};
        device_id(local_id, sizeof local_id);
        if (strcmp(target_id->valuestring, local_id) != 0) {
            cJSON_Delete(message);
            return;
        }
    }

    cJSON *host = cJSON_GetObjectItem(message, "daemon_host");
    cJSON *port = cJSON_GetObjectItem(message, "daemon_port");
    cJSON *secret = cJSON_GetObjectItem(message, "daemon_secret");
    cJSON *current = cJSON_GetObjectItem(message, "current_secret");
    bool valid = cJSON_IsString(host) && host->valuestring[0] &&
                 strlen(host->valuestring) < LINK_HOST_MAX &&
                 cJSON_IsNumber(port) && port->valuedouble >= 1 &&
                 port->valuedouble <= 65535 &&
                 cJSON_IsString(secret) && valid_secret_hex(secret->valuestring);
    char existing[LINK_SECRET_MAX] = {0};
    config_get_link_secret(existing, sizeof existing);
    bool duplicate = valid && existing[0] &&
                     strcmp(existing, secret->valuestring) == 0 &&
                     strcmp(s_last_pair_id, pair_id->valuestring) == 0 &&
                     strcmp(s_last_pair_secret, secret->valuestring) == 0;
    if (duplicate) {
        send_pair_reply(fd, peer, peer_len, pair_id->valuestring, true, NULL);
        cJSON_Delete(message);
        return;
    }
    if (valid && existing[0] &&
        (!cJSON_IsString(current) || strcmp(current->valuestring, existing) != 0)) {
        valid = false;
    }
    if (!valid) {
        send_pair_reply(fd, peer, peer_len, pair_id->valuestring, false,
                        existing[0] ? "current secret required" : "invalid pair request");
        cJSON_Delete(message);
        return;
    }

    esp_err_t err = config_set_link(host->valuestring, (uint16_t)port->valueint,
                                    secret->valuestring);
    if (err != ESP_OK) {
        send_pair_reply(fd, peer, peer_len, pair_id->valuestring, false,
                        "could not save pair config");
        cJSON_Delete(message);
        return;
    }
    device_link_request_reconnect();
    snprintf(s_last_pair_id, sizeof s_last_pair_id, "%s", pair_id->valuestring);
    snprintf(s_last_pair_secret, sizeof s_last_pair_secret, "%s", secret->valuestring);
    send_pair_reply(fd, peer, peer_len, pair_id->valuestring, true, NULL);
    ESP_LOGI(TAG, "LAN pair accepted from %s", "broadcast");
    cJSON_Delete(message);
}

static int open_pair_socket(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(PAIR_DISCOVERY_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&address, sizeof address) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void pairing_task(void *arg)
{
    (void)arg;
    int fd = -1;
    while (1) {
        if (!wifi_is_connected()) {
            if (fd >= 0) { close(fd); fd = -1; }
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        if (fd < 0) {
            fd = open_pair_socket();
            if (fd < 0) {
                ESP_LOGW(TAG, "LAN pair UDP bind failed");
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            ESP_LOGI(TAG, "LAN pair UDP listening on %u", (unsigned)PAIR_DISCOVERY_PORT);
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
        int ready = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            close(fd);
            fd = -1;
            continue;
        }
        if (ready == 0 || !FD_ISSET(fd, &readfds)) continue;

        char data[PAIR_FRAME_MAX];
        struct sockaddr_storage peer = {0};
        socklen_t peer_len = sizeof peer;
        int len = recvfrom(fd, data, sizeof data, 0,
                           (struct sockaddr *)&peer, &peer_len);
        if (len > 0) {
            handle_pair_packet(fd, data, (size_t)len,
                               (const struct sockaddr *)&peer, peer_len);
        }
    }
}

static void device_link_task(void *arg)
{
    (void)arg;
    while (1) {
        /* A LAN pair request may arrive while the old link is being torn down.
           Clear a stale request before reading the newly saved credentials. */
        s_reconnect_requested = false;
        char host[LINK_HOST_MAX] = {0};
        char secret[LINK_SECRET_MAX] = {0};
        config_get_link_host(host, sizeof host);
        config_get_link_secret(secret, sizeof secret);
        uint16_t port = config_get_link_port();
        if (!wifi_is_connected() || !host[0] || port == 0 || !secret[0]) {
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        int fd = connect_host(host, port);
        if (fd < 0) {
            ESP_LOGW(TAG, "connect %s:%u failed", host, (unsigned)port);
            vTaskDelay(pdMS_TO_TICKS(LINK_RETRY_MS));
            continue;
        }

        char id[16] = {0};
        if (!handshake(fd, secret, id, sizeof id)) {
            ESP_LOGW(TAG, "daemon authentication failed");
            close(fd);
            vTaskDelay(pdMS_TO_TICKS(LINK_RETRY_MS));
            continue;
        }
        s_connected = true;
        ESP_LOGI(TAG, "device link connected to %s:%u as %s", host, (unsigned)port, id);
        int64_t last_heartbeat = esp_timer_get_time();
        bool link_ok = true;
        while (link_ok && wifi_is_connected() && !s_reconnect_requested) {
            key_tx_t tx;
            while (xQueueReceive(s_key_q, &tx, 0) == pdTRUE) {
                cJSON *key = cJSON_CreateObject();
                if (!key) { link_ok = false; break; }
                cJSON_AddStringToObject(key, "op", "key");
                cJSON_AddStringToObject(key, "client_id", tx.client_id);
                cJSON_AddStringToObject(key, "session_id", tx.session_id);
                cJSON_AddStringToObject(key, "word", tx.word);
                if (send_json(fd, key) != ESP_OK) link_ok = false;
                cJSON_Delete(key);
                if (!link_ok) break;
            }
            if (!link_ok) break;

            int64_t now = esp_timer_get_time();
            if (now - last_heartbeat >= LINK_HEARTBEAT_US) {
                cJSON *heartbeat = cJSON_CreateObject();
                if (!heartbeat) { link_ok = false; break; }
                cJSON_AddStringToObject(heartbeat, "op", "heartbeat");
                if (send_json(fd, heartbeat) != ESP_OK) link_ok = false;
                cJSON_Delete(heartbeat);
                last_heartbeat = now;
                if (!link_ok) break;
            }

            esp_err_t recv_err = ESP_OK;
            cJSON *message = recv_json(fd, 200, &recv_err);
            if (message) {
                handle_message(fd, message);
                cJSON_Delete(message);
            } else if (recv_err != ESP_ERR_TIMEOUT) {
                link_ok = false;
            }
        }
        s_connected = false;
        close(fd);
        s_reconnect_requested = false;
        ESP_LOGW(TAG, "device link disconnected; retrying");
        vTaskDelay(pdMS_TO_TICKS(LINK_RETRY_MS));
    }
}

void device_link_start(void)
{
    if (s_started) return;
    s_started = true;
    s_key_q = xQueueCreate(8, sizeof(key_tx_t));
    if (!s_key_q) {
        ESP_LOGE(TAG, "key event queue allocation failed");
        return;
    }
    xTaskCreate(device_link_task, "device_link", 6144, NULL, 5, NULL);
    xTaskCreate(pairing_task, "pair_udp", 4096, NULL, 4, NULL);
}

bool device_link_is_connected(void)
{
    return s_connected;
}

void device_link_request_reconnect(void)
{
    s_reconnect_requested = true;
}

bool device_link_send_key(const char *client_id, const char *session_id,
                          const char *word)
{
    if (!s_connected || !s_key_q) return false;
    key_tx_t tx = {0};
    copy_string(tx.client_id, sizeof tx.client_id, client_id);
    copy_string(tx.session_id, sizeof tx.session_id, session_id);
    copy_string(tx.word, sizeof tx.word, word);
    return xQueueSend(s_key_q, &tx, 0) == pdTRUE;
}
