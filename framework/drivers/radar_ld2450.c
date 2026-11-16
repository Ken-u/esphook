#include "motion_ports.h"
#include "sdkconfig.h"
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>
#define RADAR_UART UART_NUM_1
static QueueHandle_t s_events;
static ld2450_parser_t s_parser;
static ld2450_frame_t s_frame;
static uint64_t s_last_byte;
esp_err_t radar_port_init(int rx, int tx, unsigned baud)
{
    uart_config_t uart = {.baud_rate = baud, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT};
    esp_err_t err = uart_param_config(RADAR_UART, &uart);
    if (err == ESP_OK) err = uart_set_pin(RADAR_UART, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    memset(&s_parser, 0, sizeof s_parser); memset(&s_frame, 0, sizeof s_frame);
    return uart_driver_install(RADAR_UART, 2048, 0, 16, &s_events, 0);
}
void radar_port_close(void) { (void)uart_driver_delete(RADAR_UART); s_events = NULL; }
const ld2450_frame_t *radar_port_frame(void) { return &s_frame; }
const ld2450_parser_t *radar_port_parser(void) { return &s_parser; }
void radar_port_poll(void (*receive)(const ld2450_frame_t *, uint64_t, void *), void *user)
{
    uart_event_t event;
    if (!xQueueReceive(s_events, &event, pdMS_TO_TICKS(20))) return;
    if (event.type == UART_DATA) {
        uint8_t bytes[128]; size_t left = event.size;
        while (left) {
            int n = uart_read_bytes(RADAR_UART, bytes, left < sizeof bytes ? left : sizeof bytes, 0);
            if (n <= 0) break;
            uint64_t ms = (uint64_t)esp_timer_get_time() / 1000;
            if (s_last_byte && ms - s_last_byte > 250) s_parser.used = 0;
            s_last_byte = ms; left -= n;
            for (int i = 0; i < n; ++i) {
                ld2450_frame_t frame;
                if (ld2450_feed(&s_parser, bytes[i], &frame)) { s_frame = frame; receive(&frame, ms, user); }
            }
        }
    } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
               event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR) {
        uart_flush_input(RADAR_UART); xQueueReset(s_events); s_parser.used = 0; ++s_parser.rejected;
    }
}
#endif
