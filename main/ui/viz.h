/*
 * viz.h - screen-2 visualisers (list in viz.c).
 * Info: Piano, Spectrum, Staff, Chords, Piano Roll. Graphic: One, Two, Three, Four, Pulse, Fireworks.
 * Buttons: 9 = next visualiser (Shift+9 previous), 10 = option, 11 = colour.
 */
#pragma once
#include <stdbool.h>
#include "gfx.h"

void viz_init(void);
void viz_draw(gfx_t *g);
void viz_step(int dir);
void viz_option(int dir);       /* button 10: next option (dir 1) or previous (dir -1) */
void viz_colour(int dir);       /* button 11: next colour (dir 1) or previous (dir -1) */
const char *viz_name(void);
const char *viz_heading(void);   /* the heading shown now ("" when none) */
void viz_set_active(bool on);   /* a song is loaded / input is open (v1 "display active") */
/* extra text shown briefly on screen 2, e.g. "7 of 9 parts shown" */
void viz_set_caption(const char *text);
