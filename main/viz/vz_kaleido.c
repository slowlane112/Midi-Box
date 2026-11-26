/*
 * Kaleidoscope: the screen mirrored into 6, 8 or 12 slices around the centre, slowly
 * turning. Every note drops a diamond shard into the pattern: its colour is the pitch
 * (C red round the colour wheel to B), its size the velocity. Shards drift outwards,
 * spin and fade over about 2 s. Drums send a bright ring out from the centre (the kick
 * also flashes the background), and the background rings ripple all the time. Busy
 * music turns it faster. With nothing playing it keeps turning, with an odd faint shard.
 *
 * Button 10: 6 / 8 / 12 slices. Button 11: background (black or a dark colour).
 * Memory (distance table + shards) only while on screen.
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
#define PW      (VW / 2)       /* background drawn at half resolution */
#define PH      (VH / 2)
#define SHARDS  32
#define LIFE    120            /* frames */
#define TWO_PI  6.2831853f

typedef struct {
    float r, th, vr;           /* distance from the centre, angle inside the slice, speed */
    float ang, spin;           /* the shard's own turn */
    float size;
    uint16_t col;
    uint8_t life;
} shard_t;

typedef struct {
    uint8_t dist[PW * PH];     /* distance from the centre of each half-res pixel */
    shard_t sh[SHARDS];
    vz_scan_t scan;
} kal_mem_t;

static kal_mem_t *s_m;
static float s_rot, s_phase, s_ring_r = -1, s_ring_a, s_flash;
static int s_next, s_idle;
static uint32_t s_rng = 0xC0FFEE;

static float frand(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (float)(s_rng & 0xFFFF) / 65536.0f;
}

void vz_kaleido_free(void)
{
    free(s_m);
    s_m = NULL;
}

static void add_shard(int note, int vel, bool faint)
{
    shard_t *s = &s_m->sh[s_next];
    s_next = (s_next + 1) % SHARDS;
    s->r = 4.0f + frand() * 14.0f;
    s->th = frand();                           /* 0..1 of half a slice, set when drawn */
    s->vr = 0.35f + frand() * 0.5f;
    s->ang = frand() * TWO_PI;
    s->spin = (frand() - 0.5f) * 0.12f;
    s->size = 5.0f + (float)vel / 127.0f * 11.0f;
    uint16_t c = viz_rainbow((uint8_t)((note % 12) * 256 / 12));
    s->col = faint ? gfx_blend(c, C_BG, 110) : c;
    s->life = LIFE;
}

static void on_note(int ch, int note, int vel, bool on, void *ctx)
{
    (void)ctx;
    if (!on) {
        return;
    }
    if (ch == 9) {
        s_ring_r = 2;
        s_ring_a = 255;
        if (note == 35 || note == 36) {
            s_flash = 1.0f;
        }
        return;
    }
    add_shard(note, vel, false);
}

