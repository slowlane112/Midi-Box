/*
 * Pulse: a flowing plasma in the spirit of the old Windows Media Player visualisations.
 * It speeds up and brightens with how much is playing, and every new note sends a ring
 * out from a point picked by its pitch. Rendered at half resolution (214 x 71) and
 * doubled, with integer maths and lookup tables only: about 15k pixels per frame.
 *
 * Button 10: ring shape: Auto (picked by the note's pitch), Circle, Square, Diamond, Triangle.
 * Button 11: colour: the plasma in shades of one of the 8 standard colours, Rainbow, or Cycle
 * (each for 8 s, fading into the next).
 */
#include <stdlib.h>
#include <string.h>
#include "monitor.h"
#include "theme.h"
#include "viz_common.h"
#include "vz.h"

#define PW (VW / 2)
#define PH (VH / 2)
#define RINGS 10

typedef struct {
    int16_t x, y;
    uint16_t r;            /* radius in 1/4 px */
    uint8_t life, hue;
    uint8_t shape;         /* SH_* */
} ring_t;

enum { SH_CIRCLE, SH_SQUARE, SH_DIAMOND, SH_TRIANGLE };
static int s_shape_opt;    /* button 10: 0 Auto, 1.. a fixed shape */

/* working memory, only while Pulse is on screen */
typedef struct {
    int8_t sin[256];
    uint16_t pal[256];
    ring_t ring[RINGS];
    vz_scan_t scan;
    uint8_t dist[PW * PH];   /* distance from the centre, per half-res pixel */
} pulse_mem_t;
static pulse_mem_t *s_m;
#define s_sin  (s_m->sin)
#define s_pal  (s_m->pal)
#define s_ring (s_m->ring)
#define s_scan (s_m->scan)
#define s_dist (s_m->dist)
static int s_pal_key = -1;
static uint32_t s_t1, s_t2, s_t3;
static int s_energy;       /* 0..255 smoothed */

static uint8_t isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 14;
    while (b > v) {
        b >>= 2;
    }
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r > 255 ? 255 : (uint8_t)r;
}

/* 1 = ready, 2 = just claimed (start fresh), 0 = no memory */
static int init(void)
{
    if (s_m) {
        return 1;
    }
    if (!(s_m = viz_alloc(sizeof(pulse_mem_t)))) {
        return 0;
    }
    s_pal_key = -1;
    /* sine table without libm: a parabola approximation is plenty for colour waves */
    for (int i = 0; i < 256; i++) {
        int x = i & 127;                       /* half wave */
        int v = (x * (128 - x)) * 127 / 4096;  /* 0..127 */
        s_sin[i] = (int8_t)(i < 128 ? v : -v);
    }
    for (int y = 0; y < PH; y++) {
        for (int x = 0; x < PW; x++) {
            int dx = x - PW / 2, dy = (y - PH / 2) * 2;
            s_dist[y * PW + x] = isqrt((uint32_t)(dx * dx + dy * dy) * 4);
        }
    }
    return 2;
}

void vz_pulse_free(void)
{
    free(s_m);
    s_m = NULL;
    s_pal_key = -1;
}

static uint16_t mix(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, int a)
{
    return RGB((uint8_t)(r1 + (r2 - r1) * a / 255), (uint8_t)(g1 + (g2 - g1) * a / 255),
               (uint8_t)(b1 + (b2 - b1) * a / 255));
}

