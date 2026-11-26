#include <string.h>
#include "v1_midi.h"
#include "esp_timer.h"

const midi_note_t midi_notes[] =
{
	{ "C", 0, 1, -1 },
	{ "C#", 1, 2, -1 },
	{ "D", 2, 1, -1 },
	{ "D#", 3, 2, -1 },
	{ "E", 4, 1, -1 },
	{ "F", 5, 1, -1 },
	{ "F#", 6, 2, -1 },
	{ "G", 7, 1, -1 },
	{ "G#", 8, 2, -1 },
	{ "A", 9, 1, -1 },
	{ "A#", 10, 2, -1 },
	{ "B", 11, 1, -1 },
	{ "C", 12, 1, 0 },
	{ "C#", 13, 2, 0 },
	{ "D", 14, 1, 0 },
	{ "D#", 15, 2, 0 },
	{ "E", 16, 1, 0 },
	{ "F", 17, 1, 0 },
	{ "F#", 18, 2, 0 },
	{ "G", 19, 1, 0 },
	{ "G#", 20, 2, 0 },	
	{ "A", 21, 1, 0 },
	{ "A#", 22, 2, 0 },
	{ "B", 23, 1, 0 },
	{ "C", 24, 1, 1 },
	{ "C#", 25, 2, 1 },
	{ "D", 26, 1, 1 },
	{ "D#", 27, 2, 1 },
	{ "E", 28, 1, 1 },
	{ "F", 29, 1, 1 },
	{ "F#", 30, 2, 1 },
	{ "G", 31, 1, 1 },
	{ "G#", 32, 2, 1 },
	{ "A", 33, 1, 1 },
	{ "A#", 34, 2, 1 },
	{ "B", 35, 1, 1 },
	{ "C", 36, 1, 2 },
	{ "C#", 37, 2, 2 },
	{ "D", 38, 1, 2 },
	{ "D#", 39, 2, 2 },
	{ "E", 40, 1, 2 },
	{ "F", 41, 1, 2 },
	{ "F#", 42, 2, 2 },
	{ "G", 43, 1, 2 },
	{ "G#", 44, 2, 2 },
	{ "A", 45, 1, 2 },
	{ "A#", 46, 2, 2 },
	{ "B", 47, 1, 2 },
	{ "C", 48, 1, 3 },
	{ "C#", 49, 2, 3 },
	{ "D", 50, 1, 3 },
	{ "D#", 51, 2, 3 },
	{ "E", 52, 1, 3 },
	{ "F", 53, 1, 3 },
	{ "F#", 54, 2, 3 },
	{ "G", 55, 1, 3 },
	{ "G#", 56, 2, 3 },
	{ "A", 57, 1, 3 },
	{ "A#", 58, 2, 3 },
	{ "B", 59, 1, 3 },
	{ "C", 60, 1, 4 },
	{ "C#", 61, 2, 4 },
	{ "D", 62, 1, 4 },
	{ "D#", 63, 2, 4 },
	{ "E", 64, 1, 4 },
	{ "F", 65, 1, 4 },
	{ "F#", 66, 2, 4 },
	{ "G", 67, 1, 4 },
	{ "G#", 68, 2, 4 },
	{ "A", 69, 1, 4 },
	{ "A#", 70, 2, 4 },
	{ "B", 71, 1, 4 },
	{ "C", 72, 1, 5 },
	{ "C#", 73, 2, 5 },
	{ "D", 74, 1, 5 },
	{ "D#", 75, 2, 5 },
	{ "E", 76, 1, 5 },
	{ "F", 77, 1, 5 },
	{ "F#", 78, 2, 5 },
	{ "G", 79, 1, 5 },
	{ "G#", 80, 2, 5 },
	{ "A", 81, 1, 5 },
	{ "A#", 82, 2, 5 },
	{ "B", 83, 1, 5 },
	{ "C", 84, 1, 6 },
	{ "C#", 85, 2, 6 },
	{ "D", 86, 1, 6 },
	{ "D#", 87, 2, 6 },
	{ "E", 88, 1, 6 },
	{ "F", 89, 1, 6 },
	{ "F#", 90, 2, 6 },
	{ "G", 91, 1, 6 },
	{ "G#", 92, 2, 6 },
	{ "A", 93, 1, 6 },
	{ "A#", 94, 2, 6 },
	{ "B", 95, 1, 6 },
	{ "C", 96, 1, 7 },
	{ "C#", 97, 2, 7 },
	{ "D", 98, 1, 7 },
	{ "D#", 99, 2, 7 },
	{ "E", 100, 1, 7 },
	{ "F", 101, 1, 7 },
	{ "F#", 102, 2, 7 },
	{ "G", 103, 1, 7 },
	{ "G#", 104, 2, 7 },
	{ "A", 105, 1, 7 },
	{ "A#", 106, 2, 7 },
	{ "B", 107, 1, 7 },
	{ "C", 108, 1, 8 },
	{ "C#", 109, 2, 8 },
	{ "D", 110, 1, 8 },
	{ "D#", 111, 2, 8 },
	{ "E", 112, 1, 8 },
	{ "F", 113, 1, 8 },
	{ "F#", 114, 2, 8 },
	{ "G", 115, 1, 8 },
	{ "G#", 116, 2, 8 },
	{ "A", 117, 1, 8 },
	{ "A#", 118, 2, 8 },
	{ "B", 119, 1, 8 },
	{ "C", 120, 1, 9 },
	{ "C#", 121, 2, 9 },
	{ "D", 122, 1, 9 },
	{ "D#", 123, 2, 9 },
	{ "E", 124, 1, 9 },
	{ "F", 125, 1, 9 },
	{ "F#", 126, 2, 9 },
	{ "G", 127, 1, 9 }
};


