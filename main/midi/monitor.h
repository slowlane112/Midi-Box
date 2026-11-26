/*
 * monitor.h - what is sounding right now, for the visualisers and activity lights.
 * Written by the player / MIDI-in tasks, read by the UI. Plain bytes, races are harmless.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint8_t vel[16][128];      /* velocity of each held note, 0 = off */
    volatile uint8_t level[16];         /* per-channel level, decays in monitor_decay() */
    volatile uint8_t energy[128];       /* per-note energy for the spectrum, decays */
    volatile uint32_t last_ms[16];      /* last activity per channel */
    volatile uint8_t ons[16][128];      /* note-on counter, so a visualiser sees notes shorter than a frame */
    volatile uint8_t on_vel[16][128];   /* velocity of the latest note-on */
    /* what each channel of the synth is set to, from the messages actually sent to it
     * (and MIDI in), for the Spectrum: instrument, loudness, pedal, pitch bend */
    volatile uint8_t program[16];       /* General MIDI program 0..127 */
    volatile uint8_t volume[16];        /* CC7, default 100 */
    volatile uint8_t expression[16];    /* CC11, default 127 */
    volatile uint8_t sustain[16];       /* CC64 >= 64 */
    volatile int16_t bend[16];          /* pitch bend in cents (range +-2 semitones) */
} monitor_t;

extern monitor_t g_mon;

void monitor_note(uint8_t ch, uint8_t note, uint8_t vel, bool on);
void monitor_cc(uint8_t ch, uint8_t cc, uint8_t value);
void monitor_channel_msgs(const uint8_t *msg, int len);   /* bytes going to the synth */
void monitor_init(void);    /* channel defaults; once at start-up */
void monitor_clear(void);
void monitor_decay(void);   /* call once per visualiser frame */
bool monitor_any_note(uint8_t note);
