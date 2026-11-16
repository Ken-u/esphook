#pragma once
#include <stdbool.h>
#include <stdint.h>
#define APP_BUTTON_GPIO 6
#define APP_DEBOUNCE_MS 25U
#define APP_CONFIRM_MS 500U
#define APP_TRIPLE_MS 800U
#define APP_MENU_IDLE_MS 10000U

enum { INPUT_DOWN = 1, INPUT_UP = 2, INPUT_SHORT = 4, INPUT_CONFIRM = 8, INPUT_MENU = 16 };
typedef struct {
    bool down, confirmed, blocked;
    uint64_t down_ms, first_ms;
    unsigned clicks;
} app_input_t;
unsigned app_input_edge(app_input_t *input, bool pressed, uint64_t now_ms);
unsigned app_input_tick(app_input_t *input, uint64_t now_ms);
unsigned app_input_progress(const app_input_t *input, uint64_t now_ms);
/* Consume a confirmation's release when it launches another application. */
void app_input_consume(app_input_t *input);

/* IRQ-driven debounce shared by every app. A boot-held key must be released. */
typedef struct {
    bool initialized, raw_pressed, stable_pressed, may_press;
    uint64_t changed_ms;
} app_button_t;
bool app_button_sample(app_button_t *button, bool pressed, uint64_t now_ms);
/* Restart the quiet window even after coalesced IRQs return to the same level. */
void app_button_edge(app_button_t *button, bool pressed, uint64_t now_ms);
