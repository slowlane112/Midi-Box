/*
 * SPDX-FileCopyrightText: 2026 slowlane112
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
#ifndef V1_MIDI_H
#define V1_MIDI_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct
{
	const char *note;
	uint8_t number;
	uint8_t type;
	int8_t octave;
} midi_note_t;

typedef enum {
	CTRL_BANK_SELECT_MSB        = 0,
	CTRL_MODULATION_WHEEL       = 1,
	CTRL_BREATH_CONTROLLER      = 2,
	CTRL_FOOT_CONTROLLER        = 4,
	CTRL_PORTAMENTO_TIME        = 5,
	CTRL_DATA_ENTRY_MSB         = 6,
	CTRL_CHANNEL_VOLUME         = 7,
	CTRL_BALANCE                = 8,
	CTRL_PAN                    = 10,
	CTRL_EXPRESSION             = 11,
	CTRL_EFFECT_CONTROL_1       = 12,
	CTRL_EFFECT_CONTROL_2       = 13,

	CTRL_GENERAL_PURPOSE_1      = 16,
	CTRL_GENERAL_PURPOSE_2      = 17,
	CTRL_GENERAL_PURPOSE_3      = 18,
	CTRL_GENERAL_PURPOSE_4      = 19,

	CTRL_BANK_SELECT_LSB        = 32,
	CTRL_MODULATION_WHEEL_LSB   = 33,
	CTRL_BREATH_CONTROLLER_LSB  = 34,
	CTRL_FOOT_CONTROLLER_LSB    = 36,
	CTRL_PORTAMENTO_TIME_LSB    = 37,
	CTRL_DATA_ENTRY_LSB         = 38,
	CTRL_CHANNEL_VOLUME_LSB     = 39,
	CTRL_BALANCE_LSB            = 40,
	CTRL_PAN_LSB                = 42,
	CTRL_EXPRESSION_LSB         = 43,
	CTRL_EFFECT_CONTROL_1_LSB   = 44,
	CTRL_EFFECT_CONTROL_2_LSB   = 45,

	CTRL_SUSTAIN_PEDAL          = 64,
	CTRL_PORTAMENTO_SWITCH      = 65,
	CTRL_SOSTENUTO_PEDAL        = 66,
	CTRL_SOFT_PEDAL             = 67,
	CTRL_LEGATO_SWITCH          = 68,
	CTRL_HOLD_2                 = 69,

	CTRL_SOUND_CONTROLLER_1     = 70,
	CTRL_SOUND_CONTROLLER_2     = 71,
	CTRL_SOUND_CONTROLLER_3     = 72,
	CTRL_SOUND_CONTROLLER_4     = 73,
	CTRL_SOUND_CONTROLLER_5     = 74,
	CTRL_SOUND_CONTROLLER_6     = 75,
	CTRL_SOUND_CONTROLLER_7     = 76,
	CTRL_SOUND_CONTROLLER_8     = 77,
	CTRL_SOUND_CONTROLLER_9     = 78,
	CTRL_SOUND_CONTROLLER_10    = 79,

	CTRL_GENERAL_PURPOSE_5      = 80,
	CTRL_GENERAL_PURPOSE_6      = 81,
	CTRL_GENERAL_PURPOSE_7      = 82,
	CTRL_GENERAL_PURPOSE_8      = 83,
	CTRL_PORTAMENTO_CONTROL     = 84,

	CTRL_EFFECT_1_DEPTH         = 91,
	CTRL_EFFECT_2_DEPTH         = 92,
	CTRL_EFFECT_3_DEPTH         = 93,
	CTRL_EFFECT_4_DEPTH         = 94,
	CTRL_EFFECT_5_DEPTH         = 95,

	CTRL_DATA_INCREMENT         = 96,
	CTRL_DATA_DECREMENT         = 97,
	CTRL_NRPN_LSB               = 98,
	CTRL_NRPN_MSB               = 99,
	CTRL_RPN_LSB                = 100,
	CTRL_RPN_MSB                = 101,

	CTRL_ALL_SOUND_OFF          = 120,
	CTRL_RESET_ALL_CONTROLLERS  = 121,
	CTRL_LOCAL_CONTROL          = 122,
	CTRL_ALL_NOTES_OFF          = 123,
	CTRL_OMNI_OFF               = 124,
	CTRL_OMNI_ON                = 125,
	CTRL_MONO_MODE_ON           = 126,
	CTRL_POLY_MODE_ON           = 127
} midi_controller_t;


typedef enum {
    // Piano (0 - 7)
    MIDI_INST_ACOUSTIC_GRAND_PIANO = 0,
    MIDI_INST_BRIGHT_ACOUSTIC_PIANO = 1,
    MIDI_INST_ELECTRIC_GRAND_PIANO = 2,
    MIDI_INST_HONKY_TONK_PIANO = 3,
    MIDI_INST_ELECTRIC_PIANO_1 = 4,
    MIDI_INST_ELECTRIC_PIANO_2 = 5,
    MIDI_INST_HARPSICHORD = 6,
    MIDI_INST_CLAVINET = 7,
    
    // Chromatic Percussion (8 - 15)
    MIDI_INST_CELESTA = 8,
    MIDI_INST_GLOCKENSPIEL = 9,
    MIDI_INST_MUSIC_BOX = 10,
    MIDI_INST_VIBRAPHONE = 11,
    MIDI_INST_MARIMBA = 12,
    MIDI_INST_XYLOPHONE = 13,
    MIDI_INST_TUBULAR_BELLS = 14,
    MIDI_INST_DULCIMER = 15,

    // Organs (16 - 23)
    MIDI_INST_DRAWBAR_ORGAN = 16,
    MIDI_INST_PERCUSSIVE_ORGAN = 17,
    MIDI_INST_ROCK_ORGAN = 18,
    MIDI_INST_CHURCH_ORGAN = 19,
    MIDI_INST_REED_ORGAN = 20,
    MIDI_INST_ACCORDION = 21,
    MIDI_INST_HARMONICA = 22,
    MIDI_INST_TANGO_ACCORDION = 23,

    // Guitars (24 - 31)
    MIDI_INST_ACOUSTIC_GUITAR_NYLON = 24,
    MIDI_INST_ACOUSTIC_GUITAR_STEEL = 25,
    MIDI_INST_ELECTRIC_GUITAR_JAZZ = 26,
    MIDI_INST_ELECTRIC_GUITAR_CLEAN = 27,
    MIDI_INST_ELECTRIC_GUITAR_MUTED = 28,
    MIDI_INST_OVERDRIVEN_GUITAR = 29,
    MIDI_INST_DISTORTION_GUITAR = 30,
    MIDI_INST_GUITAR_HARMONICS = 31,

    // Basses (32 - 39)
    MIDI_INST_ACOUSTIC_BASS = 32,
    MIDI_INST_ELECTRIC_BASS_FINGER = 33,
    MIDI_INST_ELECTRIC_BASS_PICK = 34,
    MIDI_INST_FRETLESS_BASS = 35,
    MIDI_INST_SLAP_BASS_1 = 36,
    MIDI_INST_SLAP_BASS_2 = 37,
    MIDI_INST_SYNTH_BASS_1 = 38,
    MIDI_INST_SYNTH_BASS_2 = 39,

    // Strings & Orchestral (40 - 47)
    MIDI_INST_VIOLIN = 40,
    MIDI_INST_VIOLA = 41,
    MIDI_INST_CELLO = 42,
    MIDI_INST_CONTRABASS = 43,
    MIDI_INST_TREMOLO_STRINGS = 44,
    MIDI_INST_PIZZICATO_STRINGS = 45,
    MIDI_INST_ORCHESTRAL_HARP = 46,
    MIDI_INST_TIMPANI = 47,

    // Ensemble (48 - 55)
    MIDI_INST_STRING_ENSEMBLE_1 = 48,
    MIDI_INST_STRING_ENSEMBLE_2 = 49,
    MIDI_INST_SYNTH_STRINGS_1 = 50,
    MIDI_INST_SYNTH_STRINGS_2 = 51,
    MIDI_INST_CHOIR_AAHS = 52,
    MIDI_INST_VOICE_OOHS = 53,
    MIDI_INST_SYNTH_VOICE = 54,
    MIDI_INST_ORCHESTRA_HIT = 55,

    // Brass (56 - 63)
    MIDI_INST_TRUMPET = 56,
    MIDI_INST_TROMBONE = 57,
    MIDI_INST_TUBA = 58,
    MIDI_INST_MUTED_TRUMPET = 59,
    MIDI_INST_FRENCH_HORN = 60,
    MIDI_INST_BRASS_SECTION = 61,
    MIDI_INST_SYNTH_BRASS_1 = 62,
    MIDI_INST_SYNTH_BRASS_2 = 63,

    // Reeds (64 - 71)
    MIDI_INST_SOPRANO_SAX = 64,
    MIDI_INST_ALTO_SAX = 65,
    MIDI_INST_TENOR_SAX = 66,
    MIDI_INST_BARITONE_SAX = 67,
    MIDI_INST_OBOE = 68,
    MIDI_INST_ENGLISH_HORN = 69,
    MIDI_INST_BASSOON = 70,
    MIDI_INST_CLARINET = 71,

    // Pipe (72 - 79)
    MIDI_INST_PICCOLO = 72,
    MIDI_INST_FLUTE = 73,
    MIDI_INST_RECORDER = 74,
    MIDI_INST_PAN_FLUTE = 75,
    MIDI_INST_BLOWN_BOTTLE = 76,
    MIDI_INST_SHAKUHACHI = 77,
    MIDI_INST_WHISTLE = 78,
    MIDI_INST_OCARINA = 79,

    // Synth Lead (80 - 87)
    MIDI_INST_LEAD_1_SQUARE = 80,
    MIDI_INST_LEAD_2_SAWTOOTH = 81,
    MIDI_INST_LEAD_3_CALLIOPE = 82,
    MIDI_INST_LEAD_4_CHIFF = 83,
    MIDI_INST_LEAD_5_CHARANG = 84,
    MIDI_INST_LEAD_6_VOICE = 85,
    MIDI_INST_LEAD_7_FIFTHS = 86,
    MIDI_INST_LEAD_8_BASS_LEAD = 87,

    // Synth Pad (88 - 95)
    MIDI_INST_PAD_1_NEW_AGE = 88,
    MIDI_INST_PAD_2_WARM = 89,
    MIDI_INST_PAD_3_POLYSYNTH = 90,
    MIDI_INST_PAD_4_CHOIR = 91,
    MIDI_INST_PAD_5_BOWED = 92,
    MIDI_INST_PAD_6_METALLIC = 93,
    MIDI_INST_PAD_7_HALO = 94,
    MIDI_INST_PAD_8_SWEEP = 95,

    // Synth Effects (96 - 103)
    MIDI_INST_FX_1_RAIN = 96,
    MIDI_INST_FX_2_SOUNDTRACK = 97,
    MIDI_INST_FX_3_CRYSTAL = 98,
    MIDI_INST_FX_4_ATMOSPHERE = 99,
    MIDI_INST_FX_5_BRIGHTNESS = 100,
    MIDI_INST_FX_6_GOBLINS = 101,
    MIDI_INST_FX_7_ECHOES = 102,
    MIDI_INST_FX_8_SCI_FI = 103,

    // Ethnic (104 - 111)
    MIDI_INST_SITAR = 104,
    MIDI_INST_BANJO = 105,
    MIDI_INST_SHAMISEN = 106,
    MIDI_INST_KOTO = 107,
    MIDI_INST_KALIMBA = 108,
    MIDI_INST_BAGPIPE = 109,
    MIDI_INST_FIDDLE = 110,
    MIDI_INST_SHANAI = 111,

    // Percussive (112 - 119)
    MIDI_INST_TINKLE_BELL = 112,
    MIDI_INST_AGOGO = 113,
    MIDI_INST_STEEL_DRUMS = 114,
    MIDI_INST_WOODBLOCK = 115,
    MIDI_INST_TAIKO_DRUM = 116,
    MIDI_INST_MELODIC_TOM = 117,
    MIDI_INST_SYNTH_DRUM = 118,
    MIDI_INST_REVERSE_CYMBAL = 119,

    // Sound Effects (120 - 127)
    MIDI_INST_GUITAR_FRET_NOISE = 120,
    MIDI_INST_BREATH_NOISE = 121,
    MIDI_INST_SEASHORE = 122,
    MIDI_INST_BIRD_TWEET = 123,
    MIDI_INST_TELEPHONE_RING = 124,
    MIDI_INST_HELICOPTER = 125,
    MIDI_INST_APPLAUSE = 126,
    MIDI_INST_GUNSHOT = 127

} midi_instrument_t;

typedef struct
{
	uint8_t note_values[128];
	uint8_t drum_values[128];
	uint64_t drum_timestamp_us[128];
	uint8_t controller_values[128];
} midi_values_t;

typedef struct
{
	uint8_t  level;
	uint64_t timestamp_us;
} midi_channel_level_t;

extern const midi_note_t midi_notes[];
void midi_note_set(midi_values_t *midi_values, uint8_t channel, uint8_t note_num, uint8_t velocity, bool key_on);
void midi_controller_set(midi_values_t *midi_values, uint8_t controller, uint8_t value);
void midi_values_clear(midi_values_t *midi_values);
const char* midi_get_instrument_name(uint8_t patch);

uint8_t midi_channel_level_get(const midi_channel_level_t *midi_channel_level);
void midi_channel_level_set(midi_channel_level_t *midi_channel_levels, uint8_t channel, uint8_t level);
void midi_channel_level_clear(midi_channel_level_t *midi_channel_levels);

#endif
