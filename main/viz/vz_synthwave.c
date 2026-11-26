/*
 * Synthwave: a retro 80s sunset. A striped sun sits on the horizon behind a range of
 * mountains whose height follows the music (bass on the left, treble on the right), and
 * a neon grid scrolls towards you. Each note sends a spark along the grid from a lane
 * set by its pitch; the kick drum makes the sun pulse, the snare flashes the horizon.
 * The grid runs faster when the music is busy; with nothing playing it rolls on slowly
 * and the stars twinkle.
 *
 * Button 10: grid speed slow / medium / fast. Button 11: theme Classic / Outrun / Ice / Toxic,
 * or Cycle (each theme for 8 s, fading into the next).
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

#define HOR     86             /* horizon row */
#define VPX     (VW / 2)
#define GROUND  (VH - HOR)
#define DEPTH_K 67.0f          /* y = HOR + K / z */
#define SPARKS  24
#define STARS   40

typedef struct { uint8_t r, g, b; } rgb_t;
typedef struct {
    rgb_t sky_top, sky_mid, sky_low, sun_top, sun_bot, grid, mtn, edge, ground;
} theme_t;

static const theme_t s_theme[4] = {
    /* Classic: purple sky, yellow-pink sun, pink grid, cyan ridge */
    { { 20, 5, 60 }, { 150, 30, 120 }, { 255, 120, 80 }, { 255, 230, 60 }, { 255, 60, 150 },
      { 255, 40, 200 }, { 25, 8, 45 }, { 0, 235, 255 }, { 18, 4, 30 } },
    /* Outrun: dark red sky, orange-red sun */
    { { 10, 0, 30 }, { 120, 0, 60 }, { 255, 90, 0 }, { 255, 240, 80 }, { 255, 30, 30 },
      { 255, 60, 0 }, { 30, 5, 20 }, { 255, 200, 0 }, { 20, 2, 10 } },
    /* Ice */
    { { 0, 5, 30 }, { 0, 60, 120 }, { 120, 220, 255 }, { 255, 255, 255 }, { 0, 180, 255 },
      { 0, 200, 255 }, { 5, 15, 40 }, { 200, 255, 255 }, { 2, 8, 25 } },
    /* Toxic */
    { { 0, 15, 5 }, { 20, 90, 10 }, { 220, 255, 0 }, { 255, 255, 120 }, { 60, 255, 0 },
      { 120, 255, 0 }, { 5, 25, 5 }, { 255, 255, 0 }, { 3, 15, 3 } },
};

typedef struct {
    float z;                   /* depth: 12 far away .. 1 at the bottom of the screen */
    float lane;                /* -1 .. 1 across the grid */
    uint16_t col;
} spark_t;

typedef struct {
    uint8_t terrain[VW];       /* fixed mountain shape */
    float band[16];            /* smoothed level per band */
    spark_t sp[SPARKS];
    int16_t sx[STARS], sy[STARS];
    uint8_t sphase[STARS], speriod[STARS];
    vz_scan_t scan;
} sw_mem_t;

static sw_mem_t *s_m;
static float s_scroll, s_sun, s_flash, s_stripe;
static int s_next;
static uint32_t s_tick;

void vz_synthwave_free(void)
{
    free(s_m);
    s_m = NULL;
}

static uint16_t mixc(rgb_t a, rgb_t b, float t)
{
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return RGB((uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t));
}

static void on_note(int ch, int note, int vel, bool on, void *ctx)
{
    (void)ctx;
    (void)vel;
    if (!on) {
        return;
    }
    if (ch == 9) {
        if (note == 35 || note == 36) {
            s_sun = 1.0f;
        } else if (note == 38 || note == 40 || note == 39) {
            s_flash = 1.0f;
        }
        return;
    }
    spark_t *s = &s_m->sp[s_next];
    s_next = (s_next + 1) % SPARKS;
    float lane = ((float)note - 60.0f) / 30.0f;
    s->lane = lane < -1 ? -1 : (lane > 1 ? 1 : lane);
    s->z = 12.0f;
    s->col = viz_rainbow((uint8_t)(128 + (note % 12) * 64 / 12));   /* cyan .. blue .. pink */
}

