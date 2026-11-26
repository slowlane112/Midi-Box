/*
 * midi_in.h - DIN MIDI input (two sockets, one UART: the RX pin is switched).
 * Messages go to the monitor, optionally through to the synth (thru),
 * and to a listener (e.g. the recorder in phase 2).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef void (*midi_in_listener_t)(const uint8_t *msg, uint16_t len, uint8_t source);

enum { MIDI_SRC_DIN1 = 0, MIDI_SRC_DIN2 = 1, MIDI_SRC_BLE = 2 };

typedef struct {
    uint32_t msg_count;
    uint32_t last_ms;
    uint8_t last[3];
    uint8_t last_len;
    uint8_t last_src;
} midi_in_stats_t;

void midi_in_init(void);
void midi_in_start(uint8_t port);       /* 0 = DIN 1 (back), 1 = DIN 2 (front) */
void midi_in_stop(void);
bool midi_in_running(void);
void midi_in_set_thru(bool on);
void midi_in_set_listener(midi_in_listener_t fn);
void midi_in_get_stats(midi_in_stats_t *out);
/* Common entry point for every input source (DIN task, BLE in phase 2). */
void midi_in_dispatch(const uint8_t *msg, uint16_t len, uint8_t source);
