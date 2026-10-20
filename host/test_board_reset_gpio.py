"""Compile the production GPIO controller against a deterministic hardware shim."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
    "esp_err.h": """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
""",
    "driver/gpio.h": """#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_INTR_DISABLE 0
esp_err_t gpio_set_level(int pin, int level);
esp_err_t gpio_config(const gpio_config_t *config);
""",
    "esp_timer.h": """#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
""",
    "freertos/FreeRTOS.h": """#pragma once
#include <stddef.h>
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((ms) / 10)
""",
    "freertos/semphr.h": """#pragma once
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t lock, int wait);
int xSemaphoreGive(SemaphoreHandle_t lock);
""",
    "freertos/task.h": """#pragma once
void vTaskDelay(unsigned ticks);
""",
}
HARNESS = r"""
#include <assert.h>
#include <stdio.h>
#include "board_reset.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
static int locked, configured, level = -1, high_count, low_count, fail_high, fail_low;
static int64_t now;
esp_err_t gpio_set_level(int pin, int value) {
    assert(pin == 20);
    if (value) { assert(configured); high_count++; if (fail_high) return ESP_FAIL; }
    else { low_count++; if (fail_low) return ESP_FAIL; }
    level = value;
    return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t *c) {
    assert(level == 0); /* Never enable the output with a high latch. */
    assert(c->pin_bit_mask == (1ULL << 20));
    assert(c->mode == GPIO_MODE_OUTPUT);
    assert(c->pull_up_en == GPIO_PULLUP_DISABLE);
    assert(c->pull_down_en == GPIO_PULLDOWN_ENABLE);
    configured = 1;
    return ESP_OK;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &locked; }
int xSemaphoreTake(SemaphoreHandle_t lock, int wait) {
    (void)lock; assert(wait == 0);
    if (locked) return 0;
    locked = 1; return 1;
}
int xSemaphoreGive(SemaphoreHandle_t lock) { (void)lock; locked = 0; return 1; }
int64_t esp_timer_get_time(void) { return now; }
void vTaskDelay(unsigned ticks) {
    assert(level == 1);
    assert(ticks * 10 >= 200 && ticks * 10 <= 220);
    assert(board_reset_press() == ESP_ERR_INVALID_STATE); /* Concurrent call cannot hold/extend pulse. */
    now += ticks * 10000;
}
int main(void) {
    assert(board_reset_press() == ESP_ERR_INVALID_STATE);
    assert(high_count == 0);
    assert(board_reset_init() == ESP_OK);
    assert(level == 0 && high_count == 0);
    assert(board_reset_press() == ESP_OK);
    assert(level == 0 && high_count == 1 && !locked);
    assert(board_reset_press() == ESP_ERR_INVALID_STATE);
    now += 2999999;
    assert(board_reset_press() == ESP_ERR_INVALID_STATE);
    assert(high_count == 1);
    now++;
    assert(board_reset_press() == ESP_OK);
    assert(level == 0 && high_count == 2);
    now += 3000000;
    fail_high = 1;
    int previous_low = low_count;
    assert(board_reset_press() == ESP_FAIL);
    assert(low_count == previous_low + 1 && level == 0 && !locked);
    fail_high = 0;
    now += 3000000;
    fail_low = 1;
    assert(board_reset_press() == ESP_FAIL); /* Never ACK success if release failed. */
    assert(!locked);
    puts("GPIO reset: startup, pulse duration, busy, cooldown, error release checks passed");
}
"""

with tempfile.TemporaryDirectory() as directory:
    build = Path(directory)
    for name, content in HEADERS.items():
        path = build / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    (build / "harness.c").write_text(HARNESS)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(build), "-I" + str(ROOT / "main"),
                    str(ROOT / "main/board_reset.c"), str(build / "harness.c"),
                    "-o", str(build / "test")], check=True)
    subprocess.run([str(build / "test")], check=True)