/* one palette: choice 0..7 = shades of that standard colour, 8 = rainbow */
static void make_palette(int choice, uint16_t *pal)
{
    /* standard colour as RGB, read back from the swapped 565 value */
    uint16_t c = viz_col(choice);
    c = (uint16_t)((c >> 8) | (c << 8));
    uint8_t cr = (uint8_t)((c >> 11) << 3), cg = (uint8_t)(((c >> 5) & 63) << 2), cb = (uint8_t)((c & 31) << 3);
    for (int i = 0; i < 256; i++) {
        uint16_t v;
        if (choice == 8) {
            v = viz_rainbow((uint8_t)i);
            v = gfx_blend(v, C_BG, (uint8_t)(i < 40 ? i * 2 : 80 + (i - 40) * 175 / 215));
        } else {
            /* aurora: black -> deep colour -> colour -> pale highlight */
            int k = i;
            if (k < 150) {
                v = mix(0x0B, 0x0D, 0x10, (uint8_t)(cr / 3), (uint8_t)(cg / 3), (uint8_t)(cb / 3), k * 255 / 149);
            } else if (k < 220) {
                v = mix((uint8_t)(cr / 3), (uint8_t)(cg / 3), (uint8_t)(cb / 3), cr, cg, cb, (k - 150) * 255 / 69);
            } else {
                v = mix(cr, cg, cb, (uint8_t)((cr + 255) / 2), (uint8_t)((cg + 255) / 2),
                        (uint8_t)((cb + 255) / 2), (k - 220) * 255 / 35);
            }
        }
        pal[i] = v;
    }
}

static void build_palette(int colour)
{
    if (colour < 9) {
        if (colour != s_pal_key) {
            s_pal_key = colour;
            make_palette(colour, s_pal);
        }
        return;
    }
    /* Cycle: blend the two palettes it is between (changes every frame) */
    int a, b;
    uint8_t t;
    viz_get_cycle(&a, &b, &t);
    uint16_t pb[256];
    make_palette(a, s_pal);
    if (t) {
        make_palette(b, pb);
        for (int i = 0; i < 256; i++) {
            s_pal[i] = gfx_blend(pb[i], s_pal[i], t);
        }
    }
    s_pal_key = -1;
}

/* where a ring's band (lo..hi from its centre) crosses a row dy away: |dx| from inner to
 * outer, or the whole -outer..outer when inner < 0; false if the row misses it */
static bool ring_span(int shape, int dy, int lo, int hi, int *inner, int *outer)
{
    int ady = dy < 0 ? -dy : dy;
    switch (shape) {
    case SH_SQUARE:
        if (ady > hi) {
            return false;
        }
        *outer = hi;
        *inner = ady >= lo ? -1 : lo;
        return true;
    case SH_DIAMOND: {   /* |dx| + |dy| from lo * 1.41 to hi * 1.41 */
        int o = hi * 362 / 256 - ady, in = lo * 362 / 256 - ady;
        if (o < 0) {
            return false;
        }
        *outer = o;
        *inner = in <= 0 ? -1 : in;
        return true;
    }
    case SH_TRIANGLE: {  /* point up (rows count down): max(0.87 |dx| - dy / 2, dy) */
        dy = -dy;
        if (-dy > hi) {
            return false;
        }
        int o = (hi - dy / 2) * 296 / 256;
        if (o < 0) {
            return false;
        }
        int in = (lo - dy / 2) * 296 / 256;
        *outer = o;
        *inner = (-dy >= lo || in <= 0) ? -1 : in;
        return true;
    }
    default:
        if (ady > hi) {
            return false;
        }
        *outer = isqrt((uint32_t)(hi * hi - dy * dy));
        *inner = lo * lo > dy * dy ? isqrt((uint32_t)(lo * lo - dy * dy)) : -1;
        return true;
    }
}

static void on_note(int ch, int note, int vel, bool on, void *ctx)
{
    (void)ctx;
    if (!on) {
        return;
    }
    int best = 0;
    for (int i = 1; i < RINGS; i++) {
        if (s_ring[i].life < s_ring[best].life) {
            best = i;
        }
    }
    ring_t *r = &s_ring[best];
    r->x = (int16_t)(8 + (note % 48) * (PW - 16) / 47);
    r->y = (int16_t)(ch == 9 ? PH / 2 : 8 + ((note * 37) % (PH - 16)));
    r->r = 0;
    r->life = (uint8_t)(60 + vel);
    r->hue = (uint8_t)(note * 21);
    r->shape = (uint8_t)(s_shape_opt ? s_shape_opt - 1 : note % 4);
}

