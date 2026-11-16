#pragma once
#include "FreeRTOS.h"
#include <stddef.h>
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
int xQueueSend(QueueHandle_t queue, const void *value, unsigned timeout);
int xQueueReceive(QueueHandle_t queue, void *value, unsigned timeout);
int xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
