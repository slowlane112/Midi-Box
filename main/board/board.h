/*
 * board.h - MIDI Box v2 pin map (ESP32-S3 WROOM-1 N16R8).
 * Same wiring as v1.
 */
#pragma once
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

/* SPI3: main display (ST7789 320x240) + SD card share this bus */
#define BOARD_SPI3_HOST        SPI3_HOST
#define BOARD_SPI3_SCLK        GPIO_NUM_15
#define BOARD_SPI3_MOSI        GPIO_NUM_6
#define BOARD_SPI3_MISO        GPIO_NUM_5
#define BOARD_LCD1_DC          GPIO_NUM_21
#define BOARD_LCD1_RST         GPIO_NUM_13
#define BOARD_LCD1_CS          GPIO_NUM_12
#define BOARD_LCD1_HZ          (80 * 1000 * 1000)
#define BOARD_SD_CS            GPIO_NUM_1
#define BOARD_SD_KHZ           20000

/* Backlight (P-channel MOSFET: duty 255 = off, 0 = full) */
#define BOARD_BACKLIGHT        GPIO_NUM_10

/* SPI2: bar display (NV3007 428x142) */
#define BOARD_SPI2_HOST        SPI2_HOST
#define BOARD_LCD2_SCLK        GPIO_NUM_8
#define BOARD_LCD2_MOSI        GPIO_NUM_7
#define BOARD_LCD2_DC          GPIO_NUM_18
#define BOARD_LCD2_RST         GPIO_NUM_14
#define BOARD_LCD2_HZ          (80 * 1000 * 1000)

/* MIDI */
#define BOARD_SYNTH_UART       UART_NUM_1      /* to SAM2695 */
#define BOARD_SYNTH_TX         GPIO_NUM_17
#define BOARD_DIN_UART         UART_NUM_2      /* MIDI in */
#define BOARD_DIN1_RX          GPIO_NUM_9      /* back socket */
#define BOARD_DIN2_RX          GPIO_NUM_11     /* front socket */

/* Buttons, active low. Index 0 = button 1 (Shift) ... index 12 = button 13 */
#define BOARD_BUTTON_COUNT     13
#define BOARD_BUTTON_PINS { \
    GPIO_NUM_0,  GPIO_NUM_2,  GPIO_NUM_4,  GPIO_NUM_41, GPIO_NUM_40, \
    GPIO_NUM_39, GPIO_NUM_38, GPIO_NUM_3,  GPIO_NUM_44, GPIO_NUM_43, \
    GPIO_NUM_48, GPIO_NUM_42, GPIO_NUM_16 }

/* Display sizes */
#define LCD1_W 320
#define LCD1_H 240
#define LCD2_W 428
#define LCD2_H 142
