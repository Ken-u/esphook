/* main/events.h */
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "beeper.h"

typedef enum { EVT_NOTIFY, EVT_DISMISS, EVT_NET_STATE } display_evt_kind_t;

typedef struct {
    display_evt_kind_t kind;
    char client_id[24];
    char session_id[48];
    char title[32];
    char body[128];
    int  net_state;   /* 0=disc 1=connecting 2=connected */
    char ip[16];
    char callback[32]; /* host:port，/notify 携带，供按键回传 */
} display_event_t;

extern QueueHandle_t display_q;
extern QueueHandle_t beeper_q;

/* 最近一次 /notify 的 callback 地址（按键回传用）。线程安全读写。 */
const char *last_callback(void);
void set_last_callback(const char *cb);
