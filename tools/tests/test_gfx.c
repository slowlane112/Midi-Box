#include <stdio.h>
#include <string.h>
#include "gfx.h"
static uint16_t px[320*40];
int main(void) {
    gfx_t g = { px, 320, 40 };
    gfx_clear(&g, 0);
    gfx_text(&g, &font_main, 2, 2, "Canon in D " UI_MIDDOT " 1:24/3:51 Quantise", 0xFFFF);
    gfx_text(&g, &font_small, 2, 18, "Strings ch 4 " UI_ENDASH " 9px small", 0xFFFF);
    gfx_text_box(&g, &font_main, 2, 28, 120, ALIGN_LEFT, "a_very_long_file_name_that_will_not_fit.mid", 0xFFFF);
    FILE *f = fopen("c.bin", "wb"); fwrite(px, 2, 320*40, f); fclose(f);
    return 0;
}
