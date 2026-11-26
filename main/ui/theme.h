/* theme.h - colours and layout constants (same values as tools/mockup.py). */
#pragma once
#include <stdint.h>
#include "gfx.h"

#define C_BG        RGB(0x0B, 0x0D, 0x10)
#define C_PANEL     RGB(0x16, 0x1A, 0x21)
#define C_LINE      RGB(0x2A, 0x31, 0x3C)
#define C_TEXT      RGB(0xE8, 0xED, 0xF2)
#define C_DIM       RGB(0x8E, 0x99, 0xA8)
#define C_REC       RGB(0xFF, 0x5A, 0x5F)
#define C_AMBER     RGB(0xFF, 0xB5, 0x47)
#define C_KEY_W     RGB(0xD9, 0xDE, 0xE5)
#define C_KEY_B     RGB(0x11, 0x15, 0x1B)
#define C_GREY      RGB(0x4B, 0x55, 0x63)
#define C_GOLD      RGB(0xFF, 0xD2, 0x4A)   /* stars, new best */
#define C_BLUE      RGB(0x4D, 0xA3, 0xFF)   /* Learn: left hand, "Good", notes the box plays */

/* accent follows the Display Colour setting */
extern uint16_t C_ACC;
extern uint16_t C_ACC_BG;   /* selected row background */
extern uint16_t C_ACC_DK;   /* clip fill */

#define THEME_ACCENTS 8
const char *theme_accent_name(int i);
void theme_apply(uint8_t accent);

/* screen 1 layout */
#define STATUS_H    18
#define CONTENT_Y   19
#define SOFT_Y      218
#define SOFT_H      22
#define SOFT_W      64
#define ROW_H       22
#define LIST_ROWS   9    /* (SOFT_Y - CONTENT_Y) / ROW_H */
