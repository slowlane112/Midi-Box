#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef enum { UART_NUM_0, UART_NUM_1, UART_NUM_2 } uart_port_t;
#define UART_PIN_NO_CHANGE (-1)
typedef enum { UART_DATA_8_BITS = 3 } uart_word_length_t;
typedef enum { UART_PARITY_DISABLE } uart_parity_t;
typedef enum { UART_STOP_BITS_1 = 1 } uart_stop_bits_t;
typedef enum { UART_HW_FLOWCTRL_DISABLE } uart_hw_flowcontrol_t;
typedef enum { UART_SCLK_DEFAULT } uart_sclk_t;
typedef struct { int baud_rate; uart_word_length_t data_bits; uart_parity_t parity; uart_stop_bits_t stop_bits; uart_hw_flowcontrol_t flow_ctrl; uart_sclk_t source_clk; } uart_config_t;
esp_err_t uart_driver_install(uart_port_t, int, int, int, void *, int);
esp_err_t uart_param_config(uart_port_t, const uart_config_t *);
esp_err_t uart_set_pin(uart_port_t, int, int, int, int);
int uart_write_bytes(uart_port_t, const void *, size_t);
int uart_read_bytes(uart_port_t, void *, uint32_t, uint32_t);
esp_err_t uart_flush_input(uart_port_t);
