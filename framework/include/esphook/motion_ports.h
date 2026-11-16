#pragma once
#include "esp_err.h"
#include "ld2450.h"
/* Board ports. Applications must use app_services.h instead. */
esp_err_t servo_port_init(int gpio, unsigned hz);
esp_err_t servo_port_write(int pulse_us);
esp_err_t radar_port_init(int rx, int tx, unsigned baud);
void radar_port_close(void);
void radar_port_poll(void (*receive)(const ld2450_frame_t *, uint64_t, void *), void *user);
const ld2450_frame_t *radar_port_frame(void);
const ld2450_parser_t *radar_port_parser(void);