// Companion function to turn a patch number into its exact string name
const char* midi_get_instrument_name(uint8_t patch) {
    static const char *names[128] = {
        "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
        "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
        "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
        "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)", "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
        "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
        "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
        "String Ensemble 1", "String Ensemble 2", "SynthStrings 1", "SynthStrings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
        "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
        "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
        "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
        "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
        "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
        "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)", "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
        "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
        "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
        "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot"
    };
    if (patch < 128) {
        return names[patch];
    }
    return "Unknown Instrument";
}


void midi_note_set(midi_values_t *midi_values, uint8_t channel, uint8_t note_num, uint8_t velocity, bool key_on)
{
	
	if (channel == 9) {
		
		//printf("drum: %d\n", note_num);
		
		if (key_on) {
			midi_values->drum_values[note_num] = velocity;
			midi_values->drum_timestamp_us[note_num] = esp_timer_get_time();
		}
		
	}
	else {
	
	
		if (key_on) {
			midi_values->note_values[note_num] = velocity;
		}
		else {
			midi_values->note_values[note_num] = 0;
		}
	
	}
}

void midi_controller_set(midi_values_t *midi_values, uint8_t controller, uint8_t value)
{
	midi_values->controller_values[controller] = value;
}

void midi_values_clear(midi_values_t *midi_values)
{
	memset(midi_values, 0, sizeof(midi_values_t));
}

/*
uint8_t midi_channel_level_get(const midi_channel_level_t *midi_channel_level)
{
	uint32_t elapsed_ms = (esp_timer_get_time() - midi_channel_level->timestamp_us) / 1000;
	uint32_t decay = elapsed_ms / 10;
	if (decay >= midi_channel_level->level) return 0;
	return midi_channel_level->level - decay;
}
*/

uint8_t midi_channel_level_get(const midi_channel_level_t *midi_channel_level)
{
    uint32_t elapsed_ms = (esp_timer_get_time() - midi_channel_level->timestamp_us) / 1000;
    if (elapsed_ms <= 100) return midi_channel_level->level;
    uint32_t decay = (elapsed_ms - 100) / 5;
    if (decay >= midi_channel_level->level) return 0;
    return midi_channel_level->level - decay;
}

void midi_channel_level_set(midi_channel_level_t *midi_channel_levels, uint8_t channel, uint8_t level)
{
	if (channel >= 16) return;
	if (level > midi_channel_level_get(&midi_channel_levels[channel])) {
		midi_channel_levels[channel].level = level;
		midi_channel_levels[channel].timestamp_us = esp_timer_get_time();
	}
}

void midi_channel_level_clear(midi_channel_level_t *midi_channel_levels)
{
    memset(midi_channel_levels, 0, sizeof(midi_channel_level_t) * 16);
}

