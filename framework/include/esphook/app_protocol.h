#pragma once
#include "app.h"
typedef enum { APP_OP_LIST, APP_OP_STATUS, APP_OP_OPEN, APP_OP_MENU, APP_OP_STANDBY, APP_OP_MESSAGE } app_op_t;
typedef struct {
    int seq;
    app_op_t op;
    char app[APP_ID_MAX + 1], message[APP_MESSAGE_MAX + 1];
} app_request_t;
bool app_protocol_parse(const char *json, size_t length, app_request_t *out);
