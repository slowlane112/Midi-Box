/*
 * midi_parse.h - byte-stream MIDI parser (DIN / UART), with running status,
 * real-time bytes in the middle of messages, and SysEx.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MIDI_SYSEX_MAX 256

typedef struct midi_parser midi_parser_t;
/* msg = complete message incl. status, len = 1..3 (or SysEx incl. F0..F7) */
typedef void (*midi_msg_cb)(const uint8_t *msg, uint16_t len, void *ctx);

struct midi_parser {
    uint8_t running;
    uint8_t msg[3];
    uint8_t count;
    uint8_t needed;
    bool in_sysex;
    uint16_t sysex_len;
    uint8_t sysex[MIDI_SYSEX_MAX];
    midi_msg_cb cb;
    void *ctx;
};

void midi_parser_init(midi_parser_t *p, midi_msg_cb cb, void *ctx);
void midi_parser_feed(midi_parser_t *p, uint8_t byte);
void midi_parser_reset(midi_parser_t *p);
/* true between messages (no status pending, not in SysEx) */
bool midi_parser_idle(const midi_parser_t *p);
