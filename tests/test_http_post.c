/* Host test for the string half of the Hook key-reply POST (main/http_post.c). */
#include "http_post.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void target(void)
{
    char host[HTTP_POST_HOST_MAX], port[HTTP_POST_PORT_MAX];
    assert(http_post_target("192.168.3.9:8765", host, sizeof host, port, sizeof port));
    assert(!strcmp(host, "192.168.3.9") && !strcmp(port, "8765"));
    assert(http_post_target("my-mac.local", host, sizeof host, port, sizeof port));
    assert(!strcmp(host, "my-mac.local") && !strcmp(port, "80"));
    assert(http_post_target("h:65535", host, sizeof host, port, sizeof port) && !strcmp(port, "65535"));
    const char *bad[] = {"", ":8765", "host:", "host:0", "host:65536", "host:99999", "host:12a", "host:1:2", "::1", "[::1]:80",
                         "ho st:80", "host/path:80", "user@host:80", "host:80/x", "host\r\nX: y:80", "host?x:80", "host#:80", "ho\\st:80",
                         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:80"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        assert(!http_post_target(bad[i], host, sizeof host, port, sizeof port));
    assert(!http_post_target(NULL, host, sizeof host, port, sizeof port));
    assert(!http_post_target("h:80", host, 1, port, sizeof port));
    assert(!http_post_target("h:80", host, sizeof host, port, 2));
}

static void request(void)
{
    char out[256];
    const int n = http_post_request(out, sizeof out, "192.168.3.9:8765", "/keyevent", "{\"word\":\"yes\"}");
    assert(n > 0 && (size_t)n == strlen(out));
    assert(!strcmp(out,
                   "POST /keyevent HTTP/1.1\r\nHost: 192.168.3.9:8765\r\nContent-Type: application/json\r\n"
                   "Content-Length: 14\r\nConnection: close\r\n\r\n{\"word\":\"yes\"}"));
    /* the port is spelled out even when the callback named none, and it is the same normalised number */
    assert(http_post_request(out, sizeof out, "pc.local", "/keyevent", "{}") > 0 && strstr(out, "Host: pc.local:80\r\n"));
    assert(strstr(out, "Content-Length: 2\r\n") && !strcmp(strstr(out, "\r\n\r\n") + 4, "{}"));
    /* it never overruns: one byte too small fails instead of truncating */
    char small[256];
    const int need = http_post_request(small, sizeof small, "h:1", "/k", "{}");
    assert(need > 0);
    for (int size = 1; size <= need; ++size) assert(http_post_request(small, (size_t)size, "h:1", "/k", "{}") == -1);
    assert(http_post_request(small, (size_t)need + 1, "h:1", "/k", "{}") == need);
    assert(http_post_request(out, sizeof out, "h:1", "keyevent", "{}") == -1);        /* the path starts with a slash */
    assert(http_post_request(out, sizeof out, "h:1", "/key event", "{}") == -1);
    assert(http_post_request(out, sizeof out, "h:1", "/k\r\nX: y", "{}") == -1);
    assert(http_post_request(out, sizeof out, "h /x:1", "/k", "{}") == -1);
    assert(http_post_request(out, sizeof out, "h:1", "/k", NULL) == -1);
    assert(http_post_request(NULL, sizeof out, "h:1", "/k", "{}") == -1);
    assert(http_post_request(out, 0, "h:1", "/k", "{}") == -1);
}

static void status(void)
{
    const char *ok = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    assert(http_post_status(ok, strlen(ok)) == 200);
    assert(http_post_status("HTTP/1.0 204 No Content", 23) == 204);
    assert(http_post_status("HTTP/1.1 404 Not Found", 22) == 404);
    assert(http_post_status("HTTP/1.1 500", 12) == 500);
    assert(http_post_status("HTTP/1.1 20", 11) == -1);                  /* cut short */
    assert(http_post_status("HTTP/2 200 OK", 13) == -1);
    assert(http_post_status("HTTP/1.1 2x0 OK", 15) == -1);
    assert(http_post_status("HTTP/1.1  200", 13) == -1);
    assert(http_post_status("HTTP/1.1 099 x", 14) == -1);               /* below 100 is not a status */
    assert(http_post_status("garbage garbage", 15) == -1);
    assert(http_post_status("", 0) == -1);
    assert(http_post_status(NULL, 0) == -1);
}

int main(void)
{
    target();
    request();
    status();
    puts("http post tests passed");
    return 0;
}