static void make(void)
{
    uint32_t r = 0x51A7;
    float a = 0, b = 0;
    for (int x = 0; x < VW; x++) {
        /* two layers of gentle random ridges */
        if (x % 18 == 0) {
            r = r * 1664525u + 1013904223u;
            a = (float)((r >> 24) % 9);
        }
        if (x % 5 == 0) {
            r = r * 1664525u + 1013904223u;
            b = (float)((r >> 24) % 4);
        }
        float base = 10.0f + a + b;
        s_m->terrain[x] = (uint8_t)base;
    }
    /* smooth it so it reads as mountains, not noise */
    for (int pass = 0; pass < 2; pass++) {
        uint8_t t[VW];
        memcpy(t, s_m->terrain, VW);
        for (int x = 1; x < VW - 1; x++) {
            s_m->terrain[x] = (uint8_t)((t[x - 1] + 2 * t[x] + t[x + 1]) / 4);
        }
    }
    for (int i = 0; i < STARS; i++) {
        r = r * 1664525u + 1013904223u; s_m->sx[i] = (int16_t)((r >> 8) % VW);
        r = r * 1664525u + 1013904223u; s_m->sy[i] = (int16_t)((r >> 8) % 50);
        r = r * 1664525u + 1013904223u;
        s_m->speriod[i] = (uint8_t)(60 + (r >> 8) % 150);
        s_m->sphase[i] = (uint8_t)(r >> 24);
    }
}

