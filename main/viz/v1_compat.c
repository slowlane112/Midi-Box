#include <string.h>
#include "gfx.h"
#include "settings.h"
#include "v1_compat.h"

uint16_t *framebuffer_1;
uint16_t *framebuffer_2;

void display_2_draw(void) { }

void graphic_display_text(uint16_t *fb, const char *text, int y, int x, uint16_t fg, uint16_t bg)
{
    gfx_t g = { .px = fb, .w = fb == framebuffer_1 ? DISPLAY_1_WIDTH : DISPLAY_2_WIDTH,
                .h = fb == framebuffer_1 ? DISPLAY_1_HEIGHT : DISPLAY_2_HEIGHT };
    int w = gfx_text_width(&font_main, text);
    gfx_fill(&g, x - 2, y, w + 4, 16, bg);   /* v1 text cells were 16 px tall */
    gfx_text(&g, &font_main, x, y + 1, text, fg);
}

uint8_t nvs_get_value(nvs_id_t id, uint8_t def)
{
    (void)def;
    return id == DISPLAY_PIANO_KEY_ON_COLOR ? g_settings.viz_col[0] : g_settings.viz_opt[0];
}

bool nvs_get_value_bool(nvs_id_t id, bool def)
{
    return nvs_get_value(id, def ? 1 : 0) != 0;
}
