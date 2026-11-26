#pragma once
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *, BaseType_t);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *, TickType_t);
