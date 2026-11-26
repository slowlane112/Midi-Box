/*
 * lcd.c - ST7789 (esp_lcd, SPI3 shared with SD) + NV3007 (raw SPI2) + LEDC backlight.
 * Init sequences and orientation are the ones from MIDI Box v1.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/ledc.h"
#include "board.h"
#include "bus.h"
#include "lcd.h"

static const char *TAG = "lcd";

static uint16_t *s_fb1;
static uint16_t *s_fb2;
static esp_lcd_panel_handle_t s_panel1;
static spi_device_handle_t s_lcd2;

/* ------------------------------------------------------------------ backlight */

#define BL_TIMER   LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0

/* Gamma-ish curve; inverted because the backlight switch is a P-MOSFET.
 * With 8-bit resolution, 256 (not 255) is a constant high output = truly off. */
#define BL_OFF 256
static const uint16_t s_bl_duty[11] = { BL_OFF, 253, 249, 242, 231, 214, 190, 157, 115, 65, 0 };

void backlight_init(void)
{
    /* The gate pull-up keeps the backlight off from reset; drive it high ourselves before
     * LEDC takes over. Level first, then output, so the pin never glitches low. */
    gpio_set_level(BOARD_BACKLIGHT, 1);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(BOARD_BACKLIGHT, 1);

    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = BL_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = BOARD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = BL_OFF,  /* off until the first frame is on screen */
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void backlight_set(uint8_t level)
{
    if (level > 10) {
        level = 10;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, s_bl_duty[level]);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

/* ------------------------------------------------------------------ lcd1: ST7789 */

static bool lcd1_done_isr(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io; (void)edata; (void)ctx;
    return bus_unlock_from_isr();
}

static void lcd1_init(void)
{
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BOARD_LCD1_DC,
        .cs_gpio_num = BOARD_LCD1_CS,
        .pclk_hz = BOARD_LCD1_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 3,
        .trans_queue_depth = 4,
        .on_color_trans_done = lcd1_done_isr,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_SPI3_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = BOARD_LCD1_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &dev, &s_panel1));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel1));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel1));
    esp_lcd_panel_swap_xy(s_panel1, true);
    esp_lcd_panel_mirror(s_panel1, true, false);
    esp_lcd_panel_invert_color(s_panel1, true);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel1, true));
}

void lcd1_begin_frame(void)
{
    bus_lock();
}

void lcd1_wait_idle(void)
{
    bus_lock();     /* only free once the DMA-done interrupt has released it */
    bus_unlock();
}

void lcd1_flush(void)
{
    /* bus lock is held (lcd1_begin_frame); released in lcd1_done_isr */
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel1, 0, 0, LCD1_W, LCD1_H, s_fb1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd1 draw: %s", esp_err_to_name(err));
        bus_unlock();
    }
}

/* ------------------------------------------------------------------ lcd2: NV3007 */

#define NV_COL_OFFSET 0
#define NV_ROW_OFFSET 14
#define NV_CHUNK      (32 * 1024)
#define NV_QUEUE      4

static void nv_write(const uint8_t *data, size_t len, bool is_data)
{
    gpio_set_level(BOARD_LCD2_DC, is_data ? 1 : 0);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_lcd2, &t));
}

static void nv_cmd(uint8_t c) { nv_write(&c, 1, false); }

static void nv_data(const uint8_t *d, size_t n) { nv_write(d, n, true); }

/* command followed by up to 4 data bytes; n = number of data bytes */
static void nv_seq(uint8_t cmd, int n, uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t d[3] = { a, b, c };
    nv_cmd(cmd);
    if (n > 0) {
        nv_data(d, (size_t)n);
    }
}

static void nv_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 += NV_COL_OFFSET; x1 += NV_COL_OFFSET;
    y0 += NV_ROW_OFFSET; y1 += NV_ROW_OFFSET;
    uint8_t b[4];
    nv_cmd(0x2A);
    b[0] = x0 >> 8; b[1] = x0 & 0xFF; b[2] = x1 >> 8; b[3] = x1 & 0xFF;
    nv_data(b, 4);
    nv_cmd(0x2B);
    b[0] = y0 >> 8; b[1] = y0 & 0xFF; b[2] = y1 >> 8; b[3] = y1 & 0xFF;
    nv_data(b, 4);
    nv_cmd(0x2C);
}

