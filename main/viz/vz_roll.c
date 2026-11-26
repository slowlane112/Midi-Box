/*
 * Piano Roll: notes scroll from right to left, pitch up the screen, one colour per channel.
 * The pitch range follows the music so the notes always fill the screen.
 * A keyboard strip on the left shows which pitch each row is; held notes light it.
 *
 * Button 10: scroll speed (slow / medium / fast). Button 11: background colour.
 */
#include <stdlib.h>
#include <string.h>
#include "monitor.h"
#include "theme.h"
#include "viz_common.h"
#include "vz.h"

#define SEGS     320
#define KEYS_W   12
#define TOP      18
#define BOTTOM   (VH - 2)
#define NOW_X    (VW - 6)          /* where new notes appear */

typedef struct {
    int32_t start, end;            /* frame numbers; end < 0 while held */
    uint8_t note, ch, vel;
} seg_t;

typedef struct {
    seg_t seg[SEGS];
    vz_scan_t scan;
} roll_mem_t;
static roll_mem_t *s_m;   /* only while Piano Roll is on screen */
#define s_seg  (s_m->seg)
#define s_scan (s_m->scan)
static int s_head;                  /* next slot to use */
static int32_t s_frame;
static int s_lo = 48, s_hi = 72;    /* shown pitch range (smoothed) */

static void on_note(int ch, int note, int vel, bool on, void *ctx)
{
    (void)ctx;
    if (on) {
        seg_t *s = &s_seg[s_head];
        s_head = (s_head + 1) % SEGS;
        *s = (seg_t){ .start = s_frame, .end = -1, .note = (uint8_t)note, .ch = (uint8_t)ch, .vel = (uint8_t)vel };
        return;
    }
    for (int i = 0; i < SEGS; i++) {
        seg_t *s = &s_seg[i];
        if (s->end < 0 && s->note == note && s->ch == ch && s->start >= 0) {
            s->end = s_frame;
        }
    }
}

static bool is_black(int n)
{
    int k = n % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

void vz_roll_free(void)
{
    free(s_m);
    s_m = NULL;
}

void vz_roll_draw(int colour, int opt, bool active, bool fresh)
{
    (void)active;
    if (!s_m) {
        if (!(s_m = viz_alloc(sizeof(roll_mem_t)))) {
            viz_clear(viz_bg(0));
            return;
        }
        fresh = true;
    }
    if (fresh) {
        memset(s_seg, 0, sizeof(s_seg));
        for (int i = 0; i < SEGS; i++) {
            s_seg[i].start = -1;
        }
        vz_scan_reset(&s_scan);
        s_frame = 0;
        s_lo = 48;
        s_hi = 72;
    }
    s_frame++;
    vz_scan(&s_scan, on_note, NULL);

    static const int speeds[3] = { 1, 2, 3 };
    int px = speeds[opt % 3];
    int32_t visible = (NOW_X - KEYS_W) / px;   /* frames that fit on screen */

    /* pitch range of what is on screen, eased so it doesn't jump */
    int lo = 127, hi = 0;
    for (int i = 0; i < SEGS; i++) {
        const seg_t *s = &s_seg[i];
        if (s->start < 0 || (s->end >= 0 && s_frame - s->end > visible) || s->ch == 9) {
            continue;
        }
        lo = s->note < lo ? s->note : lo;
        hi = s->note > hi ? s->note : hi;
    }
    if (lo > hi) {
        lo = 48;
        hi = 72;
    }
    lo -= 2;
    hi += 2;
    if (hi - lo < 24) {
        int mid = (lo + hi) / 2;
        lo = mid - 12;
        hi = mid + 12;
    }
    s_lo += (lo - s_lo + (lo < s_lo ? -3 : 3)) / 4;
    s_hi += (hi - s_hi + (hi < s_hi ? -3 : 3)) / 4;
    if (s_hi - s_lo < 12) {
        s_hi = s_lo + 12;
    }
    int range = s_hi - s_lo + 1;
    int area = BOTTOM - TOP;

    uint16_t bg = viz_bg(colour), lane = gfx_blend(0xFFFF, bg, 10), cline = gfx_blend(0xFFFF, bg, 40);
    viz_clear(bg);

    /* lanes for black keys, a line at every C */
    for (int n = s_lo; n <= s_hi; n++) {
        int y0 = BOTTOM - (n - s_lo + 1) * area / range;
        int y1 = BOTTOM - (n - s_lo) * area / range;
        if (is_black(n)) {
            viz_fill(KEYS_W, y0, VW - KEYS_W, y1 - y0, lane);
        }
        if (n % 12 == 0) {
            viz_fill(KEYS_W, y1 - 1, VW - KEYS_W, 1, cline);
        }
    }

    /* notes */
    for (int i = 0; i < SEGS; i++) {
        const seg_t *s = &s_seg[i];
        if (s->start < 0 || s->note < s_lo || s->note > s_hi) {
            continue;
        }
        int32_t end = s->end < 0 ? s_frame : s->end;
        int x1 = NOW_X - (int)(s_frame - end) * px;
        int x0 = NOW_X - (int)(s_frame - s->start) * px;
        if (x1 < KEYS_W) {
            continue;
        }
        if (x0 < KEYS_W) {
            x0 = KEYS_W;
        }
        if (x1 - x0 < 2) {
            x1 = x0 + 2;
        }
        int y0 = BOTTOM - (s->note - s_lo + 1) * area / range;
        int h = area / range;
        if (h < 2) {
            h = 2;
        }
        uint16_t c = viz_col(viz_hue_of_channel(s->ch));
        c = gfx_blend(c, bg, (uint8_t)(110 + s->vel));        /* softer notes are dimmer */
        viz_fill(x0, y0 + (h > 3 ? 1 : 0), x1 - x0, h > 3 ? h - 1 : h, c);
        if (s->end < 0) {
            viz_fill(x1 - 2, y0, 2, h, 0xFFFF);              /* bright tip while held */
        }
    }

    /* keyboard strip */
    for (int n = s_lo; n <= s_hi; n++) {
        int y0 = BOTTOM - (n - s_lo + 1) * area / range;
        int y1 = BOTTOM - (n - s_lo) * area / range;
        bool held = viz_note_vel(-1, n) != 0;
        uint16_t k = held ? viz_col(colour == 0 ? 0 : colour) : (is_black(n) ? C_KEY_B : C_KEY_W);
        viz_fill(0, y0, is_black(n) ? KEYS_W - 4 : KEYS_W - 1, (y1 - y0) > 1 ? y1 - y0 - (is_black(n) ? 0 : 1) : 1, k);
    }
    viz_fill(NOW_X, TOP, 1, area, gfx_blend(0xFFFF, bg, 70));

    /* octave labels on the C lines */
    gfx_t g = { framebuffer_2, VW, VH };
    for (int n = s_lo; n <= s_hi; n++) {
        if (n % 12 == 0) {
            int y = BOTTOM - (n - s_lo) * area / range;
            char lbl[4] = { 'C', (char)('0' + (n / 12 - 1)), 0, 0 };
            if (n / 12 - 1 >= 0 && y - 12 > TOP) {
                gfx_text(&g, &font_small, KEYS_W + 3, y - 12, lbl, C_DIM);
            }
        }
    }
}
