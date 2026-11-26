#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"
typedef struct esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;
typedef void *esp_lcd_spi_bus_handle_t;
typedef struct { void *data; } esp_lcd_panel_io_event_data_t;
typedef bool (*esp_lcd_panel_io_color_trans_done_cb_t)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *);
typedef struct { gpio_num_t cs_gpio_num; gpio_num_t dc_gpio_num; int spi_mode; unsigned pclk_hz; size_t trans_queue_depth; esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done; void *user_ctx; int lcd_cmd_bits; int lcd_param_bits; } esp_lcd_panel_io_spi_config_t;
esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t, const esp_lcd_panel_io_spi_config_t *, esp_lcd_panel_io_handle_t *);
