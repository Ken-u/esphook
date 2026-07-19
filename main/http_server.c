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

static const char *WEB_CSS =
":root{--bg:#0a0e14;--panel:#141a23;--line:#1f2733;--txt:#c5d0de;--dim:#5c6a7a;"
"--acc:#00d9a3;--acc2:#00a3ff;--red:#ff4757;--y:#ffb800}"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:var(--bg);color:var(--txt);font:14px/1.5 'SF Mono',Consolas,Menlo,monospace;"
"padding:16px;max-width:520px;margin:0 auto}"
"h1{font-size:18px;color:var(--acc);margin-bottom:16px;letter-spacing:1px}"
"h1:before{content:'> ';color:var(--dim)}"
".card{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:14px;margin-bottom:14px}"
".card h2{font-size:13px;color:var(--acc2);margin-bottom:12px;text-transform:uppercase;letter-spacing:1px}"
"label{display:flex;align-items:center;gap:10px;margin-bottom:8px}"
"label span{color:var(--dim);width:28px;flex-shrink:0}"
"input[type=text],textarea{flex:1;background:var(--bg);border:1px solid var(--line);color:var(--txt);"
"border-radius:4px;padding:7px 9px;font:inherit;outline:none;min-width:0}"
"input:focus,textarea:focus{border-color:var(--acc)}"
"textarea{resize:vertical;min-height:70px;font-size:12px}"
"button{background:var(--acc);color:var(--bg);border:0;border-radius:4px;padding:9px 18px;"
"font:inherit;font-weight:bold;cursor:pointer;letter-spacing:1px;width:100%}"
"button:hover{background:#00f0b5}"
".hint{color:var(--dim);font-size:11px;margin-top:6px}"
"code{color:var(--y)}"
".row{display:flex;align-items:center;gap:8px;margin-bottom:7px}"
".row .id{color:var(--dim);font-size:11px;width:38%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
".row input{flex:1}";

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

    /* 记已见 client/session（持久化，Web 页用） */
    config_seen_add("client", e.client_id);
    config_seen_add("session", e.session_id);

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

    /* 解析形如 c:<id>=<name> 和 s:<id>=<name> 的字段（Web 页新格式）。
       字段名以 c: 或 s: 开头。逐字段扫描，含末尾无 & 的最后一个字段。 */
    const char *p = buf;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        const char *eq = strchr(p, '=');
        if (eq && (!amp || eq < amp)) {
            size_t namelen = eq - p;
            if (namelen > 2 && p[1] == ':' && (p[0] == 'c' || p[0] == 's')) {
                char id[64] = {0};
                size_t idlen = namelen - 2;
                if (idlen >= sizeof id) idlen = sizeof id - 1;
                memcpy(id, p + 2, idlen); id[idlen] = '\0';
                char val[32] = {0};
                const char *vstart = eq + 1;
                size_t vallen = amp ? (size_t)(amp - vstart) : strlen(vstart);
                if (vallen >= sizeof val) vallen = sizeof val - 1;
                memcpy(val, vstart, vallen); val[vallen] = '\0';
                urldecode(id);
                urldecode(val);
                const char *type = (p[0] == 'c') ? "client" : "session";
                if (val[0]) config_set_display_name(type, id, val);
            }
        }
        p = amp ? amp + 1 : NULL;
    }

    /* 返回带自动重定向的 HTML，避免白屏，刷新回配置页 */
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta http-equiv='refresh' content='0;url=/'>"
        "<title>saved</title></head><body style='background:#0a0e14;color:#00d9a3;"
        "font:14px monospace;padding:20px'>saved, redirecting...</body></html>");
    return ESP_OK;
}

/* 动态生成 Web 页：含已见 client/session 列表，每行带 id + 显示名输入框。 */
static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    /* 逐块发，避免单次缓冲过大 */
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>aihook</title><style>");
    httpd_resp_sendstr_chunk(req, WEB_CSS);
    httpd_resp_sendstr_chunk(req, "</style></head><body>"
        "<h1>aihook config</h1>"
        "<form method='post' action='/config'>"
        "<div class='card'><h2>Keymap</h2>"
        "<label><span>K1</span><input type=text name='k0' maxlength='15'></label>"
        "<label><span>K2</span><input type=text name='k1' maxlength='15'></label>"
        "<label><span>K3</span><input type=text name='k2' maxlength='15'></label>"
        "<div class='hint'>ASCII only. e.g. <code>ok continue done</code></div>"
        "</div>");

    /* client 列表卡片 */
    httpd_resp_sendstr_chunk(req, "<div class='card'><h2>Clients</h2>");
    int cn = config_seen_count("client");
    if (cn == 0) {
        httpd_resp_sendstr_chunk(req, "<div class='hint'>none yet (send a notify first)</div>");
    }
    for (int i = 0; i < cn; i++) {
        char id[64] = {0};
        config_seen_at("client", i, id, sizeof id);
        char name[32] = {0};
        config_get_display_name("client", id, name, sizeof name);
        char row[320];
        snprintf(row, sizeof(row),
            "<div class='row'><span class='id'>%.30s</span>"
            "<input name='c:%.50s' value='%.30s'></div>",
            id, id, name);
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</div>");

    /* session 列表卡片 */
    httpd_resp_sendstr_chunk(req, "<div class='card'><h2>Sessions</h2>");
    int sn = config_seen_count("session");
    if (sn == 0) {
        httpd_resp_sendstr_chunk(req, "<div class='hint'>none yet</div>");
    }
    for (int i = 0; i < sn; i++) {
        char id[64] = {0};
        config_seen_at("session", i, id, sizeof id);
        char name[32] = {0};
        config_get_display_name("session", id, name, sizeof name);
        char row[320];
        snprintf(row, sizeof(row),
            "<div class='row'><span class='id'>%.30s</span>"
            "<input name='s:%.50s' value='%.30s'></div>",
            id, id, name);
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</div>");

    httpd_resp_sendstr_chunk(req,
        "<button type='submit'>SAVE</button>"
        "</form></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);  /* 结束 */
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
