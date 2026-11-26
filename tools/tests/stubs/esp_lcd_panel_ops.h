#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef struct esp_lcd_panel_t *esp_lcd_panel_handle_t;
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void *);
esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t, bool);
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t, bool, bool);
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t, bool);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t, bool);
