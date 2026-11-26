/*
 * lcd.h - both displays and the backlight.
 *
 * Framebuffers hold RGB565 with bytes swapped (panel byte order), see gfx.h RGB().
 * lcd1 (ST7789) shares SPI3 with the SD card: lcd1_flush() takes the bus lock
 * and the DMA-done interrupt releases it, so the SD card never talks mid-frame.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void lcd_init(void);

uint16_t *lcd1_framebuffer(void);
uint16_t *lcd2_framebuffer(void);

/* Wait until lcd1 is free to draw into (previous DMA finished). Takes the SPI3 bus lock. */
void lcd1_begin_frame(void);
/* Start DMA of the framebuffer. The bus lock is released by the DMA-done ISR. */
void lcd1_flush(void);

/* lcd2 has its own bus. Blocking, ~12 ms at 80 MHz. */
void lcd2_flush(void);

/* Call first thing in app_main: holds the backlight off through start-up. */
void backlight_init(void);
/* 0 = off ... 10 = full */
void backlight_set(uint8_t level);
/* Block until the last lcd1 frame has finished sending. */
void lcd1_wait_idle(void);
