/*
 * midi_out.h - the only code that talks to the SAM2695.
 * uart_write_bytes() is atomic per call, so every message goes out in one call
 * and messages from different tasks never interleave.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum { BANK_GM = 0, BANK_MT32 = 1 };

void midi_out_init(void);
void midi_out_send(const uint8_t *msg, size_t len);
void midi_out_note(uint8_t ch, uint8_t note, uint8_t vel, bool on);
void midi_out_cc(uint8_t ch, uint8_t cc, uint8_t val);
void midi_out_program(uint8_t ch, uint8_t program);
void midi_out_pitch(uint8_t ch, uint16_t value14);

void midi_out_all_notes_off(void);         /* sustain off + all notes off + all sound off, every channel */
void midi_out_channel_off(uint8_t ch);
/* GM System On, then bank and master volume. */
void midi_out_reset(uint8_t bank, uint8_t volume_level);
void midi_out_set_volume(uint8_t level);   /* 0..20, SAM2695 master volume (NRPN 37 07) */
void midi_out_set_bank(uint8_t bank);      /* bank select on every melodic channel + re-send programs */
uint8_t midi_out_program_of(uint8_t ch);   /* last program sent on a channel */
