/* v1_state.h - the note / controller / channel state the v1 visualisers read. Fed by monitor.c. */
#pragma once
#include "v1_midi.h"

extern midi_values_t g_v1_values;
extern midi_channel_level_t g_v1_levels[16];
extern bool g_v1_key_on_fired;

void v1_state_note(uint8_t ch, uint8_t note, uint8_t vel, bool on);
void v1_state_cc(uint8_t cc, uint8_t value);
void v1_state_clear(void);