void vz_synthwave_draw(int colour, int opt, bool active, bool fresh)
{
    if (!s_m) {
        if (!(s_m = viz_alloc(sizeof(sw_mem_t)))) {
            viz_clear(C_BG);
            return;
        }
        make();
        fresh = true;
    }
    if (fresh) {
        memset(s_m->sp, 0, sizeof(s_m->sp));
        memset(s_m->band, 0, sizeof(s_m->band));
        vz_scan_reset(&s_m->scan);
        s_sun = s_flash = 0;
    }
    theme_t blend;
    const theme_t *th = &s_theme[colour % 4];
    if (colour >= 4) {   /* Cycle: fade between two themes, field by field */
        int a, b;
        uint8_t t;
        viz_get_cycle(&a, &b, &t);
        const uint8_t *pa = (const uint8_t *)&s_theme[a % 4], *pb = (const uint8_t *)&s_theme[b % 4];
        uint8_t *po = (uint8_t *)&blend;
        for (size_t i = 0; i < sizeof(theme_t); i++) {
            po[i] = (uint8_t)(pa[i] + (pb[i] - pa[i]) * t / 255);
        }
        th = &blend;
    }
    vz_scan(&s_m->scan, on_note, NULL);
    s_tick++;

    /* loudness per band from the decaying per-note energy (notes 24..107 over 16 bands) */
    float lvl[16] = { 0 };
    for (int n = 24; n < 108; n++) {
        float e = (float)g_mon.energy[n] / 127.0f;
        int b = (n - 24) * 16 / 84;
        lvl[b] = e > lvl[b] ? e : lvl[b];
    }
    float busy = 0;
    for (int b = 0; b < 16; b++) {
        float v = lvl[b];
        s_m->band[b] = v > s_m->band[b] ? v : s_m->band[b] * 0.92f;
        busy += s_m->band[b];
    }
    busy = busy > 6 ? 1.0f : busy / 6.0f;

    /* sky */
    for (int y = 0; y < HOR; y++) {
        float t = (float)y / HOR;
        uint16_t c = t < 0.65f ? mixc(th->sky_top, th->sky_mid, t / 0.65f) : mixc(th->sky_mid, th->sky_low, (t - 0.65f) / 0.35f);
        viz_fill(0, y, VW, 1, c);
    }
    /* stars */
    for (int i = 0; i < STARS; i++) {
        uint32_t p = s_m->speriod[i], t = (s_tick + (uint32_t)s_m->sphase[i] * p / 256) % p;
        uint32_t tri = t < p / 2 ? t * 510 / p : (p - t) * 510 / p;
        uint16_t bg = framebuffer_2[s_m->sy[i] * VW + s_m->sx[i]];
        viz_px(s_m->sx[i], s_m->sy[i], gfx_blend(0xFFFF, bg, (uint8_t)(60 + tri * 150 / 255)));
    }
    /* sun: gradient, stripes in the lower half drifting down, pulses on the kick */
    {
        float R = 40.0f + s_sun * 6.0f;
        int cy = HOR - 4;
        s_stripe += 0.12f;
        for (int y = (int)(cy - R); y <= cy; y++) {
            if (y < 0) {
                continue;
            }
            float dy = (float)(cy - y);
            float w2 = R * R - dy * dy;
            int w = w2 > 0 ? (int)sqrtf(w2) : 0;
            float t = 1.0f - dy / R;
            float lower = (t - 0.45f) / 0.55f;   /* 0 at the first stripe .. 1 at the horizon */
            if (lower > 0) {
                int gap = 1 + (int)(lower * 4.0f);
                int ph = ((int)(dy + 64.0f - s_stripe)) % 9;
                ph = ph < 0 ? ph + 9 : ph;
                if (ph < gap) {
                    continue;
                }
            }
            uint16_t c = mixc(th->sun_top, th->sun_bot, t);
            if (s_sun > 0.01f) {
                c = gfx_blend(0xFFFF, c, (uint8_t)(s_sun * 90));
            }
            viz_fill(VPX - w, y, 2 * w + 1, 1, c);
        }
        s_sun *= 0.88f;
    }
    /* mountains: the shape scaled by the band under each column */
    uint16_t mtn = mixc(th->mtn, th->mtn, 0), edge = mixc(th->edge, th->edge, 0);
    for (int x = 0; x < VW; x++) {
        float fb = (float)x * 15.0f / (float)(VW - 1);
        int b = (int)fb;
        float fr = fb - (float)b;
        float l = s_m->band[b] * (1 - fr) + (b < 15 ? s_m->band[b + 1] : 0) * fr;
        int h = (int)((float)s_m->terrain[x] * (0.45f + 1.6f * l));
        h = h > HOR - 6 ? HOR - 6 : h;
        viz_fill(x, HOR - h, 1, h, mtn);
        viz_px(x, HOR - h, edge);
    }
    /* ground and grid */
    uint16_t ground = mixc(th->ground, th->ground, 0);
    viz_fill(0, HOR, VW, GROUND, ground);
    uint16_t grid = mixc(th->grid, th->grid, 0);
    static const float speed_of[3] = { 0.03f, 0.06f, 0.10f };
    float speed = speed_of[opt % 3] * (active ? 1.0f + busy * 1.5f : 0.5f);
    s_scroll += speed;
    if (s_scroll >= 1.0f) {
        s_scroll -= 1.0f;
    }
    for (int n = 0; n < 24; n++) {   /* lines across, closer together towards the horizon */
        float z = (float)n + 1.2f - s_scroll;
        int y = HOR + (int)(DEPTH_K / z);
        if (y >= VH || y <= HOR) {
            continue;
        }
        uint8_t a = (uint8_t)((y - HOR) * 255 / GROUND);
        viz_fill(0, y, VW, 1, gfx_blend(grid, ground, a < 40 ? 40 : a));
    }
    for (int k = -16; k <= 16; k++) {   /* lines running to the vanishing point, brighter near you */
        float dxb = (float)k * 30.0f;
        for (int s = 0; s < 4; s++) {
            float t0 = (float)s / 4.0f, t1 = (float)(s + 1) / 4.0f;
            uint8_t a = (uint8_t)(50 + s * 68);
            viz_line(VPX + (int)(dxb * t0), HOR + (int)(t0 * GROUND), VPX + (int)(dxb * t1),
                     HOR + (int)(t1 * GROUND), gfx_blend(grid, ground, a));
        }
    }
    uint16_t horizon = s_flash > 0.01f ? gfx_blend(0xFFFF, grid, (uint8_t)(s_flash * 200)) : grid;
    viz_fill(0, HOR, VW, 1, horizon);
    s_flash *= 0.85f;
    /* sparks coming towards you */
    for (int i = 0; i < SPARKS; i++) {
        spark_t *s = &s_m->sp[i];
        if (s->z <= 0) {
            continue;
        }
        s->z -= 0.12f * (1.0f + busy);
        if (s->z < 1.0f) {
            s->z = 0;
            continue;
        }
        float y = (float)HOR + DEPTH_K / s->z;
        float t = (y - HOR) / (float)GROUND;
        int x = VPX + (int)(s->lane * 200.0f * t);
        int r = 1 + (int)(2.5f / s->z * 3.0f);
        r = r > 3 ? 3 : r;
        viz_fill(x - r, (int)y - r, 2 * r + 1, 2 * r + 1, s->col);
        viz_px(x, (int)y, 0xFFFF);
    }
}
