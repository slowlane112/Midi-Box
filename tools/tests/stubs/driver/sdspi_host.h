#pragma once
#include "driver/spi_master.h"
typedef struct { int slot; int max_freq_khz; } sdmmc_host_t;
#define SDSPI_HOST_DEFAULT() ((sdmmc_host_t){0, 20000})
typedef struct { int host_id; gpio_num_t gpio_cs; } sdspi_device_config_t;
#define SDSPI_DEVICE_CONFIG_DEFAULT() ((sdspi_device_config_t){0, GPIO_NUM_NC})
