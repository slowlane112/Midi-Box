/*
 * Spirograph: neon spirograph curves (hypotrochoids) that slowly spin and redraw.
 * The chord being held shapes the curve: its lowest note sets the number of petals,
 * how many notes and whether it is minor set the loops and how pointed they are, so a
 * new chord morphs into a new star or flower. Each note adds a layer in its own colour
 * (pitch round the colour wheel), as bright as it was played, fading after release.
 * Drums make a ring of coloured dots bloom outwards. With nothing playing one dim
 * layer keeps turning with slowly changing colour.
 *
 * A sky of small twinkling stars sits behind the curves.
 *
 * Button 10: trails on/off (old curves fade slowly). Button 11: Rainbow, one colour, or
 * Cycle (each for 8 s, fading into the next).
 * Memory only while on screen.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "monitor.h"
#include "theme.h"
#include "gfx.h"
#include "viz_common.h"
#include "vz.h"

#define CX      (VW / 2)
#define CY      (VH / 2)
#define LAYERS  4
#define POINTS  420
#define TWO_PI  6.2831853f

typedef struct {
    uint8_t note, held;
    float level;               /* 0..1, brightness */
    float spin, rate, scale;
    uint8_t hue;
} layer_t;

#define STARS 70

typedef struct {
    layer_t ly[LAYERS + 1];    /* [LAYERS] = the idle layer */
    vz_scan_t scan;
    viz_star_t star[STARS];
} sp_mem_t;

static sp_mem_t *s_m;
static float s_R = 5, s_r = 2, s_d = 1.6f;      /* current shape */
static float s_tR = 5, s_tr = 2, s_td = 1.6f;   /* shape of the chord being held */
static float s_bloom = -1, s_bloom_a;
static float s_idle_hue;
static int s_next;
static uint32_t s_tick;

void vz_spiro_free(void)
{
    free(s_m);
    s_m = NULL;
}

