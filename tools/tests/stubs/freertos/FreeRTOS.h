#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint32_t TickType_t; typedef int BaseType_t; typedef unsigned UBaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define portTICK_PERIOD_MS 1
typedef struct { int x; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(m) (void)(m)
#define portEXIT_CRITICAL(m) (void)(m)
