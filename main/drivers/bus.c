#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "board.h"
#include "bus.h"

/* A binary semaphore (not a mutex): it is given back from an ISR. */
static SemaphoreHandle_t s_lock;

void bus_init(void)
{
    s_lock = xSemaphoreCreateBinary();
    xSemaphoreGive(s_lock);

    gpio_set_pull_mode(BOARD_SPI3_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BOARD_SD_CS, GPIO_PULLUP_ONLY);

    spi_bus_config_t cfg = {
        .sclk_io_num = BOARD_SPI3_SCLK,
        .mosi_io_num = BOARD_SPI3_MOSI,
        .miso_io_num = BOARD_SPI3_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD1_W * LCD1_H * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_SPI3_HOST, &cfg, SPI_DMA_CH_AUTO));
}

void bus_lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }

void bus_unlock(void) { xSemaphoreGive(s_lock); }

bool bus_unlock_from_isr(void)
{
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(s_lock, &woke);
    return woke == pdTRUE;
}
