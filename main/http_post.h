#pragma once
/* The one outgoing HTTP request this device makes: a small JSON POST to the "host:port" a Hook callback names, on the
 * local network, plain HTTP/1.1 with no redirects and no TLS. It used to go through esp_http_client, which drags in
 * esp-tls and the whole mbedTLS TLS stack (about 60 KB of flash) for a request that never uses them. This file is the
 * string work only, so it runs on the host; the socket part is in framework/ports/esp32/app_services.c. */
#include <stdbool.h>
#include <stddef.h>

#define HTTP_POST_HOST_MAX 32
#define HTTP_POST_PORT_MAX 6

/* Splits "host" or "host:port" (port 80 when absent). Rejects an empty host, a port outside 1..65535 and anything
 * that could break the request line (spaces, control characters, '/', '?', '#', '@'). */
bool http_post_target(const char *host_port, char *host, size_t host_size, char *port, size_t port_size);
/* Builds "POST <path> HTTP/1.1" with Host, Content-Type: application/json, Content-Length and Connection: close.
 * Returns the length, or -1 when it does not fit or `path` does not start with '/'. */
int http_post_request(char *out, size_t size, const char *host_port, const char *path, const char *json_body);
/* The status code of "HTTP/1.x NNN ..." at the start of `response`, or -1 when it is not a status line. */
int http_post_status(const char *response, size_t length);
