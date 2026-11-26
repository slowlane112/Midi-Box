#include <string.h>
#include "gfx.h"

void gfx_clear(gfx_t *g, uint16_t c)
{
    int n = g->w * g->h;
    if ((c & 0xFF) == (c >> 8)) {
        memset(g->px, c & 0xFF, (size_t)n * 2);
        return;
    }
    for (int i = 0; i < n; i++) {
        g->px[i] = c;
    }
}

void gfx_fill(gfx_t *g, int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g->w) { w = g->w - x; }
    if (y + h > g->h) { h = g->h - y; }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = g->px + yy * g->w + x;
        for (int i = 0; i < w; i++) {
            row[i] = c;
        }
    }
}

void gfx_hline(gfx_t *g, int x, int y, int w, uint16_t c) { gfx_fill(g, x, y, w, 1, c); }
void gfx_vline(gfx_t *g, int x, int y, int h, uint16_t c) { gfx_fill(g, x, y, 1, h, c); }

void gfx_rect(gfx_t *g, int x, int y, int w, int h, uint16_t c)
{
    gfx_hline(g, x, y, w, c);
    gfx_hline(g, x, y + h - 1, w, c);
    gfx_vline(g, x, y, h, c);
    gfx_vline(g, x + w - 1, y, h, c);
}

void gfx_pixel(gfx_t *g, int x, int y, uint16_t c)
{
    if ((unsigned)x < (unsigned)g->w && (unsigned)y < (unsigned)g->h) {
        g->px[y * g->w + x] = c;
    }
}

void gfx_circle(gfx_t *g, int cx, int cy, int r, uint16_t c)
{
    for (int yy = -r; yy <= r; yy++) {
        for (int xx = -r; xx <= r; xx++) {
            if (xx * xx + yy * yy <= r * r + r) {
                gfx_pixel(g, cx + xx, cy + yy, c);
            }
        }
    }
}

void gfx_tri_right(gfx_t *g, int x, int y, int h, uint16_t c)
{
    int half = h / 2;
    for (int i = 0; i < h; i++) {
        int w = (i <= half ? i : h - 1 - i) + 1;
        gfx_hline(g, x, y + i, w, c);
    }
}

void gfx_tri_left(gfx_t *g, int x, int y, int h, uint16_t c)
{
    int half = h / 2;
    for (int i = 0; i < h; i++) {
        int w = (i <= half ? i : h - 1 - i) + 1;
        gfx_hline(g, x + half + 1 - w, y + i, w, c);
    }
}

static const font_glyph_t *glyph(const font_t *f, uint8_t ch)
{
    if (ch < f->first || ch > f->last || f->glyphs[ch - f->first].adv == 0) {
        ch = '?';
    }
    return &f->glyphs[ch - f->first];
}

int gfx_text_width(const font_t *f, const char *s)
{
    int w = 0;
    for (; *s; s++) {
        w += glyph(f, (uint8_t)*s)->adv;
    }
    return w;
}

int gfx_text(gfx_t *g, const font_t *f, int x, int y, const char *s, uint16_t c)
{
    int x0 = x;
    for (; *s; s++) {
        const font_glyph_t *gl = glyph(f, (uint8_t)*s);
        const uint8_t *bits = f->bitmap + gl->offset;
        int bit = 0;
        for (int r = 0; r < gl->h; r++) {
            int py = y + gl->y + r;
            for (int col = 0; col < gl->w; col++, bit++) {
                if (bits[bit >> 3] & (0x80 >> (bit & 7))) {
                    gfx_pixel(g, x + gl->x + col, py, c);
                }
            }
        }
        x += gl->adv;
    }
    return x - x0;
}

int gfx_text_box(gfx_t *g, const font_t *f, int x, int y, int w, gfx_align_t a, const char *s, uint16_t c)
{
    int tw = gfx_text_width(f, s);
    char buf[160];
    const char *out = s;
    if (tw > w) {
        int ell = glyph(f, 0x81)->adv;
        int n = 0, acc = 0;
        while (s[n] && n < (int)sizeof(buf) - 2) {
            int adv = glyph(f, (uint8_t)s[n])->adv;
            if (acc + adv + ell > w) {
                break;
            }
            acc += adv;
            n++;
        }
        memcpy(buf, s, (size_t)n);
        buf[n] = (char)0x81;
        buf[n + 1] = '\0';
        out = buf;
        tw = acc + ell;
    }
    if (a == ALIGN_CENTER) {
        x += (w - tw) / 2;
    } else if (a == ALIGN_RIGHT) {
        x += w - tw;
    }
    return gfx_text(g, f, x, y, out, c);
}

int gfx_text_right(gfx_t *g, const font_t *f, int x, int y, const char *s, uint16_t c)
{
    return gfx_text(g, f, x - gfx_text_width(f, s), y, s, c);
}

void gfx_icon(gfx_t *g, const icon_t *ic, int x, int y, uint16_t c)
{
    int bpr = (ic->w + 7) / 8;
    for (int r = 0; r < ic->h; r++) {
        const uint8_t *row = ic->bits + r * bpr;
        for (int col = 0; col < ic->w; col++) {
            if (row[col >> 3] & (0x80 >> (col & 7))) {
                gfx_pixel(g, x + col, y + r, c);
            }
        }
    }
}

uint16_t gfx_blend(uint16_t a, uint16_t b, uint8_t alpha)
{
    a = SWAP16(a);
    b = SWAP16(b);
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    int r = (ar * alpha + br * (255 - alpha)) / 255;
    int gg = (ag * alpha + bg * (255 - alpha)) / 255;
    int bl = (ab * alpha + bb * (255 - alpha)) / 255;
    return SWAP16((uint16_t)((r << 11) | (gg << 5) | bl));
}
