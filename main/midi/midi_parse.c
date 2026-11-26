#include <string.h>
#include "midi_parse.h"

static uint8_t data_len(uint8_t status)
{
    switch (status & 0xF0) {
    case 0xC0: case 0xD0: return 1;
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    default: break;
    }
    switch (status) {
    case 0xF1: case 0xF3: return 1;
    case 0xF2: return 2;
    default: return 0;
    }
}

void midi_parser_init(midi_parser_t *p, midi_msg_cb cb, void *ctx)
{
    memset(p, 0, sizeof(*p));
    p->cb = cb;
    p->ctx = ctx;
}

void midi_parser_reset(midi_parser_t *p)
{
    p->running = 0;
    p->count = 0;
    p->needed = 0;
    p->in_sysex = false;
    p->sysex_len = 0;
}

void midi_parser_feed(midi_parser_t *p, uint8_t b)
{
    if (b >= 0xF8) {                     /* real-time: may appear anywhere */
        if (b != 0xFE) {                 /* ignore active sensing */
            p->cb(&b, 1, p->ctx);
        }
        return;
    }
    if (p->in_sysex) {
        if (b == 0xF7) {
            if (p->sysex_len < MIDI_SYSEX_MAX) {
                p->sysex[p->sysex_len++] = b;
                p->cb(p->sysex, p->sysex_len, p->ctx);
            }
            p->in_sysex = false;
            return;
        }
        if (!(b & 0x80)) {
            if (p->sysex_len < MIDI_SYSEX_MAX) {
                p->sysex[p->sysex_len++] = b;
            } else {
                p->sysex_len = MIDI_SYSEX_MAX + 1;   /* too long: drop it */
            }
            return;
        }
        p->in_sysex = false;             /* status byte aborts SysEx, handle it below */
    }
    if (b & 0x80) {
        p->count = 0;
        if (b == 0xF0) {
            p->in_sysex = true;
            p->running = 0;
            p->sysex[0] = b;
            p->sysex_len = 1;
            return;
        }
        if (b >= 0xF0) {
            p->running = 0;              /* system common cancels running status */
            if (b == 0xF6) {
                p->cb(&b, 1, p->ctx);
                p->needed = 0;
                return;
            }
            p->msg[0] = b;
            p->needed = data_len(b);
            if (p->needed == 0) {
                p->msg[0] = 0;           /* undefined F4/F5/F7 */
            }
            return;
        }
        p->running = b;
        p->msg[0] = b;
        p->needed = data_len(b);
        return;
    }
    /* data byte */
    if (p->msg[0] == 0) {
        if (!p->running) {
            return;
        }
        p->msg[0] = p->running;
        p->needed = data_len(p->running);
        p->count = 0;
    }
    p->msg[1 + p->count++] = b;
    if (p->count >= p->needed) {
        p->cb(p->msg, (uint16_t)(1 + p->needed), p->ctx);
        p->count = 0;
        p->msg[0] = 0;                   /* next data byte uses running status */
    }
}

bool midi_parser_idle(const midi_parser_t *p)
{
    return p->msg[0] == 0 && !p->in_sysex;
}
