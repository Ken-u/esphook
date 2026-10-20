#pragma once
#include "esp_err.h"

/* GPIO20 drives an external active-high switch, never the target RESET pin. */
#define BOARD_RESET_GPIO 20
#define BOARD_RESET_PULSE_MS 200
#define BOARD_RESET_COOLDOWN_MS 3000

esp_err_t board_reset_init(void);
/* Returns only after releasing the switch. Busy/cooldown requests are rejected. */
esp_err_t board_reset_press(void);
