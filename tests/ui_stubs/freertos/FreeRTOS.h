#pragma once
/* Sequential state tests only; concurrency is provided by ESP-IDF in firmware. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((unsigned)(ms))
#define portMAX_DELAY 0xffffffffU
