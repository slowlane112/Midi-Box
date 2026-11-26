/*
 * font.h - 1-bit proportional bitmap fonts made by tools/fontgen.py.
 * Glyph bits are a continuous MSB-first stream, w*h bits per glyph.
 */
#pragma once
#include <stdint.h>

typedef struct {
    uint16_t offset;   /* byte offset into bitmap */
    uint8_t w, h;      /* ink box */
    int8_t x, y;       /* ink box position from pen x / line top */
    uint8_t adv;       /* pen advance */
} font_glyph_t;

typedef struct {
    const uint8_t *bitmap;
    const font_glyph_t *glyphs;
    uint8_t first, last;
    uint8_t line_height;
    uint8_t ascent;
} font_t;

/* Extra glyphs above ASCII, use in strings as e.g. "a" UI_MIDDOT "b" */
#define UI_MIDDOT   "\x80"
#define UI_ELLIPSIS "\x81"
#define UI_DEGREE   "\x82"
#define UI_ENDASH   "\x83"
#define UI_DOWN     "\x84"   /* ▼ */
#define UI_UP       "\x85"   /* ▲ */

extern const font_t font_small;   /* DejaVu Sans 9px,  line 12 */
extern const font_t font_main;    /* DejaVu Sans Bold 11px, line 14 */
extern const font_t font_large;   /* DejaVu Sans Bold 16px, line 19 */
extern const font_t font_huge;    /* DejaVu Sans Bold 28px, line 33 */