static int gcd(int a, int b)
{
    while (b) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* the chord held now -> target shape */
static void chord_shape(void)
{
    int low = -1, count = 0;
    uint16_t pcs = 0;
    for (int n = 0; n < 128; n++) {
        bool held = false;
        for (int c = 0; c < 16 && !held; c++) {
            held = c != 9 && g_mon.vel[c][n];
        }
        if (held) {
            if (low < 0) {
                low = n;
            }
            if (!(pcs & (1u << (n % 12)))) {
                pcs |= (uint16_t)(1u << (n % 12));
                count++;
            }
        }
    }
    if (low < 0) {
        return;   /* nothing held: keep the last shape */
    }
    int root = low % 12;
    bool minor = (pcs & (1u << ((root + 3) % 12))) && !(pcs & (1u << ((root + 4) % 12)));
    int petals = 3 + root % 9;                  /* 3 .. 11 */
    int loops = 1 + (count - 1) % 3;            /* 1 .. 3 */
    while (gcd(petals, loops) != 1) {
        petals++;
    }
    s_tR = (float)(petals + loops);
    s_tr = (float)loops;
    s_td = (float)loops * (minor ? 0.6f : 0.95f);
}

static void on_note(int ch, int note, int vel, bool on, void *ctx)
{
    (void)ctx;
    if (ch == 9) {
        if (on) {
            s_bloom = 30;
            s_bloom_a = 255;
        }
        return;
    }
    if (!on) {
        for (int i = 0; i < LAYERS; i++) {
            if (s_m->ly[i].held && s_m->ly[i].note == note) {
                s_m->ly[i].held = 0;
            }
        }
        return;
    }
    layer_t *l = &s_m->ly[s_next];
    s_next = (s_next + 1) % LAYERS;
    l->note = (uint8_t)note;
    l->held = 1;
    l->level = 0.35f + 0.65f * (float)vel / 127.0f;
    l->hue = (uint8_t)((note % 12) * 256 / 12);
    l->scale = 0.62f + 0.38f * (float)((note / 12) % 4) / 3.0f;   /* octave: size */
    l->rate = 0.002f + 0.0015f * (float)(note % 5);
    if (note & 1) {
        l->rate = -l->rate;
    }
}

/* trails: fade every other row a quarter of the way to black each frame */
static void fade_rows(int parity)
{
    for (int y = parity; y < VH; y += 2) {
        uint16_t *p = &framebuffer_2[y * VW];
        for (int x = 0; x < VW; x++) {
            uint16_t c = p[x];
            if (!c) {
                continue;
            }
            uint16_t v = (uint16_t)((c >> 8) | (c << 8));
            int r = v >> 11, g = (v >> 5) & 63, b = v & 31;
            r = r * 3 / 4; g = g * 3 / 4; b = b * 3 / 4;
            v = (uint16_t)((r << 11) | (g << 5) | b);
            p[x] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
}

static void draw_layer(const layer_t *l, int colour, float bright)
{
    float R = s_R, r = s_r, d = s_d;
    float maxr = (R - r) + d;
    float sc = 62.0f * l->scale / maxr;
    float cs = cosf(l->spin), sn = sinf(l->spin);
    float k = (R - r) / r;
    float span = TWO_PI * r;   /* closes the curve for whole-number shapes */
    int px = 0, py = 0;
    /* colour choice: 0 Rainbow, 1..8 a standard colour, 9 Cycle (a blend of two of those) */
    int ca = colour, cb = colour;
    uint8_t ct = 0;
    if (colour >= 9) {
        viz_get_cycle(&ca, &cb, &ct);
    }
    uint16_t sa = ca ? viz_col(ca - 1) : 0, sb = cb ? viz_col(cb - 1) : 0;
    for (int i = 0; i <= POINTS; i++) {
        float t = span * (float)i / POINTS;
        float x = (R - r) * cosf(t) + d * cosf(k * t);
        float y = (R - r) * sinf(t) - d * sinf(k * t);
        float xr = x * cs - y * sn, yr = x * sn + y * cs;
        int qx = CX + (int)(xr * sc * 1.55f), qy = CY + (int)(yr * sc);
        if (i) {
            uint16_t rb = viz_rainbow((uint8_t)(l->hue + i * 48 / POINTS));
            uint16_t c = ca ? sa : rb;
            if (ct) {
                c = gfx_blend(cb ? sb : rb, c, ct);
            }
            c = gfx_blend(c, 0, (uint8_t)(bright * 255));
            viz_line(px, py, qx, qy, c);
            viz_px(qx + 1, qy, c);   /* a little thicker */
        }
        px = qx;
        py = qy;
    }
}

void vz_spiro_draw(int colour, int opt, bool active, bool fresh)
{
    if (!s_m) {
        if (!(s_m = viz_alloc(sizeof(sp_mem_t)))) {
            viz_clear(C_BG);
            return;
        }
        fresh = true;
    }
    if (fresh) {
        memset(s_m->ly, 0, sizeof(s_m->ly));
        vz_scan_reset(&s_m->scan);
        s_m->ly[LAYERS].scale = 0.85f;
        s_m->ly[LAYERS].rate = 0.004f;
        s_bloom = -1;
        viz_clear(0);
        viz_stars_make(s_m->star, STARS, 0x5EED1234);
    }
    vz_scan(&s_m->scan, on_note, NULL);
    chord_shape();
    /* morph towards the chord's shape */
    s_R += (s_tR - s_R) * 0.06f;
    s_r += (s_tr - s_r) * 0.06f;
    s_d += (s_td - s_d) * 0.06f;

    static int parity;
    if (opt) {
        fade_rows(parity);
        parity ^= 1;
    } else {
        viz_clear(0);
    }

    viz_stars_draw(s_m->star, STARS, ++s_tick, 0);

    bool any = false;
    for (int i = 0; i < LAYERS; i++) {
        layer_t *l = &s_m->ly[i];
        if (l->level <= 0.02f) {
            continue;
        }
        any = true;
        if (!l->held) {
            l->level *= 0.97f;   /* about 2 s to fade after release */
        }
        l->spin += l->rate;
        draw_layer(l, colour, l->level);
    }
    layer_t *idle = &s_m->ly[LAYERS];
    idle->spin += idle->rate;
    s_idle_hue += 0.3f;
    idle->hue = (uint8_t)s_idle_hue;
    float idle_level = any && active ? 0.25f : 0.45f;
    draw_layer(idle, colour, idle_level);

    /* drum bloom: a ring of dots spreading out */
    if (s_bloom >= 0) {
        uint8_t a = (uint8_t)s_bloom_a;
        for (int k = 0; k < 24; k++) {
            float ang = (float)k * (TWO_PI / 24.0f);
            int x = CX + (int)(cosf(ang) * s_bloom * 1.6f), y = CY + (int)(sinf(ang) * s_bloom);
            uint16_t c = gfx_blend(viz_rainbow((uint8_t)(k * 256 / 24)), 0, a);
            viz_fill(x - 1, y, 3, 1, c);
            viz_fill(x, y - 1, 1, 3, c);
        }
        s_bloom += 2.5f;
        s_bloom_a *= 0.93f;
        if (s_bloom > 140 || s_bloom_a < 10) {
            s_bloom = -1;
        }
    }
}
