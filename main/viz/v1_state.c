#include "v1_state.h"

midi_values_t g_v1_values;
midi_channel_level_t g_v1_levels[16];
bool g_v1_key_on_fired;

void v1_state_note(uint8_t ch, uint8_t note, uint8_t vel, bool on)
{
    midi_note_set(&g_v1_values, ch & 0x0F, note & 0x7F, vel, on);
    if (on) {
        g_v1_key_on_fired = true;
        midi_channel_level_set(g_v1_levels, ch & 0x0F, vel);
    }
}

void v1_state_cc(uint8_t cc, uint8_t value)
{
    midi_controller_set(&g_v1_values, cc & 0x7F, value);
}

void v1_state_clear(void)
{
    midi_values_clear(&g_v1_values);
    midi_channel_level_clear(g_v1_levels);
    g_v1_key_on_fired = false;
}