/* {cmd, n, d0, d1, d2} - vendor init table from v1 */
static const uint8_t s_nv_init[][5] = {
    {0xFF,1,0xA5,0,0},
    {0x9A,1,0x08,0,0},{0x9B,1,0x08,0,0},{0x9C,1,0xB0,0,0},{0x9D,1,0x16,0,0},{0x9E,1,0xC4,0,0},
    {0x8F,2,0x55,0x04,0},
    {0x84,1,0x90,0,0},{0x83,1,0x7B,0,0},{0x85,1,0x33,0,0},
    {0x60,1,0x00,0,0},{0x70,1,0x00,0,0},{0x61,1,0x02,0,0},{0x71,1,0x02,0,0},
    {0x62,1,0x04,0,0},{0x72,1,0x04,0,0},{0x6C,1,0x29,0,0},{0x7C,1,0x29,0,0},
    {0x6D,1,0x31,0,0},{0x7D,1,0x31,0,0},{0x6E,1,0x0F,0,0},{0x7E,1,0x0F,0,0},
    {0x66,1,0x21,0,0},{0x76,1,0x21,0,0},{0x68,1,0x3A,0,0},{0x78,1,0x3A,0,0},
    {0x63,1,0x07,0,0},{0x73,1,0x07,0,0},{0x64,1,0x05,0,0},{0x74,1,0x05,0,0},
    {0x65,1,0x02,0,0},{0x75,1,0x02,0,0},{0x67,1,0x23,0,0},{0x77,1,0x23,0,0},
    {0x69,1,0x08,0,0},{0x79,1,0x08,0,0},{0x6A,1,0x13,0,0},{0x7A,1,0x13,0,0},
    {0x6B,1,0x13,0,0},{0x7B,1,0x13,0,0},{0x6F,1,0x00,0,0},{0x7F,1,0x00,0,0},
    {0x50,1,0x00,0,0},{0x52,1,0xD6,0,0},{0x53,1,0x08,0,0},{0x54,1,0x08,0,0},
    {0x55,1,0x1E,0,0},{0x56,1,0x1C,0,0},
    {0xA0,3,0x2B,0x24,0x00},{0xA1,1,0x87,0,0},{0xA2,1,0x86,0,0},{0xA5,1,0x00,0,0},
    {0xA6,1,0x00,0,0},{0xA7,1,0x00,0,0},{0xA8,1,0x36,0,0},{0xA9,1,0x7E,0,0},{0xAA,1,0x7E,0,0},
    {0xB9,1,0x85,0,0},{0xBA,1,0x84,0,0},{0xBB,1,0x83,0,0},{0xBC,1,0x82,0,0},
    {0xBD,1,0x81,0,0},{0xBE,1,0x80,0,0},{0xBF,1,0x01,0,0},
    {0xC0,1,0x02,0,0},{0xC1,1,0x00,0,0},{0xC2,1,0x00,0,0},{0xC3,1,0x00,0,0},
    {0xC4,1,0x33,0,0},{0xC5,1,0x7E,0,0},{0xC6,1,0x7E,0,0},{0xC8,2,0x33,0x33,0},
    {0xC9,1,0x68,0,0},{0xCA,1,0x69,0,0},{0xCB,1,0x6A,0,0},{0xCC,1,0x6B,0,0},
    {0xCD,2,0x33,0x33,0},{0xCE,1,0x6C,0,0},{0xCF,1,0x6D,0,0},{0xD0,1,0x6E,0,0},{0xD1,1,0x6F,0,0},
    {0xAB,2,0x03,0x67,0},{0xAC,2,0x03,0x6B,0},{0xAD,2,0x03,0x68,0},{0xAE,2,0x03,0x6C,0},
    {0xB3,1,0x00,0,0},{0xB4,1,0x00,0,0},{0xB5,1,0x00,0,0},{0xB6,1,0x32,0,0},
    {0xB7,1,0x7E,0,0},{0xB8,1,0x7E,0,0},
    {0xE0,1,0x00,0,0},{0xE1,2,0x03,0x0F,0},{0xE2,1,0x04,0,0},{0xE3,1,0x01,0,0},
    {0xE4,1,0x0E,0,0},{0xE5,1,0x01,0,0},{0xE6,1,0x19,0,0},{0xE7,1,0x10,0,0},
    {0xE8,1,0x10,0,0},{0xEA,1,0x12,0,0},{0xEB,1,0xD0,0,0},{0xEC,1,0x04,0,0},
    {0xED,1,0x07,0,0},{0xEE,1,0x07,0,0},{0xEF,1,0x09,0,0},{0xF0,1,0xD0,0,0},{0xF1,1,0x0E,0,0},
    {0xE9,1,0x29,0,0},{0xEC,1,0x04,0,0},
    {0x35,1,0x00,0,0},{0x44,2,0x00,0x10,0},{0x46,1,0x10,0,0},
    {0xFF,1,0x00,0,0},
    {0x3A,1,0x05,0,0},   /* RGB565 */
    {0x36,1,0x60,0,0},   /* MADCTL: landscape */
    {0x21,0,0,0,0},      /* inversion on */
};

