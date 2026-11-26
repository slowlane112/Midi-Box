/*
 * vz.h - the v2 visualisers (written for the new UI).
 * Each draws a full frame into framebuffer_2.
 *   colour: standard colour index (button 11)
 *   opt:    option state (button 10)
 *   active: a song is playing or an input is open
 *   fresh:  first frame after switching to it: reset any history
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void vz_spectrum_draw(int colour, int opt, bool active, bool fresh);
void vz_roll_draw(int colour, int opt, bool active, bool fresh);
void vz_staff_draw(int colour, int opt, bool active, bool fresh);
void vz_chords_draw(int colour, int opt, bool active, bool fresh);
void vz_pulse_draw(int colour, int opt, bool active, bool fresh);
void vz_pulse_free(void);
void vz_spectrum_free(void);
void vz_roll_free(void);
void vz_staff_free(void);
void vz_fireworks_free(void);
void vz_kaleido_draw(int colour, int opt, bool active, bool fresh);
void vz_kaleido_free(void);
void vz_synthwave_draw(int colour, int opt, bool active, bool fresh);
void vz_synthwave_free(void);
void vz_spiro_draw(int colour, int opt, bool active, bool fresh);
void vz_spiro_free(void);
void vz_fireworks_draw(int colour, int opt, bool active, bool fresh);

/* note tracking shared by the visualisers: reports each note-on (even ones that also
 * ended since the last frame) and each note-off since the previous scan */
typedef struct {
    uint8_t held[16][16];     /* bitset of notes held at the last scan */
    uint8_t ons[16][128];     /* copy of g_mon.ons at the last scan */
} vz_scan_t;
typedef void (*vz_note_fn)(int ch, int note, int vel, bool on, void *ctx);
void vz_scan(vz_scan_t *s, vz_note_fn fn, void *ctx);
void vz_scan_reset(vz_scan_t *s);   /* start fresh: notes already sounding are reported as they restart */
