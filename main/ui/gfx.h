/*
 * gfx.h - drawing into an RGB565 framebuffer. Same primitives as tools/gfx.py,
 * so tools/mockup.py renders exactly what these draw.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "font.h"
#include "icons.h"

/* RGB565 with the bytes swapped for the panels */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define SWAP16(c)       ((uint16_t)(((c) >> 8) | ((c) << 8)))
#define RGB(r, g, b)    SWAP16(RGB565(r, g, b))

typedef struct {
    uint16_t *px;
    int w, h;
} gfx_t;

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } gfx_align_t;

void gfx_clear(gfx_t *g, uint16_t c);
void gfx_fill(gfx_t *g, int x, int y, int w, int h, uint16_t c);
void gfx_rect(gfx_t *g, int x, int y, int w, int h, uint16_t c);
void gfx_hline(gfx_t *g, int x, int y, int w, uint16_t c);
void gfx_vline(gfx_t *g, int x, int y, int h, uint16_t c);
void gfx_pixel(gfx_t *g, int x, int y, uint16_t c);
void gfx_circle(gfx_t *g, int cx, int cy, int r, uint16_t c);
void gfx_tri_right(gfx_t *g, int x, int y, int h, uint16_t c);
void gfx_tri_left(gfx_t *g, int x, int y, int h, uint16_t c);

int gfx_text_width(const font_t *f, const char *s);
/* Draw s with its line top at y. Returns the drawn width. */
int gfx_text(gfx_t *g, const font_t *f, int x, int y, const char *s, uint16_t c);
/* Draw into a box of width w: cut with an ellipsis if too long, then align. */
int gfx_text_box(gfx_t *g, const font_t *f, int x, int y, int w, gfx_align_t a, const char *s, uint16_t c);
/* Right-aligned at x (x is the right edge). */
int gfx_text_right(gfx_t *g, const font_t *f, int x, int y, const char *s, uint16_t c);

void gfx_icon(gfx_t *g, const icon_t *ic, int x, int y, uint16_t c);

uint16_t gfx_blend(uint16_t a, uint16_t b, uint8_t alpha);  /* alpha 0..255 of a over b, swapped colours */
