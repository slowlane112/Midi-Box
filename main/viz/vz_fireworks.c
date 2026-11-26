/*
 * Fireworks: every note launches a burst. Low notes burst on the left, high notes on the
 * right, louder notes make bigger bursts, drums make white crackles near the bottom.
 * Up to 480 sparks, fixed-point physics. A sky of small twinkling stars behind them.
 *
 * Button 10: trails on/off (trails fade the previous frame instead of clearing it).
 * Button 11: colour (0 = colour by channel, others = that standard colour).
 */
#include <stdlib.h>
#include <string.h>
#include "monitor.h"
#include "theme.h"
#include "viz_common.h"
#include "vz.h"

#define SPARKS 480
#define GRAV   3          /* 1/256 px per frame^2 */

typedef struct {
    int32_t x, y;         /* 1/256 px */
    int16_t vx, vy;
    uint8_t life, max;
    uint16_t col;
} spark_t;

static int s_next;
static int s_colour;
static uint32_t s_rng = 0x1234567;
static int s_bursts_this_frame;

/* background stars (viz_common) */
#define STARS 90
static bool s_stars_made;
static uint32_t s_tick;

/* working memory, only while Fireworks is on screen */
typedef struct {
    spark_t sp[SPARKS];
    vz_scan_t scan;
    viz_star_t star[STARS];
} fw_mem_t;
static fw_mem_t *s_m;
#define s_sp   (s_m->sp)
#define s_scan (s_m->scan)
#define s_star (s_m->star)

void vz_fireworks_free(void)
{
    free(s_m);
    s_m = NULL;
    s_stars_made = false;
}

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* unit circle in 32 steps, *256 */
static const int16_t s_cos[32] = {
    256, 251, 236, 213, 181, 142, 98, 50, 0, -50, -98, -142, -181, -213, -236, -251,
    -256, -251, -236, -213, -181, -142, -98, -50, 0, 50, 98, 142, 181, 213, 236, 251,
};

static void burst(int x, int y, int n, int power, uint16_t col, int life)
{
    for (int i = 0; i < n; i++) {
        spark_t *s = &s_sp[s_next];
        s_next = (s_next + 1) % SPARKS;
        int a = (int)(rnd() & 31);
        int sp = power * (90 + (int)(rnd() % 166)) / 256;
        s->x = x << 8;
        s->y = y << 8;
        s->vx = (int16_t)(s_cos[a] * sp / 256);
        s->vy = (int16_t)(s_cos[(a + 24) & 31] * sp / 256 - 40);
        s->max = s->life = (uint8_t)(life + (int)(rnd() % 16));
        s->col = col;
    }
}

static void on_note(int ch, int note, int vel, bool on, void *ctx)
{
    (void)ctx;
    if (!on || s_bursts_this_frame >= 6) {
        return;   /* a big chord still looks like a few bursts, not a white-out */
    }
    s_bursts_this_frame++;
    if (ch == 9) {
        int x = 20 + (int)(rnd() % (VW - 40));
        burst(x, VH - 20 - (int)(rnd() % 20), 8 + vel / 16, 120, gfx_blend(0xFFFF, C_BG, 200), 18);
        return;
    }
    int lo = 28, hi = 100;
    int n = note < lo ? lo : note > hi ? hi : note;
    int x = 24 + (n - lo) * (VW - 48) / (hi - lo) + (int)(rnd() % 17) - 8;
    int y = 22 + (int)(rnd() % (VH / 2));
    uint16_t col = s_colour == 0 ? viz_col(viz_hue_of_channel(ch)) : viz_col(s_colour);
    burst(x, y, 24 + vel / 3, 260 + vel * 3, col, 44);
}

/* trails: halve every pixel's distance to the background (rounding towards it, so
 * old trails always end exactly on the background) */
static void fade(uint16_t bg)
{
    uint16_t b = (uint16_t)((bg >> 8) | (bg << 8));
    int br = b >> 11, bgn = (b >> 5) & 63, bb = b & 31;
    uint16_t *p = framebuffer_2;
    for (int i = 0; i < VW * VH; i++) {
        uint16_t c = p[i];
        if (c == bg) {
            continue;
        }
        c = (uint16_t)((c >> 8) | (c << 8));
        int r = br + ((c >> 11) - br) / 2;
        int g = bgn + (((c >> 5) & 63) - bgn) / 2;
        int bl = bb + ((c & 31) - bb) / 2;
        c = (uint16_t)((r << 11) | (g << 5) | bl);
        p[i] = (uint16_t)((c >> 8) | (c << 8));
    }
}

void vz_fireworks_draw(int colour, int opt, bool active, bool fresh)
{
    (void)active;
    uint16_t bg = viz_bg(0);
    s_colour = colour;
    if (!s_m) {
        if (!(s_m = viz_alloc(sizeof(fw_mem_t)))) {
            viz_clear(bg);
            return;
        }
        fresh = true;
    }
    if (fresh) {
        memset(s_sp, 0, sizeof(s_sp));
        vz_scan_reset(&s_scan);
        viz_clear(bg);
    }
    s_bursts_this_frame = 0;
    vz_scan(&s_scan, on_note, NULL);

    if (opt) {
        fade(bg);
    } else {
        viz_clear(bg);
    }
    if (!s_stars_made) {
        viz_stars_make(s_star, STARS, 0x9E3779B9);
        s_stars_made = true;
    }
    viz_stars_draw(s_star, STARS, ++s_tick, bg);
    for (int i = 0; i < SPARKS; i++) {
        spark_t *s = &s_sp[i];
        if (!s->life) {
            continue;
        }
        s->life--;
        s->vy = (int16_t)(s->vy + GRAV);
        s->vx = (int16_t)(s->vx * 250 / 256);   /* air drag */
        s->x += s->vx;
        s->y += s->vy;
        int x = s->x >> 8, y = s->y >> 8;
        if ((unsigned)x >= VW || (unsigned)y >= VH) {
            s->life = 0;
            continue;
        }
        uint8_t a = (uint8_t)(s->life * 255 / s->max);
        uint16_t c = gfx_blend(s->col, bg, a);
        viz_px(x, y, c);
        viz_px(x + 1, y, c);
        viz_px(x, y + 1, c);
        viz_px(x + 1, y + 1, c);
        if (a > 170) {   /* young sparks are bigger and have a hot centre */
            viz_px(x - 1, y, c);
            viz_px(x, y - 1, c);
            viz_px(x + 2, y + 1, c);
            viz_px(x + 1, y + 2, c);
            viz_px(x, y, gfx_blend(0xFFFF, c, 140));
        }
    }
}
