#pragma once
#include "app.h"
#include "app_keys.h"
#include "app_hardware.h"
#include "app_time.h"
typedef enum { APP_OP_LIST, APP_OP_STATUS, APP_OP_OPEN, APP_OP_MENU, APP_OP_STANDBY, APP_OP_MESSAGE,
               APP_OP_KEY, APP_OP_KEYS_RELEASE, APP_OP_KEYMAP, APP_OP_HARDWARE, APP_OP_TIME } app_op_t;
typedef struct {
    int seq;
    app_op_t op;
    char app[APP_ID_MAX + 1], message[APP_MESSAGE_MAX + 1];
    unsigned key;
    bool pressed, keymap_set;
    app_keymap_t keymap;
    bool hardware_set, reboot;
    app_hardware_profile_t hardware_profile;
    bool time_set; /* op time: both epoch and tz_min were given (otherwise it only reads the clock) */
    uint32_t epoch;
    int tz_min;
} app_request_t;
bool app_protocol_parse(const char *json, size_t length, app_request_t *out);
