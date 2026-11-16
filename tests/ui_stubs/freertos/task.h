#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
int xTaskCreate(void (*function)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *handle);
void vTaskDelay(unsigned ticks);