void vz_pulse_draw(int colour, int opt, bool active, bool fresh)
{
    (void)active;
    int st = init();
    if (!st) {
        viz_clear(C_BG);
        return;
    }
    if (fresh || st == 2) {
        memset(s_ring, 0, sizeof(s_ring));
        vz_scan_reset(&s_scan);
    }
    s_shape_opt = opt % 5;
    build_palette(colour);
    vz_scan(&s_scan, on_note, NULL);

    int e = 0;
    for (int c = 0; c < 16; c++) {
        e += g_mon.level[c];
    }
    e = e > 300 ? 255 : e * 255 / 300;
    s_energy += (e - s_energy) / (e > s_energy ? 3 : 12);

    /* phases move faster with more music */
    int speed = 6 + s_energy / 8;
    s_t1 += (uint32_t)speed;
    s_t2 += (uint32_t)(speed * 3 / 4);
    s_t3 += (uint32_t)(speed * 5 / 4);
    uint8_t a = (uint8_t)(s_t1 >> 3), b = (uint8_t)(s_t2 >> 3), cph = (uint8_t)(s_t3 >> 2);
    int bright = 150 + s_energy * 105 / 255;   /* scales the wave height into the palette */

    for (int i = 0; i < RINGS; i++) {
        if (s_ring[i].life) {
            s_ring[i].r = (uint16_t)(s_ring[i].r + 5);
            s_ring[i].life = (uint8_t)(s_ring[i].life > 3 ? s_ring[i].life - 3 : 0);
        }
    }

    uint32_t *fb = (uint32_t *)framebuffer_2;
    int16_t idxrow[PW];
    for (int y = 0; y < PH; y++) {
        int8_t sy = s_sin[(uint8_t)(y * 5 + b)];
        int8_t sy2 = s_sin[(uint8_t)(y * 3 - a)];
        const uint8_t *drow = &s_dist[y * PW];
        for (int x = 0; x < PW; x++) {
            int v = s_sin[(uint8_t)(x * 3 + a)] + sy + s_sin[(uint8_t)(x * 2 + sy2 + b)] +
                    s_sin[(uint8_t)(drow[x] * 2 - cph)];          /* -508..508 */
            idxrow[x] = (int16_t)((v + 512) * bright >> 10);
        }
        /* rings: brighten only the spans of this row that cross each ring */
        for (int i = 0; i < RINGS; i++) {
            const ring_t *r = &s_ring[i];
            if (!r->life) {
                continue;
            }
            int dy = y - r->y, rr = r->r / 4, lo = rr - 2, hi = rr + 2;   /* true shapes: same scale both ways */
            int inner, outer;
            if (lo <= 0 || !ring_span(r->shape, dy, lo, hi, &inner, &outer)) {
                continue;
            }
            int add = r->life * 3 / 4;
            for (int side = -1; side <= 1; side += 2) {
                int x0 = inner < 0 ? r->x - outer : (side < 0 ? r->x - outer : r->x + inner + 1);
                int x1 = inner < 0 ? r->x + outer : (side < 0 ? r->x - inner - 1 : r->x + outer);
                if (x0 < 0) {
                    x0 = 0;
                }
                if (x1 >= PW) {
                    x1 = PW - 1;
                }
                for (int x = x0; x <= x1; x++) {
                    idxrow[x] = (int16_t)(idxrow[x] + add);
                }
                if (inner < 0) {
                    break;
                }
            }
        }
        uint32_t *row0 = &fb[(y * 2) * (VW / 2)];
        uint32_t *row1 = row0 + VW / 2;
        for (int x = 0; x < PW; x++) {
            int idx = idxrow[x] > 255 ? 255 : idxrow[x];
            uint32_t p = s_pal[idx];
            p |= p << 16;
            row0[x] = p;
            row1[x] = p;
        }
    }
}
