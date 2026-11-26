#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"
#include "midi_out.h"
#include "monitor.h"

static const uint8_t s_vol_curve[21] = { 0, 5, 12, 18, 25, 31, 38, 44, 51, 57, 64, 70, 77, 83, 90, 96, 103, 109, 116, 122, 127 };
static uint8_t s_program[16];

void midi_out_init(void)
{
    monitor_init();
    uart_config_t cfg = {
        .baud_rate = 31250,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    /* TX ring buffer so senders do not block on short bursts */
    ESP_ERROR_CHECK(uart_driver_install(BOARD_SYNTH_UART, 256, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BOARD_SYNTH_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(BOARD_SYNTH_UART, BOARD_SYNTH_TX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void midi_out_send(const uint8_t *msg, size_t len)
{
    if (len == 0) {
        return;
    }
    if ((msg[0] & 0xF0) == 0xC0 && len >= 2) {
        s_program[msg[0] & 0x0F] = msg[1] & 0x7F;
    }
    monitor_channel_msgs(msg, (int)len);   /* instrument, volume, pedal, bend for the Spectrum */
    uart_write_bytes(BOARD_SYNTH_UART, (const char *)msg, len);
}

void midi_out_note(uint8_t ch, uint8_t note, uint8_t vel, bool on)
{
    uint8_t m[3] = { (uint8_t)((on ? 0x90 : 0x80) | (ch & 0x0F)), (uint8_t)(note & 0x7F), (uint8_t)(vel & 0x7F) };
    midi_out_send(m, 3);
}

void midi_out_cc(uint8_t ch, uint8_t cc, uint8_t val)
{
    uint8_t m[3] = { (uint8_t)(0xB0 | (ch & 0x0F)), (uint8_t)(cc & 0x7F), (uint8_t)(val & 0x7F) };
    midi_out_send(m, 3);
}

void midi_out_program(uint8_t ch, uint8_t program)
{
    uint8_t m[2] = { (uint8_t)(0xC0 | (ch & 0x0F)), (uint8_t)(program & 0x7F) };
    midi_out_send(m, 2);
}

void midi_out_pitch(uint8_t ch, uint16_t v)
{
    uint8_t m[3] = { (uint8_t)(0xE0 | (ch & 0x0F)), (uint8_t)(v & 0x7F), (uint8_t)((v >> 7) & 0x7F) };
    midi_out_send(m, 3);
}

void midi_out_channel_off(uint8_t ch)
{
    uint8_t m[9] = {
        (uint8_t)(0xB0 | ch), 64, 0,    /* sustain off */
        (uint8_t)(0xB0 | ch), 123, 0,   /* all notes off */
        (uint8_t)(0xB0 | ch), 120, 0,   /* all sound off */
    };
    midi_out_send(m, sizeof(m));
}

void midi_out_all_notes_off(void)
{
    for (uint8_t ch = 0; ch < 16; ch++) {
        midi_out_channel_off(ch);
    }
}

void midi_out_set_volume(uint8_t level)
{
    if (level > 20) {
        level = 20;
    }
    uint8_t m[9] = { 0xB0, 0x63, 0x37, 0xB0, 0x62, 0x07, 0xB0, 0x06, s_vol_curve[level] };
    midi_out_send(m, sizeof(m));
}

void midi_out_set_bank(uint8_t bank)
{
    for (uint8_t ch = 0; ch < 16; ch++) {
        if (ch == 9) {
            continue;  /* drums */
        }
        midi_out_cc(ch, 0, bank == BANK_MT32 ? 127 : 0);
        midi_out_program(ch, s_program[ch]);  /* bank only applies on the next program change */
    }
}

uint8_t midi_out_program_of(uint8_t ch) { return s_program[ch & 0x0F]; }

void midi_out_reset(uint8_t bank, uint8_t volume_level)
{
    static const uint8_t gm_on[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
    midi_out_all_notes_off();
    midi_out_send(gm_on, sizeof(gm_on));
    vTaskDelay(pdMS_TO_TICKS(50));  /* the synth needs a moment after GM reset */
    memset(s_program, 0, sizeof(s_program));
    midi_out_set_bank(bank);
    midi_out_set_volume(volume_level);
}