static void lcd2_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_LCD2_DC) | (1ULL << BOARD_LCD2_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_LCD2_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = BOARD_LCD2_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = NV_CHUNK,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .clock_speed_hz = BOARD_LCD2_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = NV_QUEUE,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(BOARD_SPI2_HOST, &dev, &s_lcd2));

    gpio_set_level(BOARD_LCD2_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(BOARD_LCD2_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    for (size_t i = 0; i < sizeof(s_nv_init) / sizeof(s_nv_init[0]); i++) {
        const uint8_t *e = s_nv_init[i];
        nv_seq(e[0], e[1], e[2], e[3], e[4]);
    }
    nv_cmd(0x11);  /* sleep out */
    vTaskDelay(pdMS_TO_TICKS(220));
    nv_cmd(0x29);  /* display on */
    vTaskDelay(pdMS_TO_TICKS(20));
}

void lcd2_flush(void)
{
    nv_window(0, 0, LCD2_W - 1, LCD2_H - 1);
    gpio_set_level(BOARD_LCD2_DC, 1);

    const uint8_t *p = (const uint8_t *)s_fb2;
    size_t remaining = LCD2_W * LCD2_H * sizeof(uint16_t);
    spi_transaction_t tr[NV_QUEUE];
    memset(tr, 0, sizeof(tr));
    int next = 0, queued = 0;

    while (remaining || queued) {
        while (remaining && queued < NV_QUEUE) {
            size_t n = remaining > NV_CHUNK ? NV_CHUNK : remaining;
            spi_transaction_t *t = &tr[next];
            memset(t, 0, sizeof(*t));
            t->length = n * 8;
            t->tx_buffer = p;
            ESP_ERROR_CHECK(spi_device_queue_trans(s_lcd2, t, portMAX_DELAY));
            p += n;
            remaining -= n;
            next = (next + 1) % NV_QUEUE;
            queued++;
        }
        spi_transaction_t *done;
        ESP_ERROR_CHECK(spi_device_get_trans_result(s_lcd2, &done, portMAX_DELAY));
        queued--;
    }
}

/* ------------------------------------------------------------------ init */

uint16_t *lcd1_framebuffer(void) { return s_fb1; }
uint16_t *lcd2_framebuffer(void) { return s_fb2; }

void lcd_init(void)
{

    /* lcd2 is pushed from internal DMA RAM; lcd1 framebuffer lives in PSRAM (as in v1). */
    s_fb2 = heap_caps_malloc(LCD2_W * LCD2_H * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_fb1 = heap_caps_aligned_alloc(64, LCD1_W * LCD1_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_fb1 || !s_fb2) {
        ESP_LOGE(TAG, "framebuffer alloc failed: need %u B internal DMA, largest free block %u B",
                 (unsigned)(LCD2_W * LCD2_H * sizeof(uint16_t)),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        abort();
    }
    memset(s_fb1, 0, LCD1_W * LCD1_H * sizeof(uint16_t));
    memset(s_fb2, 0, LCD2_W * LCD2_H * sizeof(uint16_t));

    lcd1_init();
    lcd1_begin_frame();
    lcd1_flush();

    lcd2_init();
    lcd2_flush();
}
