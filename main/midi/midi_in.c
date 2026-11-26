#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "board.h"
#include "midi_out.h"
#include "monitor.h"
#include "midi_parse.h"
#include "midi_in.h"

static TaskHandle_t s_task;
static volatile bool s_run;
static volatile bool s_thru = true;
static volatile uint8_t s_port;
static midi_in_listener_t s_listener;
static midi_parser_t s_parser;
static midi_in_stats_t s_stats;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void midi_in_dispatch(const uint8_t *msg, uint16_t len, uint8_t source)
{
    uint8_t st = msg[0];
    if (st < 0xF0) {
        uint8_t type = st & 0xF0, ch = st & 0x0F;
        if (type == 0x90 && len == 3) {
            monitor_note(ch, msg[1], msg[2], msg[2] != 0);
        } else if (type == 0x80 && len == 3) {
            monitor_note(ch, msg[1], 0, false);
        } else if (type == 0xB0 && len == 3) {
            monitor_cc(ch, msg[1], msg[2]);
        }
        if (!s_thru) {
            monitor_channel_msgs(msg, len);   /* with thru on, midi_out sees it anyway */
        }
    }
    if (s_thru && st != 0xF8 && st != 0xFA && st != 0xFB && st != 0xFC) {
        midi_out_send(msg, len);   /* clock / transport are not for the synth */
    }
    if (s_listener) {
        s_listener(msg, len, source);
    }
    if (st != 0xF8) {
        portENTER_CRITICAL(&s_mux);
        s_stats.msg_count++;
        s_stats.last_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_stats.last_len = len > 3 ? 3 : (uint8_t)len;
        memcpy(s_stats.last, msg, s_stats.last_len);
        s_stats.last_src = source;
        portEXIT_CRITICAL(&s_mux);
    }
}

static void on_msg(const uint8_t *msg, uint16_t len, void *ctx)
{
    (void)ctx;
    midi_in_dispatch(msg, len, s_port);
}

static void din_task(void *arg)
{
    (void)arg;
    uint8_t buf[64];
    while (s_run) {
        int n = uart_read_bytes(BOARD_DIN_UART, buf, sizeof(buf), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            midi_parser_feed(&s_parser, buf[i]);
        }
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

void midi_in_init(void)
{
    uart_config_t cfg = {
        .baud_rate = 31250,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(BOARD_DIN_UART, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BOARD_DIN_UART, &cfg));
    midi_parser_init(&s_parser, on_msg, NULL);
}

void midi_in_start(uint8_t port)
{
    midi_in_stop();
    s_port = port ? MIDI_SRC_DIN2 : MIDI_SRC_DIN1;
    ESP_ERROR_CHECK(uart_set_pin(BOARD_DIN_UART, UART_PIN_NO_CHANGE, port ? BOARD_DIN2_RX : BOARD_DIN1_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    uart_flush_input(BOARD_DIN_UART);
    midi_parser_reset(&s_parser);
    s_run = true;
    xTaskCreatePinnedToCore(din_task, "din", 4096, NULL, 12, &s_task, 1);
}

void midi_in_stop(void)
{
    s_run = false;
    while (s_task) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

bool midi_in_running(void) { return s_run; }
void midi_in_set_thru(bool on) { s_thru = on; }
void midi_in_set_listener(midi_in_listener_t fn) { s_listener = fn; }

void midi_in_get_stats(midi_in_stats_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_mux);
}
