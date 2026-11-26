#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"
typedef enum { SPI1_HOST, SPI2_HOST, SPI3_HOST } spi_host_device_t;
#define SPI_DMA_CH_AUTO 3
typedef struct { int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num; int max_transfer_sz; } spi_bus_config_t;
typedef struct { int clock_speed_hz; uint8_t mode; int spics_io_num; int queue_size; } spi_device_interface_config_t;
typedef struct { size_t length; const void *tx_buffer; void *user; } spi_transaction_t;
typedef struct spi_dev *spi_device_handle_t;
esp_err_t spi_bus_initialize(spi_host_device_t, const spi_bus_config_t *, int);
esp_err_t spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t *, spi_device_handle_t *);
esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t *);
esp_err_t spi_device_queue_trans(spi_device_handle_t, spi_transaction_t *, uint32_t);
esp_err_t spi_device_get_trans_result(spi_device_handle_t, spi_transaction_t **, uint32_t);
