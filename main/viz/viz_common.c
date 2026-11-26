#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "theme.h"
#include "monitor.h"
#include "viz_common.h"

/* same order as the Display Colour setting (theme.c) */
/* saturated: one or two of R, G, B strong, the rest near zero (2.1; 2.0 had pastels) */
static const uint8_t s_rgb[VIZ_COLOURS][3] = {
    { 0x00, 0xE6, 0x30 }, { 0x00, 0x5C, 0xFF }, { 0xFF, 0x9A, 0x00 }, { 0x9A, 0x30, 0xFF },
    { 0xFF, 0x1A, 0x8C }, { 0x00, 0xD8, 0xFF }, { 0xFF, 0x12, 0x12 }, { 0xFF, 0xFF, 0xFF },
};
/* the Cycle colour (index VIZ_CYCLE): a blend of two standard colours, set every frame */
static int s_cyc_a, s_cyc_b;
static uint8_t s_cyc_t;
static uint8_t s_cyc_rgb[3];
static int s_cyc_white;   /* 0..100: how much of the blend is White (darker backgrounds) */

void viz_set_cycle(int a, int b, uint8_t t)
{
    s_cyc_a = a;
    s_cyc_b = b;
    s_cyc_t = t;
    int ia = ((a % VIZ_COLOURS) + VIZ_COLOURS) % VIZ_COLOURS, ib = ((b % VIZ_COLOURS) + VIZ_COLOURS) % VIZ_COLOURS;
    for (int k = 0; k < 3; k++) {
        s_cyc_rgb[k] = (uint8_t)(s_rgb[ia][k] + (s_rgb[ib][k] - s_rgb[ia][k]) * t / 255);
    }
    s_cyc_white = ((ia == 7) * (255 - t) + (ib == 7) * t) * 100 / 255;
}

void viz_get_cycle(int *a, int *b, uint8_t *t)
{
    *a = s_cyc_a;
    *b = s_cyc_b;
    *t = s_cyc_t;
}

static const uint8_t *rgb_of(int i)
{
    if (i == VIZ_CYCLE) {
        return s_cyc_rgb;
    }
    i = ((i % VIZ_COLOURS) + VIZ_COLOURS) % VIZ_COLOURS;
    return s_rgb[i];
}

/* backgrounds: the colour itself, dark (about 24 %, white 16 %), no grey mixed in */
static uint16_t dark(int i, int pct)
{
    int white = i == VIZ_CYCLE ? s_cyc_white : (i == 7 ? 100 : 0);
    pct = pct - pct * white / 300;
    const uint8_t *c = rgb_of(i);
    return RGB(c[0] * pct / 100, c[1] * pct / 100, c[2] * pct / 100);
}

uint16_t viz_col(int i)
{
    const uint8_t *c = rgb_of(i);
    return RGB(c[0], c[1], c[2]);
}

uint16_t viz_bg(int i)
{
    if (i != VIZ_CYCLE) {
        i = ((i % VIZ_COLOURS) + VIZ_COLOURS) % VIZ_COLOURS;
    }
    return i == 0 ? C_BG : dark(i, 24);
}

uint16_t viz_bg2(int i)
{
    if (i != VIZ_CYCLE) {
        i = ((i % VIZ_COLOURS) + VIZ_COLOURS) % VIZ_COLOURS;
    }
    return i == 0 ? C_PANEL : dark(i, 40);
}

void viz_clear(uint16_t c)
{
    uint32_t v = ((uint32_t)c << 16) | c;
    uint32_t *p = (uint32_t *)framebuffer_2;
    for (int i = 0; i < VW * VH / 2; i++) {
        p[i] = v;
    }
}

void viz_fill(int x, int y, int w, int h, uint16_t c)
{
    gfx_t g = { framebuffer_2, VW, VH };
    gfx_fill(&g, x, y, w, h, c);
}

uint8_t viz_note_vel(int ch, int note)
{
    if (ch >= 0) {
        return g_mon.vel[ch & 15][note & 127];
    }
    uint8_t v = 0;
    for (int c = 0; c < 16; c++) {
        if (g_mon.vel[c][note & 127] > v) {
            v = g_mon.vel[c][note & 127];
        }
    }
    return v;
}

