/* main/http_server.c
 * 路由：POST /notify, POST /dismiss, POST /config, GET /, GET /health
 * /notify 解析 JSON，记 callback（按键回传用），推 EVT_NOTIFY 到 display_q + BEEP_NOTIFY。
 * /dismiss 推 EVT_DISMISS。
 * /config 解析 form-urlencoded（k0/k1/k2 + display 文本）。
 */
#include "http_server.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "config_store.h"
#include "net_wifi.h"
#include "events.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static const char *TAG = "http";

static const char *WEB_PAGE =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>aihook</title></head><body>"
"<h2>Keymap</h2>"
"<form method='post' action='/config'>"
"K1:<input name='k0' maxlength='15'><br>"
"K2:<input name='k1' maxlength='15'><br>"
"K3:<input name='k2' maxlength='15'><br>"
"<h2>Display names</h2>"
"<textarea name='display' rows='5' cols='40' "
"placeholder='type:id=name per line&#10;client:espdev:claude=Desktop&#10;session:abc=NES'></textarea><br>"
"<button type='submit'>Save</button></form>"
"</body></html>";

/* urldecode 极简：处理 %xx 和 + */
static void urldecode(char *s)
{
    char *d = s;
    while (*s) {
        if (*s == '+') { *d++ = ' '; s++; }
        else if (*s == '%' && s[1] && s[2]) {
            int hi = (s[1] >= 'A') ? (s[1] | 0x20) - 'a' + 10 : s[1] - '0';
            int lo = (s[2] >= 'A') ? (s[2] | 0x20) - 'a' + 10 : s[2] - '0';
            *d++ = (char)((hi << 4) | lo);
            s += 3;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0';
}

/* 从 buf 找 "key=" 的 value（到 & 或 \0），写入 out。返回是否找到。 */
static bool form_field(const char *buf, const char *key, char *out, size_t outlen)
{
    size_t klen = strlen(key);
    const char *p = buf;
    while ((p = strstr(p, key))) {
        /* 确保是字段起点（前一个是 & 或开头） */
        if (p == buf || p[-1] == '&') {
            p += klen;
            size_t n = 0;
            while (*p && *p != '&' && n < outlen - 1) out[n++] = *p++;
            out[n] = '\0';
            urldecode(out);
            return true;
        }
        p += klen;
    }
    out[0] = '\0';
    return false;
}

static esp_err_t h_notify(httpd_req_t *req)
{
    char buf[512];
    int len = req->content_len < (int)sizeof buf ? req->content_len : (int)sizeof buf - 1;
    int got = httpd_req_recv(req, buf, len);
    if (got <= 0) { httpd_resp_send_err(req, 400, "{\"ok\":false}"); return ESP_OK; }
    buf[got] = '\0';

    cJSON *j = cJSON_Parse(buf);
    if (!j) { httpd_resp_send_err(req, 400, "{\"ok\":false,\"err\":\"bad json\"}"); return ESP_OK; }
    cJSON *c = cJSON_GetObjectItem(j, "client_id");
    cJSON *s = cJSON_GetObjectItem(j, "session_id");
    cJSON *t = cJSON_GetObjectItem(j, "title");
    cJSON *b = cJSON_GetObjectItem(j, "body");
    if (!cJSON_IsString(c) || !cJSON_IsString(s)) {
        cJSON_Delete(j);
        httpd_resp_send_err(req, 400, "{\"ok\":false,\"err\":\"need client_id,session_id\"}");
        return ESP_OK;
    }
    display_event_t e = {0};
    e.kind = EVT_NOTIFY;
    strncpy(e.client_id, c->valuestring, sizeof e.client_id - 1);
    strncpy(e.session_id, s->valuestring, sizeof e.session_id - 1);
    if (cJSON_IsString(t)) strncpy(e.title, t->valuestring, sizeof e.title - 1);
    if (cJSON_IsString(b)) strncpy(e.body, b->valuestring, sizeof e.body - 1);

    /* callback：JSON 里的 callback 字段，或取自 TCP 对端 IP + 默认端口 8765 */
    cJSON *cb = cJSON_GetObjectItem(j, "callback");
    if (cJSON_IsString(cb) && cb->valuestring[0]) {
        strncpy(e.callback, cb->valuestring, sizeof e.callback - 1);
    } else {
        /* 从 socket 取对端 IP */
        int fd = httpd_req_to_sockfd(req);
        struct sockaddr_in addr;
        socklen_t alen = sizeof addr;
        if (getpeername(fd, (struct sockaddr *)&addr, &alen) == 0) {
            snprintf(e.callback, sizeof e.callback, "%s:8765", inet_ntoa(addr.sin_addr));
        }
    }
    set_last_callback(e.callback);
    cJSON_Delete(j);

    xQueueSend(display_q, &e, 0);
    beep_kind_t bk = BEEP_NOTIFY;
    xQueueSend(beeper_q, &bk, 0);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t h_dismiss(httpd_req_t *req)
{
    char buf[256];
    int len = req->content_len < (int)sizeof buf ? req->content_len : (int)sizeof buf - 1;
    int got = httpd_req_recv(req, buf, len);
    if (got <= 0) { httpd_resp_send_err(req, 400, "{\"ok\":false}"); return ESP_OK; }
    buf[got] = '\0';
    cJSON *j = cJSON_Parse(buf);
    if (!j) { httpd_resp_send_err(req, 400, "{\"ok\":false}"); return ESP_OK; }
    cJSON *c = cJSON_GetObjectItem(j, "client_id");
    cJSON *s = cJSON_GetObjectItem(j, "session_id");
    if (cJSON_IsString(c) && cJSON_IsString(s)) {
        display_event_t e = {0};
        e.kind = EVT_DISMISS;
        strncpy(e.client_id, c->valuestring, sizeof e.client_id - 1);
        strncpy(e.session_id, s->valuestring, sizeof e.session_id - 1);
        xQueueSend(display_q, &e, 0);
    }
    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t h_config(httpd_req_t *req)
{
    char buf[1024];
    int len = req->content_len < (int)sizeof buf ? req->content_len : (int)sizeof buf - 1;
    int got = httpd_req_recv(req, buf, len);
    if (got <= 0) { httpd_resp_send_err(req, 400, "{\"ok\":false}"); return ESP_OK; }
    buf[got] = '\0';

    /* 按键词 */
    const char *keys[3] = {0};
    static char kbuf[3][16];
    for (int i = 0; i < 3; i++) {
        char tag[4];
        snprintf(tag, sizeof tag, "k%d=", i);
        if (form_field(buf, tag, kbuf[i], sizeof kbuf[0]) && kbuf[i][0]) {
            keys[i] = kbuf[i];
        }
    }
    config_set_keymap(keys);

    /* display 名：解析 "type:id=name" 每行 */
    char disp[512];
    if (form_field(buf, "display=", disp, sizeof disp)) {
        char *line = strtok(disp, "\r\n");
        while (line) {
            /* 格式 type:id=name */
            char *eq = strchr(line, '=');
            if (!eq) { line = strtok(NULL, "\r\n"); continue; }
            *eq = '\0';
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                config_set_display_name(line, colon + 1, eq + 1);
            }
            line = strtok(NULL, "\r\n");
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, WEB_PAGE);
    return ESP_OK;
}

static esp_err_t h_health(httpd_req_t *req)
{
    char buf[80];
    snprintf(buf, sizeof buf, "{\"ok\":true,\"ip\":\"%s\"}", wifi_get_ip());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t http_start(void)
{
    httpd_handle_t s = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&s, &cfg) != ESP_OK) return ESP_FAIL;
    static const httpd_uri_t u_notify = {.uri="/notify",.method=HTTP_POST,.handler=h_notify};
    static const httpd_uri_t u_dismiss= {.uri="/dismiss",.method=HTTP_POST,.handler=h_dismiss};
    static const httpd_uri_t u_config = {.uri="/config",.method=HTTP_POST,.handler=h_config};
    static const httpd_uri_t u_root   = {.uri="/",.method=HTTP_GET,.handler=h_root};
    static const httpd_uri_t u_health = {.uri="/health",.method=HTTP_GET,.handler=h_health};
    httpd_register_uri_handler(s, &u_notify);
    httpd_register_uri_handler(s, &u_dismiss);
    httpd_register_uri_handler(s, &u_config);
    httpd_register_uri_handler(s, &u_root);
    httpd_register_uri_handler(s, &u_health);
    ESP_LOGI(TAG, "http server started");
    return ESP_OK;
}