static uint8_t isqrt16(uint32_t v)
{
    uint32_t r = 0, b = 1u << 16;
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

void vz_kaleido_draw(int colour, int opt, bool active, bool fresh)
{
    if (!s_m) {
        if (!(s_m = viz_alloc(sizeof(kal_mem_t)))) {
            viz_clear(C_BG);
            return;
        }
        for (int y = 0; y < PH; y++) {
            for (int x = 0; x < PW; x++) {
                int dx = 2 * x + 1 - CX, dy = 2 * y + 1 - CY;
                s_m->dist[y * PW + x] = isqrt16((uint32_t)(dx * dx + dy * dy));
            }
        }
        fresh = true;
    }
    if (fresh) {
        memset(s_m->sh, 0, sizeof(s_m->sh));
        vz_scan_reset(&s_m->scan);
        s_ring_r = -1;
        s_flash = 0;
    }
    vz_scan(&s_m->scan, on_note, NULL);

    int e = 0;
    for (int c = 0; c < 16; c++) {
        e += g_mon.level[c];
    }
    float energy = e > 300 ? 1.0f : (float)e / 300.0f;
    if (!active || energy < 0.02f) {
        if (++s_idle >= 50) {   /* keep it alive: a faint shard now and then */
            s_idle = 0;
            add_shard((int)(frand() * 12), 60, true);
        }
    } else {
        s_idle = 0;
    }
    s_rot += 0.004f + energy * 0.02f;
    s_phase += 0.35f + energy * 0.6f;

    /* background: rippling rings between the dark tint and a brighter one, plus the
     * drum ring, through a table indexed by distance */
    uint16_t lo = colour == 0 ? C_BG : viz_bg(colour);
    uint16_t hi = colour == 0 ? RGB(0x1C, 0x18, 0x2C) : viz_bg2(colour);
    if (s_flash > 0.01f) {
        hi = gfx_blend(0xFFFF, hi, (uint8_t)(s_flash * 70));
        lo = gfx_blend(hi, lo, (uint8_t)(s_flash * 120));
        s_flash *= 0.85f;
    }
    uint16_t tab[256];
    for (int d = 0; d < 256; d++) {
        float w = sinf(((float)d - s_phase) * (TWO_PI / 18.0f)) * 0.5f + 0.5f;
        uint16_t c = gfx_blend(hi, lo, (uint8_t)(w * 255));
        if (s_ring_r >= 0) {
            float k = fabsf((float)d - s_ring_r);
            if (k < 3.0f) {
                c = gfx_blend(RGB(0xFF, 0xF0, 0xC8), c, (uint8_t)(s_ring_a * (1.0f - k / 3.0f)));
            }
        }
        tab[d] = c;
    }
    if (s_ring_r >= 0) {
        s_ring_r += 3.0f;
        s_ring_a *= 0.95f;
        if (s_ring_r > 240 || s_ring_a < 8) {
            s_ring_r = -1;
        }
    }
    const uint8_t *dist = s_m->dist;
    for (int y = 0; y < PH; y++) {
        uint32_t *r0 = (uint32_t *)&framebuffer_2[(2 * y) * VW];
        uint32_t *r1 = (uint32_t *)&framebuffer_2[(2 * y + 1) * VW];
        const uint8_t *drow = &dist[y * PW];
        for (int x = 0; x < PW; x++) {
            uint16_t c = tab[drow[x]];
            uint32_t two = ((uint32_t)c << 16) | c;
            r0[x] = two;
            r1[x] = two;
        }
    }

    /* shards, mirrored into every slice */
    static const int slices_of[3] = { 6, 8, 12 };
    int n = slices_of[opt % 3];
    float seg = TWO_PI / (float)n;
    float shrink = 8.0f / (float)n < 1.0f ? 8.0f / (float)n : 1.0f;
    float bc[12], bs[12];
    for (int k = 0; k < n; k++) {
        bc[k] = cosf((float)k * seg + s_rot);
        bs[k] = sinf((float)k * seg + s_rot);
    }
    for (int i = 0; i < SHARDS; i++) {
        shard_t *s = &s_m->sh[i];
        if (!s->life) {
            continue;
        }
        s->life--;
        s->r += s->vr * (1.0f + energy);
        s->ang += s->spin;
        if (s->r > 260) {
            s->life = 0;
            continue;
        }
        uint8_t a = (uint8_t)(s->life * 255 / LIFE);
        uint16_t col = gfx_blend(s->col, lo, a);
        uint16_t core = gfx_blend(0xFFFF, col, (uint8_t)(a / 3));
        float th = s->th * seg * 0.5f;
        float tc = cosf(th), ts = sinf(th), oc = cosf(s->ang), os = sinf(s->ang);
        float size = s->size * shrink * (0.6f + 0.4f * (float)a / 255.0f);
        for (int k = 0; k < n; k++) {
            for (int m = 0; m < 2; m++) {          /* the shard and its mirror image */
                float sgn = m ? -1.0f : 1.0f;
                float ac = bc[k] * tc - bs[k] * ts * sgn, as = bs[k] * tc + bc[k] * ts * sgn;
                float px = (float)CX + s->r * ac, py = (float)CY + s->r * as;
                float uc = ac * oc - as * os * sgn, us = as * oc + ac * os * sgn;   /* orientation */
                float x[4], y[4];
                for (int j = 0; j < 2; j++) {
                    float f = size * (j ? 0.45f : 1.0f);
                    x[0] = px + f * uc; y[0] = py + f * us;
                    x[1] = px - f * us; y[1] = py + f * uc;
                    x[2] = px - f * uc; y[2] = py - f * us;
                    x[3] = px + f * us; y[3] = py - f * uc;
                    viz_quad(x, y, j ? core : col);
                }
            }
        }
    }
}