uint8_t viz_hue_of_channel(int ch)
{
    static const uint8_t map[16] = { 0, 1, 2, 3, 4, 5, 6, 0, 1, 7, 2, 3, 4, 5, 6, 1 };   /* ch 10 drums = white */
    return map[ch & 15];
}

uint16_t viz_rainbow(uint8_t pos)
{
    /* cheap HSV wheel at full saturation */
    uint8_t r, g, b, seg = pos / 43, off = (uint8_t)((pos - seg * 43) * 6);
    switch (seg) {
    case 0: r = 255; g = off; b = 0; break;
    case 1: r = 255 - off; g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = off; break;
    case 3: r = 0; g = 255 - off; b = 255; break;
    case 4: r = off; g = 0; b = 255; break;
    default: r = 255; g = 0; b = 255 - off; break;
    }
    return RGB(r, g, b);
}

void *viz_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!p) {
        p = malloc(n);
    }
    if (p) {
        memset(p, 0, n);
    }
    return p;
}

void viz_quad(const float *x, const float *y, uint16_t c)
{
    float y0 = y[0], y1 = y[0];
    for (int i = 1; i < 4; i++) {
        y0 = y[i] < y0 ? y[i] : y0;
        y1 = y[i] > y1 ? y[i] : y1;
    }
    int ya = (int)(y0 + 0.5f), yb = (int)(y1 - 0.5f);
    ya = ya < 0 ? 0 : ya;
    yb = yb >= VH ? VH - 1 : yb;
    for (int py = ya; py <= yb; py++) {
        float fy = (float)py + 0.5f, lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) & 3;
            float ay = y[i], by = y[j];
            if ((fy >= ay && fy < by) || (fy >= by && fy < ay)) {
                float fx = x[i] + (fy - ay) * (x[j] - x[i]) / (by - ay);
                lo = fx < lo ? fx : lo;
                hi = fx > hi ? fx : hi;
            }
        }
        int xa = (int)(lo + 0.5f), xb = (int)(hi - 0.5f);
        xa = xa < 0 ? 0 : xa;
        xb = xb >= VW ? VW - 1 : xb;
        uint16_t *row = &framebuffer_2[py * VW];
        for (int px = xa; px <= xb; px++) {
            row[px] = c;
        }
    }
}

void viz_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int n = 0; n < 2000; n++) {
        viz_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void viz_stars_make(viz_star_t *st, int n, uint32_t r)
{
    for (int i = 0; i < n; i++) {
        r = r * 1664525u + 1013904223u; st[i].x = (int16_t)((r >> 8) % VW);
        r = r * 1664525u + 1013904223u; st[i].y = (int16_t)((r >> 8) % (VH - 6));
        r = r * 1664525u + 1013904223u;
        st[i].big = (r >> 24) < 20;                    /* ~8% */
        st[i].blue = ((r >> 16) & 3) == 0;
        st[i].lo = (uint8_t)(40 + ((r >> 8) & 31));
        st[i].hi = (uint8_t)(st[i].big ? 230 : 110 + ((r >> 4) & 63));
        r = r * 1664525u + 1013904223u;
        st[i].period = (uint8_t)(90 + (r >> 8) % 160);
        st[i].phase = (uint8_t)(r >> 24);
    }
}

void viz_stars_draw(const viz_star_t *st, int n, uint32_t tick, uint16_t bg)
{
    uint16_t cool = RGB(0xC8, 0xD8, 0xFF);
    for (int i = 0; i < n; i++) {
        const viz_star_t *s = &st[i];
        uint32_t t = (tick + (uint32_t)s->phase * s->period / 256) % s->period;
        uint32_t half = s->period / 2;
        uint32_t tri = t < half ? t * 255 / half : (s->period - t) * 255 / half;   /* 0..255..0 */
        uint8_t a = (uint8_t)(s->lo + (s->hi - s->lo) * tri / 255);
        uint16_t c = gfx_blend(s->blue ? cool : 0xFFFF, bg, a);
        viz_px(s->x, s->y, c);
        if (s->big) {
            uint16_t c2 = gfx_blend(s->blue ? cool : 0xFFFF, bg, (uint8_t)(a / 3));
            viz_px(s->x - 1, s->y, c2);
            viz_px(s->x + 1, s->y, c2);
            viz_px(s->x, s->y - 1, c2);
            viz_px(s->x, s->y + 1, c2);
        }
    }
}
