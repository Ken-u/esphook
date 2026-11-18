#include "http_post.h"
#include <stdio.h>
#include <string.h>

bool http_post_target(const char *host_port, char *host, size_t host_size, char *port, size_t port_size)
{
    if (!host_port || !host || !port || host_size < 2 || port_size < 3) return false;
    const size_t length = strlen(host_port);
    if (length == 0) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)host_port[i];
        if (c <= ' ' || c == 0x7F || c == '/' || c == '?' || c == '#' || c == '@' || c == '\\') return false;
    }
    const char *colon = strchr(host_port, ':');
    if (colon && strchr(colon + 1, ':')) return false; /* IPv6 literals are not supported */
    const size_t host_length = colon ? (size_t)(colon - host_port) : length;
    if (host_length == 0 || host_length >= host_size) return false;
    unsigned long number = 80;
    if (colon) {
        const char *digits = colon + 1;
        if (*digits == '\0' || strlen(digits) > 5) return false;
        number = 0;
        for (; *digits; ++digits) {
            if (*digits < '0' || *digits > '9') return false;
            number = number * 10U + (unsigned long)(*digits - '0');
        }
        if (number == 0 || number > 65535UL) return false;
    }
    memcpy(host, host_port, host_length);
    host[host_length] = '\0';
    snprintf(port, port_size, "%lu", number);
    return true;
}

int http_post_request(char *out, size_t size, const char *host_port, const char *path, const char *json_body)
{
    if (!out || !size || !host_port || !path || !json_body || path[0] != '/') return -1;
    char host[HTTP_POST_HOST_MAX], port[HTTP_POST_PORT_MAX];
    if (!http_post_target(host_port, host, sizeof host, port, sizeof port)) return -1;
    for (const char *p = path; *p; ++p)
        if ((unsigned char)*p <= ' ' || *p == 0x7F) return -1;
    const int n = snprintf(out, size,
                           "POST %s HTTP/1.1\r\nHost: %s:%s\r\nContent-Type: application/json\r\nContent-Length: %u\r\n"
                           "Connection: close\r\n\r\n%s",
                           path, host, port, (unsigned)strlen(json_body), json_body);
    return n < 0 || (size_t)n >= size ? -1 : n;
}

int http_post_status(const char *response, size_t length)
{
    if (!response || length < 12 || strncmp(response, "HTTP/1.", 7) != 0 || (response[7] != '0' && response[7] != '1') ||
        response[8] != ' ')
        return -1;
    int code = 0;
    for (size_t i = 9; i < 12; ++i) {
        if (response[i] < '0' || response[i] > '9') return -1;
        code = code * 10 + (response[i] - '0');
    }
    return code >= 100 ? code : -1;
}
