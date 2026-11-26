#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef enum { GPIO_NUM_NC=-1, GPIO_NUM_0=0, GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_20, GPIO_NUM_21, GPIO_NUM_38=38, GPIO_NUM_39, GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42, GPIO_NUM_43, GPIO_NUM_44, GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_48 } gpio_num_t;
typedef enum { GPIO_MODE_INPUT=1, GPIO_MODE_OUTPUT=2 } gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE, GPIO_PULLUP_ENABLE } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE, GPIO_PULLDOWN_ENABLE } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE } gpio_int_type_t;
typedef enum { GPIO_PULLUP_ONLY } gpio_pull_mode_t;
typedef struct { uint64_t pin_bit_mask; gpio_mode_t mode; gpio_pullup_t pull_up_en; gpio_pulldown_t pull_down_en; gpio_int_type_t intr_type; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_set_level(gpio_num_t, uint32_t);
int gpio_get_level(gpio_num_t);
esp_err_t gpio_set_pull_mode(gpio_num_t, gpio_pull_mode_t);
