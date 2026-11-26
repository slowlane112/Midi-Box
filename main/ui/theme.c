#include "theme.h"

uint16_t C_ACC;
uint16_t C_ACC_BG;
uint16_t C_ACC_DK;

static const struct { const char *name; uint8_t r, g, b; } s_acc[THEME_ACCENTS] = {
    { "Green",  0x3D, 0xDC, 0x84 },
    { "Blue",   0x4F, 0xA3, 0xFF },
    { "Amber",  0xFF, 0xB5, 0x47 },
    { "Purple", 0xC0, 0x84, 0xFC },
    { "Pink",   0xFF, 0x6F, 0xB5 },
    { "Cyan",   0x22, 0xD3, 0xEE },
    { "Red",    0xFF, 0x5A, 0x5F },
    { "White",  0xE8, 0xED, 0xF2 },
};

const char *theme_accent_name(int i) { return s_acc[i % THEME_ACCENTS].name; }

void theme_apply(uint8_t accent)
{
    accent %= THEME_ACCENTS;
    C_ACC = RGB(s_acc[accent].r, s_acc[accent].g, s_acc[accent].b);
    C_ACC_BG = gfx_blend(C_ACC, C_BG, 40);
    C_ACC_DK = gfx_blend(C_ACC, C_BG, 80);
}
