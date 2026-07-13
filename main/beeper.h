/* main/beeper.h */
#pragma once
#include "esp_err.h"

typedef enum { BEEP_NOTIFY, BEEP_KEY, BEEP_ERROR } beep_kind_t;

esp_err_t beeper_init(void);
void beeper_beep(beep_kind_t kind);
