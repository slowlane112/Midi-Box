/*
 * viz_common.h - shared by every screen-2 visualiser.
 *
 * Standard colours: 8 colours (the Display Colour palette) chosen with button 11.
 * viz_bg(0) is the main screen's black; viz_bg(1..7) are dark tints of each colour,
 * viz_bg2(i) a slightly brighter tint for background patterns.
 *
 * Cost budget: screen 2 is redrawn ~60 times a second and its SPI transfer takes
 * ~12 ms, so a visualiser should draw in well under 4 ms (about a million simple
 * pixel writes per second is fine; per-pixel float maths over the whole screen is not).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "v1_compat.h"
#include "v1_midi.h"

#define VW DISPLAY_2_WIDTH
#define VH DISPLAY_2_HEIGHT
#define VIZ_COLOURS 8
#define VIZ_CYCLE   8    /* colour index of button 11's Cycle choice: a fade between two colours */

uint16_t viz_col(int i);     /* standard colour i (0..7) */
uint16_t viz_bg(int i);      /* standard background i: 0 = main black */
uint16_t viz_bg2(int i);     /* brighter background tint for patterns */
void viz_clear(uint16_t c);  /* fill framebuffer_2 fast */
void viz_fill(int x, int y, int w, int h, uint16_t c);
static inline void viz_px(int x, int y, uint16_t c)
{
    if ((unsigned)x < VW && (unsigned)y < VH) {
        framebuffer_2[y * VW + x] = c;
    }
}

/* the data the visualisers read: per-channel note velocities (from the monitor) */
uint8_t viz_note_vel(int ch, int note);   /* ch -1 = any channel (highest velocity) */
uint8_t viz_hue_of_channel(int ch);       /* standard colour index for a channel */
uint16_t viz_rainbow(uint8_t pos);        /* 0..255 around the colour wheel */

/* working memory of a visualiser: claimed when it comes on screen (PSRAM first, zeroed),
 * freed when another one is picked, like the v1 visualisers */
void *viz_alloc(size_t n);

/* Cycle: viz.c sets which two choices it is between and how far (0..255) each frame;
 * viz_col / viz_bg / viz_bg2 (VIZ_CYCLE) give the blend of standard colours. Visualisers
 * with their own choices (themes, Rainbow) read the choice numbers back and blend themselves. */
void viz_set_cycle(int a, int b, uint8_t t);
void viz_get_cycle(int *a, int *b, uint8_t *t);

/* a sky of small twinkling stars (Fireworks, Spirograph) */
typedef struct {
    int16_t x, y;
    uint8_t lo, hi, period, phase;
    bool big, blue;
} viz_star_t;
void viz_stars_make(viz_star_t *s, int n, uint32_t seed);
void viz_stars_draw(const viz_star_t *s, int n, uint32_t tick, uint16_t bg);

void viz_quad(const float *x, const float *y, uint16_t c);   /* filled convex quadrilateral */
void viz_line(int x0, int y0, int x1, int y1, uint16_t c);
